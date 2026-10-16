#include "manyrowssaver.h"

#include "io/csvformat.h"
#include "io/formatregistry.h"

#include <filesystem>
#include <stdexcept>

namespace manyrows {

    namespace {
        // Thrown from the cell source to abandon the write; caught in writeTable(), never escapes.
        struct SaveCancelled {};
    }

    ManyRowsSaver::ManyRowsSaver(const ManyRowsModel& model, std::string target)
        : model_(model), target_(std::move(target)), temp_(tempPathFor(target_)),
          totalRows_(model.rowCount() + (model.hasTitles() ? 1 : 0)) {
        worker_ = std::thread([this]() { run(); });
    }

    ManyRowsSaver::~ManyRowsSaver() {
        cancel_.store(true);
        if (worker_.joinable()) {
            worker_.join();
        }
        if (!released_) {
            std::error_code ignored;
            std::filesystem::remove(std::filesystem::path(temp_), ignored);
        }
    }

    float ManyRowsSaver::fraction() const {
        if (totalRows_ == 0) {
            return state() == State::Running ? 0.0f : 1.0f;
        }
        float value = float(double(rowsWritten()) / double(totalRows_));
        return value > 1.0f ? 1.0f : value;
    }

    std::string ManyRowsSaver::tempPathFor(const std::string& target) {
        std::filesystem::path path(target);
        return (path.parent_path() / (path.stem().string() + ".saving" + path.extension().string())).string();
    }

    void ManyRowsSaver::run() {
        try {
            bool whole = writeTable(model_, temp_, &cancel_, &rowsWritten_);
            state_.store(whole ? State::Done : State::Cancelled);
        } catch (const std::exception& problem) {
            error_ = problem.what();
            state_.store(State::Failed);   // after error_: the UI reads it only once it sees Failed
        }
    }

    bool ManyRowsSaver::writeTable(const ManyRowsModel& model, const std::string& path, const std::atomic<bool>* cancel,
        std::atomic<std::size_t>* rowsWritten) {
        const io::TableFormat* format = io::FormatRegistry::forPath(path);
        if (format == nullptr) {
            // A name with no known extension is saved as CSV.
            static const io::CsvFormat fallback;
            format = &fallback;
        }
        if (!format->canWrite()) {
            throw std::runtime_error("manyrows cannot write " + format->name() + " files - save as .csv");
        }
        const bool header = model.hasTitles();
        const std::size_t rows = model.rowCount() + (header ? 1 : 0);
        try {
            // The writer asks for cells row by row, left to right: the first column of a row marks its start.
            format->write(path, rows, model.columnCount(), [&](std::size_t row, std::size_t column) {
                if (column == 0) {
                    if (rowsWritten != nullptr) {
                        rowsWritten->store(row);
                    }
                    if (cancel != nullptr && (row & 0x3FF) == 0 && cancel->load()) {
                        throw SaveCancelled();
                    }
                }
                if (header) {
                    return row == 0 ? model.columnTitle(column) : model.cellText(row - 1, column);
                }
                return model.cellText(row, column);
            });
        } catch (const SaveCancelled&) {
            return false;
        }
        if (rowsWritten != nullptr) {
            rowsWritten->store(rows);
        }
        return true;
    }

}

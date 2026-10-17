#include "manyrowsloader.h"

#include "io/formatregistry.h"

#include <memory>
#include <stdexcept>

namespace manyrows {

    ManyRowsLoader::ManyRowsLoader(std::string path) : path_(std::move(path)) {
        worker_ = std::thread([this]() { run(); });
    }

    ManyRowsLoader::~ManyRowsLoader() {
        cancel();   // also releases a worker waiting for the sheet choice
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void ManyRowsLoader::cancel() {
        cancel_.store(true);
        std::lock_guard<std::mutex> lock(mutex_);
        sheetAnswered_ = true;   // releases a worker waiting for the sheet
        answered_.notify_all();
    }

    void ManyRowsLoader::chooseSheet(std::size_t index) {
        std::lock_guard<std::mutex> lock(mutex_);
        chosenSheet_ = index;
        sheetAnswered_ = true;
        answered_.notify_all();
    }

    float ManyRowsLoader::fraction() const {
        std::size_t total = totalBytes();
        if (total == 0) {
            return 0.0f;
        }
        float value = float(double(bytesRead()) / double(total));
        return value > 1.0f ? 1.0f : value;
    }

    void ManyRowsLoader::run() {
        try {
            auto choose = [this](const std::vector<std::string>& sheets) -> std::optional<std::size_t> {
                sheets_ = sheets;
                state_.store(State::AwaitingSheet);
                std::unique_lock<std::mutex> lock(mutex_);
                answered_.wait(lock, [this]() { return sheetAnswered_; });
                if (cancel_.load() || !chosenSheet_.has_value()) {
                    return std::nullopt;
                }
                state_.store(State::Running);
                return chosenSheet_;
            };
            bool whole = readInto(path_, staging_, &cancel_, &bytesRead_, &totalBytes_, &rows_, choose, &sheetName_);
            state_.store(whole ? State::Done : State::Cancelled);
        } catch (const std::exception& problem) {
            error_ = problem.what();
            state_.store(State::Failed);   // after error_: the UI reads it only once it sees Failed
        }
    }

    bool ManyRowsLoader::readInto(const std::string& path, ManyRowsModel& into, const std::atomic<bool>* cancel,
        std::atomic<std::size_t>* bytesRead, std::atomic<std::size_t>* totalBytes, std::atomic<std::size_t>* rows, const SheetChooser& chooser, std::string* sheetName) {
        const io::TableFormat* format = io::FormatRegistry::forPath(path);
        if (format == nullptr) {
            throw std::runtime_error("manyrows cannot read this kind of file");
        }
        std::unique_ptr<io::TableReader> reader = format->open(path);
        const io::TableInfo& info = reader->info();   // refreshed in place by selectSheet()
        if (info.sheets.size() > 1 && chooser) {
            std::optional<std::size_t> picked = chooser(info.sheets);
            if (!picked.has_value()) {
                return false;   // the user cancelled the choice: nothing was read
            }
            if (*picked >= info.sheets.size()) {
                throw std::runtime_error("manyrows: no such sheet");
            }
            reader->selectSheet(*picked);
        }
        if (sheetName != nullptr) {
            for (const auto& item : info.metadata) {
                if (item.first == "sheet") {
                    *sheetName = item.second;
                }
            }
        }
        if (totalBytes != nullptr) {
            totalBytes->store(info.fileBytes);
        }
        std::size_t columns = info.columnCount.value.has_value() ? *info.columnCount.value : 0;
        // An estimate can undercount the width (only the first rows were sampled): rows are cut to it, so
        // widen to the header's width at least. An exact count is trusted.
        if (info.hasHeaderRow && info.columnTitles.size() > columns) {
            columns = info.columnTitles.size();
        }

        std::vector<std::string> titles = info.hasHeaderRow ? info.columnTitles : std::vector<std::string>();
        bool skipHeader = info.hasHeaderRow;
        into.beginLoad(columns, std::move(titles));
        bool cancelled = false;
        reader->readRows(
            [&](const std::vector<std::string>& cells) {
                if (skipHeader) {
                    skipHeader = false;
                    return true;
                }
                into.appendRow(cells);
                if (rows != nullptr) {
                    rows->store(into.rowCount());
                }
                return true;
            },
            [&](std::size_t read, std::size_t total) {
                if (bytesRead != nullptr) {
                    bytesRead->store(read);
                }
                if (totalBytes != nullptr && total != 0) {
                    totalBytes->store(total);
                }
                if (cancel != nullptr && cancel->load()) {
                    cancelled = true;
                    return false;
                }
                return true;
            });
        into.endLoad();
        return !cancelled;
    }

}

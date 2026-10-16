#include "manyrowsdocument.h"

#include "manyrowsloader.h"
#include "manyrowssaver.h"

#include <newui/utils.h>

#include <filesystem>
#include <memory>
#include <stdexcept>

namespace manyrows {

    bool ManyRowsDocument::loadStaged(ManyRowsModel& staged, const std::string& path) {
        staged_ = &staged;
        bool ok = load(path);   // readFromFile() below adopts the staged table instead of reading the file
        staged_ = nullptr;
        return ok;
    }

    bool ManyRowsDocument::readFromFile(const std::string& path) {
        lastError_.clear();
        try {
            if (staged_ != nullptr) {
                model_.adoptLoaded(*staged_);
                loadedRows_ = model_.rowCount();
                return true;
            }
            // Rows are read straight into the model; only a failure mid-way leaves it with what was read.
            ManyRowsLoader::readInto(path, model_);
            loadedRows_ = model_.rowCount();
            return true;
        } catch (const std::exception& error) {
            lastError_ = error.what();
            if (!model_.isGenerated()) {
                model_.endLoad();   // whatever was read stays visible rather than a half-built table
            }
            return false;
        }
    }

    bool ManyRowsDocument::saveStaged(const std::string& tempPath, const std::string& target) {
        stagedSave_ = tempPath;
        bool ok = save(target);   // writeToFile() below moves the finished temporary file instead of writing
        stagedSave_.clear();
        return ok;
    }

    bool ManyRowsDocument::writeToFile(const std::string& path) {
        lastError_.clear();
        try {
            if (!stagedSave_.empty()) {
                std::filesystem::rename(std::filesystem::path(newui::utf8ToWide(stagedSave_)),
                    std::filesystem::path(newui::utf8ToWide(path)));   // replaces an existing target
                return true;
            }
            ManyRowsSaver::writeTable(model_, path);
            return true;
        } catch (const std::exception& error) {
            lastError_ = error.what();
            return false;
        }
    }

}

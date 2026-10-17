#pragma once

#include "manyrowsmodel.h"

#include <newui/models.h>

#include <string>

namespace manyrows {

    // The file behind the table: newui's Document (path, modified flag, load/save with the .bak backup)
    // bound to the ManyRowsModel. Loading and saving go through the io format registry, chosen by the file
    // extension (".csv", ".tsv"; an unknown extension on an existing file is sniffed).
    //
    // Loading replaces the model's content with the file's rows (a detected header row becomes the column
    // titles instead of a data row); saving writes the titles as a header row when the model has them, then
    // every row with the user's edits applied.
    //
    // load()/save() return false on failure; lastError() then says why (a bad format, an unreadable file).
    // @reflect ignore=true
    class ManyRowsDocument : public newui::Document {
    public:
        explicit ManyRowsDocument(ManyRowsModel& model) : model_(model) {}

        ManyRowsModel& model() { return model_; }

        const std::string& lastError() const { return lastError_; }

        // load(path) for a file a ManyRowsLoader already read on a worker thread: takes over `staged` (an
        // O(1) swap into the model) instead of reading again, then adopts the path and clears the modified flag
        // exactly as load() does. False (lastError() set) when the staged table is unusable.
        bool loadStaged(ManyRowsModel& staged, const std::string& path);

        // save(target) for a file a ManyRowsSaver already wrote to `tempPath` on a worker thread: moves it over
        // `target` (after the usual .bak backup of an existing file) instead of writing again, then adopts the
        // path and clears the modified flag exactly as save() does. False (lastError() set) when it cannot.
        bool saveStaged(const std::string& tempPath, const std::string& target);

        // What the last successful load() reported: data rows read and the file's columns.
        std::size_t loadedRows() const { return loadedRows_; }

    protected:
        bool readFromFile(const std::string& path) override;
        bool writeToFile(const std::string& path) override;

    private:
        ManyRowsModel& model_;
        std::string stagedSave_;            // set only while saveStaged() runs
        ManyRowsModel* staged_ = nullptr;   // set only while loadStaged() runs
        std::string lastError_;
        std::size_t loadedRows_ = 0;
    };

}

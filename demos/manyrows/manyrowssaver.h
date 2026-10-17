#pragma once

#include "manyrowsmodel.h"

#include <atomic>
#include <cstddef>
#include <string>
#include <thread>

namespace manyrows {

    // Writes the table to a file on a worker thread, the counterpart of ManyRowsLoader.
    //
    // The worker only READS the model (cellText() is const), so the UI may keep painting it; the one rule is
    // that nothing may edit the model until the save is finished - the controller refuses edits meanwhile.
    //
    // It never writes the target itself: it writes a sibling temporary file ("name.saving.csv" - same
    // extension, so the format and delimiter come out the same) and the caller renames it over the target once
    // state() is Done (ManyRowsDocument::saveStaged()), which also leaves the old file intact if the save
    // fails or is cancelled. The thread starts in the constructor; the destructor cancels, joins and deletes a
    // temporary file that was not handed over.
    // @reflect ignore=true
    class ManyRowsSaver {
    public:
        enum class State { Running, Done, Failed, Cancelled };

        // `target` is where the file will end up; the model must outlive the saver.
        ManyRowsSaver(const ManyRowsModel& model, std::string target);
        ~ManyRowsSaver();

        ManyRowsSaver(const ManyRowsSaver&) = delete;
        ManyRowsSaver& operator=(const ManyRowsSaver&) = delete;

        const std::string& target() const { return target_; }
        // The temporary file being written (same folder as the target).
        const std::string& tempPath() const { return temp_; }
        State state() const { return state_.load(); }
        bool finished() const { return state() != State::Running; }

        std::size_t rowsWritten() const { return rowsWritten_.load(); }
        std::size_t totalRows() const { return totalRows_; }
        float fraction() const;

        void cancel() { cancel_.store(true); }
        // Why a Failed save failed. Valid once state() is Failed.
        const std::string& error() const { return error_; }
        // The caller took the temporary file (renamed it): the destructor must not delete it.
        void release() { released_ = true; }

        // The sibling temporary path for `target`: "dir/name.saving.ext".
        static std::string tempPathFor(const std::string& target);

        // Writes `model` to `path` (titles as a header row when it has them), through the format chosen by
        // the path's extension (CSV for an unknown one). `cancel` and `rowsWritten` are optional; a cancel
        // returns false and leaves a partial file at `path`. Throws std::runtime_error on an I/O error.
        static bool writeTable(const ManyRowsModel& model, const std::string& path, const std::atomic<bool>* cancel = nullptr,
            std::atomic<std::size_t>* rowsWritten = nullptr);

    private:
        void run();

        const ManyRowsModel& model_;
        std::string target_;
        std::string temp_;
        std::size_t totalRows_;
        std::atomic<State> state_{State::Running};
        std::atomic<bool> cancel_{false};
        std::atomic<std::size_t> rowsWritten_{0};
        std::string error_;
        bool released_ = false;
        std::thread worker_;  // last member: starts after everything above exists
    };

}

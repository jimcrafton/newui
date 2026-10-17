#pragma once

#include "manyrowsmodel.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace manyrows {

    // Reads one file into a table on a worker thread, so a million-row file never freezes the window.
    //
    // The worker builds a PRIVATE staging table (never the one on screen); the UI thread watches
    // state()/fraction()/rowsRead() (plain atomics - poll them from a timer) and, once the state is Done,
    // hands table() to ManyRowsModel::adoptLoaded(), which is an O(1) swap. Nothing the UI reads is ever
    // touched by the worker, so no locking is needed beyond the atomics.
    //
    // The thread starts in the constructor. cancel() asks it to stop at the next progress callback (a
    // 256 KB chunk of the file at most); the destructor cancels and joins, so destroying a loader that is
    // still running is safe and quick.
    //
    // A workbook with several sheets (XLS) pauses the worker in State::AwaitingSheet until the UI thread
    // answers with chooseSheet() - the file is opened once, so the choice costs no second read. cancel() ends
    // the wait too.
    //
    // readInto() is the same read as a plain synchronous function (what ManyRowsDocument::load() uses).
    // @reflect ignore=true
    class ManyRowsLoader {
    public:
        enum class State { Running, AwaitingSheet, Done, Failed, Cancelled };

        explicit ManyRowsLoader(std::string path);
        ~ManyRowsLoader();

        ManyRowsLoader(const ManyRowsLoader&) = delete;
        ManyRowsLoader& operator=(const ManyRowsLoader&) = delete;

        const std::string& path() const { return path_; }
        State state() const { return state_.load(); }
        bool finished() const { State s = state(); return s != State::Running && s != State::AwaitingSheet; }

        // Data rows stored so far (the header row is not counted).
        std::size_t rowsRead() const { return rows_.load(); }
        std::size_t bytesRead() const { return bytesRead_.load(); }
        // The file's size in bytes (0 until the reader has opened it).
        std::size_t totalBytes() const { return totalBytes_.load(); }
        // 0..1: bytes consumed / file size; 0 until the size is known.
        float fraction() const;

        void cancel();

        // While state() is AwaitingSheet: the sheets to pick from, and the answer (an index into them).
        const std::vector<std::string>& sheetNames() const { return sheets_; }
        void chooseSheet(std::size_t index);
        // The sheet that was read ("" for a single-table file); valid once the read has started.
        const std::string& sheetName() const { return sheetName_; }

        // Why a Failed load failed. Valid once state() is Failed.
        const std::string& error() const { return error_; }
        // The finished table. Valid once state() is Done; hand it to ManyRowsModel::adoptLoaded().
        ManyRowsModel& table() { return staging_; }

        // Picks the sheet of a multi-sheet file from its names: an index, or nullopt to give up (cancel).
        typedef std::function<std::optional<std::size_t>(const std::vector<std::string>& sheets)> SheetChooser;

        // Reads `path` into `into` (beginLoad ... endLoad), the whole file or until `cancel` turns true.
        // Returns true when the whole file was read, false when cancelled (what was read is left in `into`).
        // Throws std::runtime_error for an unreadable / unsupported file. `bytesRead`, `totalBytes` and
        // `rows` (each optional) are updated as it goes.
        static bool readInto(const std::string& path, ManyRowsModel& into, const std::atomic<bool>* cancel = nullptr,
            std::atomic<std::size_t>* bytesRead = nullptr, std::atomic<std::size_t>* totalBytes = nullptr,
            std::atomic<std::size_t>* rows = nullptr, const SheetChooser& chooser = nullptr, std::string* sheetName = nullptr);

    private:
        void run();

        std::string path_;
        ManyRowsModel staging_{0};   // built by the worker only, until state() is Done
        std::atomic<State> state_{State::Running};
        std::atomic<bool> cancel_{false};
        std::atomic<std::size_t> rows_{0};
        std::atomic<std::size_t> bytesRead_{0};
        std::atomic<std::size_t> totalBytes_{0};
        std::mutex mutex_;                  // guards chosenSheet_ / sheetAnswered_
        std::condition_variable answered_;
        std::vector<std::string> sheets_;   // written by the worker before state_ becomes AwaitingSheet
        std::optional<std::size_t> chosenSheet_;
        bool sheetAnswered_ = false;
        std::string sheetName_;
        std::string error_;   // written by the worker before state_ leaves Running
        std::thread worker_;  // last member: starts after everything above exists
    };

}

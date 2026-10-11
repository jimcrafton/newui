#pragma once

#include "newui/delegate.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace newui {

class RunLoop;

// Watches folders for file changes. Each watch runs on its own worker thread (blocked in
// ReadDirectoryChangesW), so watching never touches the UI thread. Raw OS notifications are
// coalesced over a quiet period (an editor save is several events) and delivered as one batch:
// the worker posts to the RunLoop given at construction, and that task fires onChanged on the
// loop's own thread.
//
// Paths are UTF-8 with '/' separators. Destroy a FileWatcher on the loop's thread: destruction
// stops the workers and discards anything not yet delivered.
//@reflect ignore=true
class FileWatcher {
public:
    enum class Action {
        Added,
        Removed,
        Modified,
        Renamed,    // path is the new name, oldPath the old one
        Overflow    // the OS dropped events; path is the watched folder: rescan it
    };

    struct Change {
        Action action = Action::Modified;
        std::string path;
        std::string oldPath;   // Renamed only
    };

    using Changes = std::vector<Change>;
    using WatchId = unsigned int;
    static constexpr WatchId kInvalidWatch = 0;

    struct Options {
        bool recursive = true;
        std::chrono::milliseconds debounce{200};   // quiet time before a batch is delivered
        // Called on the worker thread with a full path; true drops the change (e.g. a build folder).
        std::function<bool(const std::string&)> ignore;
    };

    // loop: the loop onChanged is delivered on; it must outlive the watcher.
    explicit FileWatcher(RunLoop& loop);
    ~FileWatcher();
    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    // Starts watching folder; kInvalidWatch if it cannot be opened.
    WatchId watch(const std::string& folder);
    WatchId watch(const std::string& folder, const Options& options);

    // Watches single files, any number of them across folders. Files in one folder share a worker
    // watching just that folder; options (recursive is ignored) come from the first file added
    // there. Reports Modified/Removed/Renamed for the file, and Added when it is created or renamed
    // into place. kInvalidWatch if the folder cannot be opened.
    WatchId watchFile(const std::string& file);
    WatchId watchFile(const std::string& file, const Options& options);

    // Ends a watch() or watchFile(); a folder's worker stops with its last file.
    void unwatch(WatchId id);
    void unwatchAll();

    Delegate<FileWatcher, const Changes&> onChanged;

private:
    struct Watch;
    struct State;
    struct FileEntry {
        Watch* watch = nullptr;
        std::string key;   // the file's lower-cased path
    };

    // Opens folder and starts its worker; null if it cannot be opened. filesOnly: report just the
    // files watchFile() registers.
    Watch* startWatch(const std::string& folder, const Options& options, bool filesOnly);

    // The task a worker posts to the loop: it hands the batches the workers have queued to onChanged.
    static std::function<void()> fireOnChangedTask(const std::shared_ptr<State>& state, FileWatcher* self);

    std::shared_ptr<State> state_;
    RunLoop& loop_;
    WatchId nextId_ = 1;
    std::vector<std::unique_ptr<Watch>> watches_;
    std::map<WatchId, FileEntry> files_;
};

}

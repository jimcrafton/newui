#include "newui/filewatcher.h"
#include "newui/runloop.h"
#include "newui/utils.h"

#include <future>
#include <map>
#include <thread>

namespace newui {

namespace {

    // The most latency debouncing may add before a batch goes out regardless: a steady stream of
    // events must not postpone delivery forever.
    constexpr int kMaxDelayFactor = 10;
    constexpr DWORD kBufferWords = 16384;   // 64 KB of FILE_NOTIFY_INFORMATION

}

// Shared by the watcher and every posted delivery task, so a task that runs after the watcher is
// gone finds alive == false and does nothing.
struct FileWatcher::State {
    std::atomic<bool> alive{ true };
    std::mutex mutex;
    Changes ready;   // batches from the workers not yet handed to onChanged
};

struct FileWatcher::Watch {
    WatchId id = kInvalidWatch;
    std::string root;
    Options options;
    HANDLE directory = INVALID_HANDLE_VALUE;
    HANDLE stopEvent = nullptr;
    std::thread thread;

    // watchFile() watches: only these files (lower-cased) are reported, each with the number of
    // watchFile() calls holding it. Read by the worker, so guarded.
    bool filesOnly = false;
    std::mutex filesMutex;
    std::map<std::string, int> files;

    bool tracks(const std::string& path)
    {
        std::lock_guard<std::mutex> lock(filesMutex);
        return files.count(toLowerCase(path)) != 0;
    }

    ~Watch()
    {
        if (thread.joinable()) {
            ::SetEvent(stopEvent);
            thread.join();
        }
        if (stopEvent != nullptr) ::CloseHandle(stopEvent);
        if (directory != INVALID_HANDLE_VALUE) ::CloseHandle(directory);
    }
};

namespace {

    // Merges raw events into one pending change per path.
    class Pending {
    public:
        bool empty() const { return changes_.empty(); }

        void add(FileWatcher::Action action, const std::string& path, const std::string& oldPath = std::string())
        {
            using Action = FileWatcher::Action;
            auto found = changes_.find(path);
            if (found == changes_.end()) {
                changes_[path] = FileWatcher::Change{ action, path, oldPath };
                return;
            }
            FileWatcher::Change& current = found->second;
            switch (action) {
            case Action::Added:
                // removed then re-created: the content changed
                current.action = current.action == Action::Removed ? Action::Modified : Action::Added;
                break;
            case Action::Removed:
                if (current.action == Action::Added) changes_.erase(found);   // came and went
                else current.action = Action::Removed;
                break;
            case Action::Modified:
                break;   // an Added/Renamed/Modified entry already covers it
            default:
                current = FileWatcher::Change{ action, path, oldPath };
                break;
            }
        }

        FileWatcher::Changes take()
        {
            FileWatcher::Changes out;
            out.reserve(changes_.size());
            for (auto& entry : changes_) out.push_back(std::move(entry.second));
            changes_.clear();
            return out;
        }

    private:
        std::map<std::string, FileWatcher::Change> changes_;
    };

    using PathFilter = std::function<bool(const std::string&)>;

    void parse(const BYTE* buffer, std::string& pendingOld, const std::string& root,
               const PathFilter& accepts, Pending& pending)
    {
        using Action = FileWatcher::Action;
        const BYTE* cursor = buffer;
        for (;;) {
            const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(cursor);
            const std::wstring name(info->FileName, info->FileNameLength / sizeof(WCHAR));
            const std::string path = normalizePath(root + "/" + wideToUtf8(name));
            switch (info->Action) {
            case FILE_ACTION_ADDED:
                if (accepts(path)) pending.add(Action::Added, path);
                break;
            case FILE_ACTION_REMOVED:
                if (accepts(path)) pending.add(Action::Removed, path);
                break;
            case FILE_ACTION_MODIFIED:
                if (accepts(path)) pending.add(Action::Modified, path);
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                pendingOld = path;
                break;
            case FILE_ACTION_RENAMED_NEW_NAME: {
                // a rename can move a file into or out of what is accepted
                const bool newAccepted = accepts(path);
                const bool oldAccepted = !pendingOld.empty() && accepts(pendingOld);
                if (newAccepted && oldAccepted) pending.add(Action::Renamed, path, pendingOld);
                else if (newAccepted) pending.add(Action::Added, path);
                else if (oldAccepted) pending.add(Action::Removed, pendingOld);
                pendingOld.clear();
                break;
            }
            default:
                break;
            }
            if (info->NextEntryOffset == 0) break;
            cursor += info->NextEntryOffset;
        }
    }

}

std::function<void()> FileWatcher::fireOnChangedTask(const std::shared_ptr<State>& state, FileWatcher* self)
{
    return [state, self]() {
        if (!state->alive) return;
        Changes ready;
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            ready.swap(state->ready);
        }
        if (!ready.empty()) self->onChanged(*self, ready);
    };
}

FileWatcher::FileWatcher(RunLoop& loop)
    : state_(std::make_shared<State>())
    , loop_(loop)
{
}

FileWatcher::~FileWatcher()
{
    state_->alive = false;
    unwatchAll();
}

FileWatcher::WatchId FileWatcher::watch(const std::string& folder)
{
    return watch(folder, Options());
}

FileWatcher::Watch* FileWatcher::startWatch(const std::string& folder, const Options& options, bool filesOnly)
{
    auto created = std::make_unique<Watch>();
    created->root = normalizePath(folder);
    created->options = options;
    created->filesOnly = filesOnly;
    created->directory = ::CreateFileW(utf8ToWide(folder).c_str(), FILE_LIST_DIRECTORY,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                       OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (created->directory == INVALID_HANDLE_VALUE) return nullptr;
    created->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (created->stopEvent == nullptr) return nullptr;

    Watch* raw = created.get();
    std::shared_ptr<State> state = state_;
    RunLoop* loop = &loop_;
    FileWatcher* self = this;

    // The worker: reads OS notifications, coalesces them, and once things go quiet posts the batch.
    std::promise<void> listeningPromise;
    std::future<void> listening = listeningPromise.get_future();
    raw->thread = std::thread([raw, state, loop, self, listeningPromise = std::move(listeningPromise)]() mutable {
        std::vector<DWORD> buffer(kBufferWords);
        OVERLAPPED overlapped{};
        overlapped.hEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
        constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                 FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
        auto issue = [&]() {
            ::ResetEvent(overlapped.hEvent);
            return ::ReadDirectoryChangesW(raw->directory, buffer.data(), kBufferWords * sizeof(DWORD),
                                           raw->options.recursive ? TRUE : FALSE, filter, nullptr, &overlapped, nullptr) != FALSE;
        };

        using Clock = std::chrono::steady_clock;
        const std::chrono::milliseconds debounce = raw->options.debounce;
        const PathFilter accepts = [raw](const std::string& path) {
            if (raw->filesOnly && !raw->tracks(path)) return false;
            return !(raw->options.ignore && raw->options.ignore(path));
        };
        Pending pending;
        std::string pendingOld;
        Clock::time_point firstPending;
        bool pendingOverflow = false;

        auto flush = [&]() {
            FileWatcher::Changes batch = pending.take();
            if (pendingOverflow) batch.push_back(FileWatcher::Change{ Action::Overflow, raw->root, std::string() });
            pendingOverflow = false;
            if (batch.empty()) return;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->ready.insert(state->ready.end(), batch.begin(), batch.end());
            }
            // Runs on the loop's thread: hands everything ready to onChanged.
            loop->post(fireOnChangedTask(state, self));
        };

        bool reading = issue();
        listeningPromise.set_value();
        const HANDLE handles[2] = { raw->stopEvent, overlapped.hEvent };
        while (reading) {
            DWORD timeout = INFINITE;
            if (!pending.empty() || pendingOverflow) {
                const std::chrono::milliseconds waited =
                    std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - firstPending);
                const std::chrono::milliseconds left = debounce * kMaxDelayFactor - waited;
                const std::chrono::milliseconds wait = left < debounce ? left : debounce;
                timeout = wait.count() > 0 ? static_cast<DWORD>(wait.count()) : 0;
            }
            const DWORD result = ::WaitForMultipleObjects(2, handles, FALSE, timeout);
            if (result == WAIT_OBJECT_0) break;   // stop requested
            if (result == WAIT_TIMEOUT) {
                flush();
                continue;
            }
            if (result != WAIT_OBJECT_0 + 1) break;

            DWORD bytes = 0;
            if (!::GetOverlappedResult(raw->directory, &overlapped, &bytes, FALSE)) break;
            if (pending.empty() && !pendingOverflow) firstPending = Clock::now();
            if (bytes == 0) {
                pendingOverflow = true;   // the buffer overflowed and the events were dropped
            } else {
                parse(reinterpret_cast<const BYTE*>(buffer.data()), pendingOld, raw->root, accepts, pending);
            }
            reading = issue();
        }

        ::CancelIoEx(raw->directory, &overlapped);
        DWORD unused = 0;
        ::GetOverlappedResult(raw->directory, &overlapped, &unused, TRUE);
        ::CloseHandle(overlapped.hEvent);
    });
    listening.wait();   // events before the first read is posted would be lost

    watches_.push_back(std::move(created));
    return raw;
}

FileWatcher::WatchId FileWatcher::watch(const std::string& folder, const Options& options)
{
    Watch* started = startWatch(folder, options, false);
    if (started == nullptr) return kInvalidWatch;
    started->id = nextId_++;
    return started->id;
}

FileWatcher::WatchId FileWatcher::watchFile(const std::string& file)
{
    return watchFile(file, Options());
}

FileWatcher::WatchId FileWatcher::watchFile(const std::string& file, const Options& options)
{
    const std::string path = normalizePath(file);
    const std::size_t slash = path.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= path.size()) return kInvalidWatch;
    const std::string folder = slash == 0 ? "/" : path.substr(0, slash);
    const std::string key = toLowerCase(path);

    Watch* target = nullptr;
    for (const auto& existing : watches_) {
        if (existing->filesOnly && toLowerCase(existing->root) == toLowerCase(folder)) target = existing.get();
    }
    if (target == nullptr) {
        Options folderOptions = options;
        folderOptions.recursive = false;
        target = startWatch(folder, folderOptions, true);
        if (target == nullptr) return kInvalidWatch;
    }
    {
        std::lock_guard<std::mutex> lock(target->filesMutex);
        ++target->files[key];
    }
    const WatchId id = nextId_++;
    files_[id] = FileEntry{ target, key };
    return id;
}

void FileWatcher::unwatch(WatchId id)
{
    auto file = files_.find(id);
    if (file != files_.end()) {
        Watch* target = file->second.watch;
        bool empty = false;
        {
            std::lock_guard<std::mutex> lock(target->filesMutex);
            auto count = target->files.find(file->second.key);
            if (count != target->files.end() && --count->second == 0) target->files.erase(count);
            empty = target->files.empty();
        }
        files_.erase(file);
        if (empty) {   // the folder's last file: stop its worker
            for (auto it = watches_.begin(); it != watches_.end(); ++it) {
                if (it->get() == target) {
                    watches_.erase(it);
                    break;
                }
            }
        }
        return;
    }
    for (auto it = watches_.begin(); it != watches_.end(); ++it) {
        if ((*it)->id == id) {
            watches_.erase(it);   // ~Watch stops and joins the worker
            return;
        }
    }
}

void FileWatcher::unwatchAll()
{
    files_.clear();
    watches_.clear();
}

}

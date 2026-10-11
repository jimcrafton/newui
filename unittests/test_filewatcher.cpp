#include "newui/filewatcher.h"
#include "newui/runloop.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

namespace fs = std::filesystem;
using newui::FileWatcher;

namespace {

    // A temp folder, a RunLoop on its own thread, and a FileWatcher whose batches are collected.
    class FileWatcherTest : public ::testing::Test {
    protected:
        void SetUp() override
        {
            dir_ = fs::temp_directory_path() / ("newui_filewatcher_" + std::to_string(::GetCurrentProcessId()) + "_" +
                                               ::testing::UnitTest::GetInstance()->current_test_info()->name());
            fs::remove_all(dir_);
            fs::create_directories(dir_);
            root_ = dir_.generic_string();

            auto started = newui::RunLoop::runThreaded();
            loop_ = started.loop;
            loopThread_ = std::move(started.thread);
            loop_->waitForStart();
            loopThreadId_ = loopThread_.get_id();

            watcher_ = std::make_unique<FileWatcher>(*loop_);
            watcher_->onChanged.add([this](FileWatcher&, const FileWatcher::Changes& changes) {
                std::lock_guard<std::mutex> lock(mutex_);
                for (const auto& change : changes) seen_.push_back(change);
                deliveredOn_ = std::this_thread::get_id();
                cv_.notify_all();
                return newui::SyncReturn::Handled;
            });
        }

        void TearDown() override
        {
            watcher_.reset();
            loop_->quit();
            loopThread_.join();
            std::error_code ec;
            fs::remove_all(dir_, ec);
        }

        static void write(const fs::path& file, const char* text)
        {
            std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
        }

        // Waits until a change of this action and path suffix has been seen.
        bool waitFor(FileWatcher::Action action, const std::string& suffix,
                     std::chrono::milliseconds timeout = std::chrono::seconds(5))
        {
            std::unique_lock<std::mutex> lock(mutex_);
            return cv_.wait_for(lock, timeout, [&]() { return find(action, suffix) != nullptr; });
        }

        // Caller holds mutex_.
        const FileWatcher::Change* find(FileWatcher::Action action, const std::string& suffix) const
        {
            for (const auto& change : seen_) {
                if (change.action == action && change.path.size() >= suffix.size() &&
                    change.path.compare(change.path.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    return &change;
                }
            }
            return nullptr;
        }

        fs::path dir_;
        std::string root_;
        newui::RunLoop* loop_ = nullptr;
        std::thread loopThread_;
        std::thread::id loopThreadId_;
        std::unique_ptr<FileWatcher> watcher_;
        std::mutex mutex_;
        std::condition_variable cv_;
        std::vector<FileWatcher::Change> seen_;
        std::thread::id deliveredOn_;
    };

    FileWatcher::Options quick()
    {
        FileWatcher::Options options;
        options.debounce = std::chrono::milliseconds(50);
        return options;
    }

}

TEST_F(FileWatcherTest, WatchingAMissingFolderFails)
{
    EXPECT_EQ(watcher_->watch(root_ + "/nope"), FileWatcher::kInvalidWatch);
}

TEST_F(FileWatcherTest, ReportsAFileBeingAdded)
{
    ASSERT_NE(watcher_->watch(root_, quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "a.txt", "one");

    EXPECT_TRUE(waitFor(FileWatcher::Action::Added, "/a.txt"));
}

TEST_F(FileWatcherTest, DeliversOnTheLoopThread)
{
    ASSERT_NE(watcher_->watch(root_, quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "a.txt", "one");
    ASSERT_TRUE(waitFor(FileWatcher::Action::Added, "/a.txt"));

    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(deliveredOn_, loopThreadId_);
    EXPECT_NE(deliveredOn_, std::this_thread::get_id());
}

TEST_F(FileWatcherTest, ReportsAModification)
{
    write(dir_ / "a.txt", "one");
    ASSERT_NE(watcher_->watch(root_, quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "a.txt", "two, longer");

    EXPECT_TRUE(waitFor(FileWatcher::Action::Modified, "/a.txt"));
}

TEST_F(FileWatcherTest, ReportsARemoval)
{
    write(dir_ / "a.txt", "one");
    ASSERT_NE(watcher_->watch(root_, quick()), FileWatcher::kInvalidWatch);
    fs::remove(dir_ / "a.txt");

    EXPECT_TRUE(waitFor(FileWatcher::Action::Removed, "/a.txt"));
}

TEST_F(FileWatcherTest, ReportsARenameWithTheOldPath)
{
    write(dir_ / "a.txt", "one");
    ASSERT_NE(watcher_->watch(root_, quick()), FileWatcher::kInvalidWatch);
    fs::rename(dir_ / "a.txt", dir_ / "b.txt");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Renamed, "/b.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    const FileWatcher::Change* change = find(FileWatcher::Action::Renamed, "/b.txt");
    ASSERT_NE(change, nullptr);
    EXPECT_EQ(change->oldPath, root_ + "/a.txt");
}

TEST_F(FileWatcherTest, WatchesSubfoldersWhenRecursive)
{
    fs::create_directories(dir_ / "sub" / "deep");
    ASSERT_NE(watcher_->watch(root_, quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "sub" / "deep" / "x.txt", "x");

    EXPECT_TRUE(waitFor(FileWatcher::Action::Added, "/sub/deep/x.txt"));
}

TEST_F(FileWatcherTest, IgnoresSubfoldersWhenNotRecursive)
{
    fs::create_directories(dir_ / "sub");
    FileWatcher::Options options = quick();
    options.recursive = false;
    ASSERT_NE(watcher_->watch(root_, options), FileWatcher::kInvalidWatch);
    write(dir_ / "sub" / "x.txt", "x");
    write(dir_ / "top.txt", "t");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Added, "/top.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(find(FileWatcher::Action::Added, "/sub/x.txt"), nullptr);
}

TEST_F(FileWatcherTest, TheIgnoreFilterDropsChanges)
{
    fs::create_directories(dir_ / "build");
    FileWatcher::Options options = quick();
    options.ignore = [](const std::string& path) { return path.find("/build/") != std::string::npos; };
    ASSERT_NE(watcher_->watch(root_, options), FileWatcher::kInvalidWatch);
    write(dir_ / "build" / "out.obj", "o");
    write(dir_ / "kept.txt", "k");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Added, "/kept.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(find(FileWatcher::Action::Added, "/build/out.obj"), nullptr);
    EXPECT_EQ(find(FileWatcher::Action::Modified, "/build/out.obj"), nullptr);
}

TEST_F(FileWatcherTest, AFileCreatedAndDeletedWithinOneBatchReportsNothing)
{
    FileWatcher::Options options;
    options.debounce = std::chrono::milliseconds(400);
    ASSERT_NE(watcher_->watch(root_, options), FileWatcher::kInvalidWatch);
    write(dir_ / "temp.txt", "t");
    fs::remove(dir_ / "temp.txt");
    write(dir_ / "marker.txt", "m");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Added, "/marker.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(find(FileWatcher::Action::Added, "/temp.txt"), nullptr);
    EXPECT_EQ(find(FileWatcher::Action::Removed, "/temp.txt"), nullptr);
}

TEST_F(FileWatcherTest, SeveralWritesInOneBatchCoalesce)
{
    FileWatcher::Options options;
    options.debounce = std::chrono::milliseconds(400);
    ASSERT_NE(watcher_->watch(root_, options), FileWatcher::kInvalidWatch);
    for (int i = 0; i < 5; ++i) write(dir_ / "a.txt", "growing growing growing");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Added, "/a.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(seen_.size(), 1u);
}

TEST_F(FileWatcherTest, UnwatchStopsReporting)
{
    const FileWatcher::WatchId id = watcher_->watch(root_, quick());
    ASSERT_NE(id, FileWatcher::kInvalidWatch);
    watcher_->unwatch(id);
    write(dir_ / "a.txt", "one");

    EXPECT_FALSE(waitFor(FileWatcher::Action::Added, "/a.txt", std::chrono::milliseconds(500)));
}

TEST_F(FileWatcherTest, TwoWatchesAreIndependent)
{
    fs::create_directories(dir_ / "one");
    fs::create_directories(dir_ / "two");
    const FileWatcher::WatchId first = watcher_->watch(root_ + "/one", quick());
    ASSERT_NE(watcher_->watch(root_ + "/two", quick()), FileWatcher::kInvalidWatch);
    watcher_->unwatch(first);
    write(dir_ / "one" / "a.txt", "a");
    write(dir_ / "two" / "b.txt", "b");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Added, "/two/b.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(find(FileWatcher::Action::Added, "/one/a.txt"), nullptr);
}

TEST_F(FileWatcherTest, DestroyingWithAWatchActiveIsClean)
{
    ASSERT_NE(watcher_->watch(root_, quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "a.txt", "one");
    watcher_.reset();   // joins the worker; a batch it already posted must find the watcher gone
    SUCCEED();
}

TEST_F(FileWatcherTest, WatchFileReportsOnlyThatFile)
{
    write(dir_ / "tracked.txt", "one");
    write(dir_ / "other.txt", "one");
    ASSERT_NE(watcher_->watchFile(root_ + "/tracked.txt", quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "other.txt", "changed other");
    write(dir_ / "tracked.txt", "changed tracked");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Modified, "/tracked.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(find(FileWatcher::Action::Modified, "/other.txt"), nullptr);
}

TEST_F(FileWatcherTest, OneWatcherFollowsFilesInSeveralFolders)
{
    fs::create_directories(dir_ / "a");
    fs::create_directories(dir_ / "b");
    write(dir_ / "a" / "one.txt", "1");
    write(dir_ / "a" / "two.txt", "2");
    write(dir_ / "b" / "three.txt", "3");
    ASSERT_NE(watcher_->watchFile(root_ + "/a/one.txt", quick()), FileWatcher::kInvalidWatch);
    ASSERT_NE(watcher_->watchFile(root_ + "/a/two.txt", quick()), FileWatcher::kInvalidWatch);
    ASSERT_NE(watcher_->watchFile(root_ + "/b/three.txt", quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "a" / "one.txt", "11");
    write(dir_ / "a" / "two.txt", "22");
    write(dir_ / "b" / "three.txt", "33");

    EXPECT_TRUE(waitFor(FileWatcher::Action::Modified, "/a/one.txt"));
    EXPECT_TRUE(waitFor(FileWatcher::Action::Modified, "/a/two.txt"));
    EXPECT_TRUE(waitFor(FileWatcher::Action::Modified, "/b/three.txt"));
}

TEST_F(FileWatcherTest, WatchFileMatchesPathsCaseInsensitively)
{
    write(dir_ / "Mixed.TXT", "one");
    ASSERT_NE(watcher_->watchFile(root_ + "/mixed.txt", quick()), FileWatcher::kInvalidWatch);
    write(dir_ / "Mixed.TXT", "changed");

    EXPECT_TRUE(waitFor(FileWatcher::Action::Modified, "/Mixed.TXT"));
}

TEST_F(FileWatcherTest, UnwatchingOneFileKeepsTheOthers)
{
    write(dir_ / "one.txt", "1");
    write(dir_ / "two.txt", "2");
    const FileWatcher::WatchId first = watcher_->watchFile(root_ + "/one.txt", quick());
    ASSERT_NE(watcher_->watchFile(root_ + "/two.txt", quick()), FileWatcher::kInvalidWatch);
    watcher_->unwatch(first);
    write(dir_ / "one.txt", "11");
    write(dir_ / "two.txt", "22");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Modified, "/two.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(find(FileWatcher::Action::Modified, "/one.txt"), nullptr);
}

TEST_F(FileWatcherTest, UnwatchingTheLastFileStopsReporting)
{
    write(dir_ / "one.txt", "1");
    const FileWatcher::WatchId id = watcher_->watchFile(root_ + "/one.txt", quick());
    watcher_->unwatch(id);
    write(dir_ / "one.txt", "11");

    EXPECT_FALSE(waitFor(FileWatcher::Action::Modified, "/one.txt", std::chrono::milliseconds(500)));
}

TEST_F(FileWatcherTest, WatchingTheSameFileTwiceNeedsBothUnwatched)
{
    write(dir_ / "one.txt", "1");
    const FileWatcher::WatchId first = watcher_->watchFile(root_ + "/one.txt", quick());
    ASSERT_NE(watcher_->watchFile(root_ + "/one.txt", quick()), FileWatcher::kInvalidWatch);
    watcher_->unwatch(first);
    write(dir_ / "one.txt", "11");

    EXPECT_TRUE(waitFor(FileWatcher::Action::Modified, "/one.txt"));
}

TEST_F(FileWatcherTest, WatchFileReportsTheFileBeingRemovedAndRecreated)
{
    write(dir_ / "one.txt", "1");
    ASSERT_NE(watcher_->watchFile(root_ + "/one.txt", quick()), FileWatcher::kInvalidWatch);
    fs::remove(dir_ / "one.txt");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Removed, "/one.txt"));
    write(dir_ / "one.txt", "back");
    EXPECT_TRUE(waitFor(FileWatcher::Action::Added, "/one.txt"));
}

TEST_F(FileWatcherTest, WatchFileReportsARenameAwayAsARemoval)
{
    write(dir_ / "one.txt", "1");
    ASSERT_NE(watcher_->watchFile(root_ + "/one.txt", quick()), FileWatcher::kInvalidWatch);
    fs::rename(dir_ / "one.txt", dir_ / "elsewhere.txt");

    ASSERT_TRUE(waitFor(FileWatcher::Action::Removed, "/one.txt"));
    std::lock_guard<std::mutex> lock(mutex_);
    EXPECT_EQ(find(FileWatcher::Action::Added, "/elsewhere.txt"), nullptr);
}

TEST_F(FileWatcherTest, WatchFileInAMissingFolderFails)
{
    EXPECT_EQ(watcher_->watchFile(root_ + "/nope/one.txt"), FileWatcher::kInvalidWatch);
}

#include "newui/delegate.h"
#include "newui/runloop.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace {

std::atomic<int> g_lastValue{0};
std::atomic<int> g_secondCallCount{0};
std::thread::id g_callbackThreadId;

newui::SyncReturn RecordOnLoop(int& value) {
    g_lastValue = value;
    g_callbackThreadId = std::this_thread::get_id();
    return newui::SyncReturn::Handled;
}

newui::SyncReturn FailOnLoop(int&) {
    return newui::SyncReturn::Error;
}

newui::SyncReturn CountOnLoop(int&) {
    ++g_secondCallCount;
    return newui::SyncReturn::Handled;
}

}  // namespace

TEST(RunLoopPostCall, RunsOnLoopThreadAndCompletesAsyncReturn) {
    g_lastValue = 0;
    g_callbackThreadId = std::thread::id();

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();
    std::thread::id loopThreadId = loopThread.get_id();

    newui::Delegate<int> delegate;
    delegate.add(&RecordOnLoop);

    int sender = 42;
    newui::AsyncReturn asyncReturn = delegate.postCall(runLoop, &sender);

    ASSERT_TRUE(asyncReturn.waitFor(std::chrono::seconds(5)));
    EXPECT_TRUE(asyncReturn.result().handled());
    EXPECT_EQ(g_lastValue, 42);
    EXPECT_EQ(g_callbackThreadId, loopThreadId);
    EXPECT_NE(g_callbackThreadId, std::this_thread::get_id());

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostCall, StopsAtFirstError) {
    g_secondCallCount = 0;

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    newui::Delegate<int> delegate;
    delegate.add(&FailOnLoop);
    delegate.add(&CountOnLoop);

    int sender = 1;
    newui::AsyncReturn asyncReturn = delegate.postCall(runLoop, &sender);
    asyncReturn.wait();

    EXPECT_TRUE(asyncReturn.result().error());
    EXPECT_EQ(g_secondCallCount, 0);

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostIdle, RunsQueuedIdleTaskOnLoopThread) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();
    std::thread::id loopThreadId = loopThread.get_id();

    std::atomic<bool> ran{false};
    std::thread::id ranOnThreadId;
    std::mutex doneMutex;
    std::condition_variable doneCv;

    runLoop.postIdle([&]() {
        ranOnThreadId = std::this_thread::get_id();
        {
            std::lock_guard<std::mutex> lock(doneMutex);
            ran = true;
        }
        doneCv.notify_all();
        return true;  // done after one call
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5), [&] { return ran.load(); }));
    }

    EXPECT_EQ(ranOnThreadId, loopThreadId);
    EXPECT_NE(ranOnThreadId, std::this_thread::get_id());

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostIdle, RunsMultipleQueuedIdleTasksInOrder) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    std::vector<int> order;

    for (int i = 0; i < 5; ++i) {
        runLoop.postIdle([&, i]() {
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                order.push_back(i);
            }
            doneCv.notify_all();
            return true;  // each is done after one call
            });
    }

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5),
            [&] { return order.size() == 5; }));
    }

    EXPECT_EQ(order, (std::vector<int>{0, 1, 2, 3, 4}));

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostIdle, TaskIsRequeuedUntilItReturnsTrue) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    int callCount = 0;
    constexpr int kCallsUntilDone = 3;

    runLoop.postIdle([&]() {
        std::lock_guard<std::mutex> lock(doneMutex);
        ++callCount;
        bool complete = callCount >= kCallsUntilDone;
        if (complete) {
            doneCv.notify_all();
        }
        return complete;
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5),
            [&] { return callCount >= kCallsUntilDone; }));
    }

    // Give the loop a little more idle time to prove a completed task
    // really was removed rather than happening to reach kCallsUntilDone on
    // its way to being called again regardless.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    {
        std::lock_guard<std::mutex> lock(doneMutex);
        EXPECT_EQ(callCount, kCallsUntilDone);
    }

    runLoop.quit();
    loopThread.join();
}

// runModal()'s actual message-pumping/owner-disable behavior needs a live
// window and real user interaction to exercise meaningfully (same
// reasoning ContextMenu::show()/Dialog::showModal() are excluded from
// unit tests - see test_menus.cpp/test_dialogs.cpp) - these tests only
// cover the "must be called from the loop's own thread, after run() has
// started" guard, which is safely testable without either.

TEST(RunLoopRunModal, ThrowsIfCalledBeforeRun) {
    newui::RunLoop runLoop;

    EXPECT_THROW(runLoop.runModal(nullptr, nullptr, [] { return true; }), std::runtime_error);
}

TEST(RunLoopRunModal, ThrowsIfCalledFromDifferentThreadThanRun) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    // Called from this test's own thread, not loopThread - runModal()
    // should refuse rather than silently pump some other thread's queue.
    EXPECT_THROW(runLoop.runModal(nullptr, nullptr, [] { return true; }), std::runtime_error);

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopRunModal, ReturnsTrueImmediatelyWhenIsDoneIsAlreadyTrue) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();
    std::thread::id loopThreadId = loopThread.get_id();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool ran = false;
    bool sawResult = false;
    std::thread::id ranOnThreadId;

    // isDone() already true on the very first check, so runModal() never
    // has to actually pump a message - safe to run for real, on the
    // loop's own thread, from a posted task.
    runLoop.post([&]() {
        ranOnThreadId = std::this_thread::get_id();
        sawResult = runLoop.runModal(nullptr, nullptr, [] { return true; });
        {
            std::lock_guard<std::mutex> lock(doneMutex);
            ran = true;
        }
        doneCv.notify_all();
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5), [&] { return ran; }));
    }

    EXPECT_EQ(ranOnThreadId, loopThreadId);
    EXPECT_TRUE(sawResult);

    runLoop.quit();
    loopThread.join();
}

// The Escape check inside runModal() has to walk up to the owning
// top-level window (::GetAncestor(..., GA_ROOT)) rather than compare
// msg.hwnd directly against modalHandle - keyboard focus (and therefore
// WM_KEYDOWN's msg.hwnd) can sit on a child window instead of modalHandle
// itself, e.g. a Dialog's RootView grabs focus on its own (see
// RootView::initialize()'s SetFocus() call), never the top-level frame.
// This creates a real (never-shown) top-level window plus a real
// WS_CHILD window to stand in for that relationship, without needing an
// actual newui::Frame/Dialog or any real user input - same "exercise
// real Win32 objects headlessly" precedent as TestableContextMenu in
// test_menus.cpp.
TEST(RunLoopRunModal, EscapeOnFocusedChildWindowIsRecognizedAsModalHandles) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool ran = false;
    bool endingFired = false;
    bool sawResult = false;

    runLoop.onModalEnding.add([&](newui::RunLoop&, bool& canClose) {
        endingFired = true;
        canClose = true;
        return newui::SyncReturn::Handled;
        });

    runLoop.post([&]() {
        // Windows are thread-affine - topLevel/child have to be created
        // (and PostMessage'd to, and destroyed) from right here, on
        // loopThread itself, or they'd belong to some other thread's
        // queue and runModal()'s GetMessage() on this thread would never
        // see messages posted to them at all.
        HINSTANCE moduleHandle = ::GetModuleHandleA(nullptr);
        HWND topLevel = ::CreateWindowExA(0, "STATIC", "", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, moduleHandle, nullptr);
        HWND child = ::CreateWindowExA(0, "STATIC", "", WS_CHILD,
            0, 0, 0, 0, topLevel, nullptr, moduleHandle, nullptr);

        // Posted (not sent) so it's sitting in the queue for runModal()'s
        // own GetMessage to actually retrieve, the same way a real
        // Escape keypress would arrive - targeting child (standing in
        // for whichever control currently has focus), not topLevel.
        ::PostMessage(child, WM_KEYDOWN, VK_ESCAPE, 0);

        sawResult = runLoop.runModal(topLevel, nullptr, [&]() { return endingFired; });

        ::DestroyWindow(child);
        ::DestroyWindow(topLevel);

        {
            std::lock_guard<std::mutex> lock(doneMutex);
            ran = true;
        }
        doneCv.notify_all();
        });

    // If the ancestor walk regresses back to a direct msg.hwnd ==
    // modalHandle comparison, Escape on child never gets recognized,
    // onModalEnding never fires, isDone() never becomes true, and
    // runModal() just sits blocked in GetMessage() - this waits with a
    // timeout (EXPECT_TRUE, not ASSERT_TRUE) so that case reports a
    // failure instead of hanging: quit()/join() below still have to run
    // either way, since an early return here would leave loopThread
    // joinable at scope exit, and a joinable std::thread's destructor
    // calls std::terminate() - crashing the whole test binary, not just
    // failing this one test.
    bool completedInTime;
    {
        std::unique_lock<std::mutex> lock(doneMutex);
        completedInTime = doneCv.wait_for(lock, std::chrono::seconds(5), [&] { return ran; });
    }
    EXPECT_TRUE(completedInTime);

    // quit() unblocks runModal()'s GetMessage() even if it's still stuck
    // waiting (completedInTime == false) - WM_QUIT gets picked up the
    // same way any other message would, ending the modal loop (and then
    // run() itself, and the posted task's own cleanup) regardless of
    // whether isDone() ever agreed.
    runLoop.quit();
    loopThread.join();

    EXPECT_TRUE(endingFired);
    EXPECT_TRUE(sawResult);
}

TEST(RunLoopPostIdle, MultipleUnfinishedTasksInterleaveRoundRobin) {
    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    std::vector<int> callOrder;
    constexpr int kCallsUntilDone = 3;

    // Two tasks, each needing kCallsUntilDone calls to finish. If they're
    // round-robined rather than one being run to completion before the
    // other starts, the call order interleaves: 0, 1, 0, 1, 0, 1.
    for (int taskId = 0; taskId < 2; ++taskId) {
        auto callsRemaining = std::make_shared<int>(kCallsUntilDone);
        runLoop.postIdle([&, taskId, callsRemaining]() {
            std::lock_guard<std::mutex> lock(doneMutex);
            callOrder.push_back(taskId);
            --(*callsRemaining);
            bool complete = *callsRemaining <= 0;
            if (callOrder.size() == 2 * kCallsUntilDone) {
                doneCv.notify_all();
            }
            return complete;
            });
    }

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5),
            [&] { return callOrder.size() == 2 * kCallsUntilDone; }));
    }

    EXPECT_EQ(callOrder, (std::vector<int>{0, 1, 0, 1, 0, 1}));

    runLoop.quit();
    loopThread.join();
}

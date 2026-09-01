#include "newui/action.h"
#include "newui/delegate.h"
#include "newui/keyboard_constants.h"
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

    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();
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

    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

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
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();
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
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

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
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

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
    // current() on this (the test's own) thread, never run() here -
    // matches the "before run()" scenario this test wants without
    // needing a second thread.
    newui::RunLoop& runLoop = newui::RunLoop::current();

    EXPECT_THROW(runLoop.runModal(nullptr, nullptr, [] { return true; }), std::runtime_error);
}

TEST(RunLoopRunModal, ThrowsIfCalledFromDifferentThreadThanRun) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    // Called from this test's own thread, not loopThread - runModal()
    // should refuse rather than silently pump some other thread's queue.
    EXPECT_THROW(runLoop.runModal(nullptr, nullptr, [] { return true; }), std::runtime_error);

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopRunModal, ReturnsTrueImmediatelyWhenIsDoneIsAlreadyTrue) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();
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
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

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

// postDelayed()/cancelDelayed() share runModal()'s "must be called from
// the loop's own thread, after run() has started" guard (see
// checkCalledFromLoopThread(), runloop.cpp) - covered the same way those
// tests cover runModal()'s guard, above.

TEST(RunLoopPostDelayed, ThrowsIfCalledBeforeRun) {
    // current() on this (the test's own) thread, never run() here -
    // matches the "before run()" scenario this test wants without
    // needing a second thread.
    newui::RunLoop& runLoop = newui::RunLoop::current();

    EXPECT_THROW(runLoop.postDelayed(std::chrono::milliseconds(10), [] { return true; }), std::runtime_error);
}

TEST(RunLoopPostDelayed, ThrowsIfCalledFromDifferentThreadThanRun) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    // Called from this test's own thread, not loopThread - a Win32 timer
    // is thread-affine, so postDelayed() should refuse here the same way
    // runModal() does, rather than silently create a timer that can never
    // fire on the right thread.
    EXPECT_THROW(runLoop.postDelayed(std::chrono::milliseconds(10), [] { return true; }), std::runtime_error);

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostDelayed, CancelDelayedThrowsIfCalledFromDifferentThreadThanRun) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    EXPECT_THROW(runLoop.cancelDelayed(1), std::runtime_error);

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostDelayed, RunsOnLoopThreadAfterInterval) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();
    std::thread::id loopThreadId = loopThread.get_id();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool ran = false;
    std::thread::id ranOnThreadId;

    // postDelayed() itself must run on the loop thread (see the guard
    // tests above) - post() hops onto it first, same as every other test
    // below that calls postDelayed()/cancelDelayed() for real.
    runLoop.post([&]() {
        runLoop.postDelayed(std::chrono::milliseconds(20), [&]() {
            ranOnThreadId = std::this_thread::get_id();
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                ran = true;
            }
            doneCv.notify_all();
            return true;  // done after one call
            });
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5), [&] { return ran; }));
    }

    EXPECT_EQ(ranOnThreadId, loopThreadId);
    EXPECT_NE(ranOnThreadId, std::this_thread::get_id());

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostDelayed, TaskIsRescheduledUntilItReturnsTrue) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    int callCount = 0;
    constexpr int kCallsUntilDone = 3;

    runLoop.post([&]() {
        runLoop.postDelayed(std::chrono::milliseconds(15), [&]() {
            std::lock_guard<std::mutex> lock(doneMutex);
            ++callCount;
            bool complete = callCount >= kCallsUntilDone;
            if (complete) {
                doneCv.notify_all();
            }
            return complete;
            });
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5),
            [&] { return callCount >= kCallsUntilDone; }));
    }

    // Give the timer a little more time to prove a finished task really
    // was killed rather than happening to reach kCallsUntilDone on its way
    // to firing again regardless (same shape as the postIdle equivalent
    // above).
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    {
        std::lock_guard<std::mutex> lock(doneMutex);
        EXPECT_EQ(callCount, kCallsUntilDone);
    }

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostDelayed, CancelDelayedStopsATimerBeforeItsFirstTick) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    std::mutex mutex;
    int callCount = 0;

    runLoop.post([&]() {
        newui::RunLoop::TimerHandle handle = runLoop.postDelayed(std::chrono::milliseconds(15), [&]() {
            std::lock_guard<std::mutex> lock(mutex);
            ++callCount;
            return false;  // would keep repeating if not cancelled
            });

        // Cancelled immediately, still on the loop thread, before it's had
        // any chance to tick even once.
        runLoop.cancelDelayed(handle);
        });

    // Several intervals' worth of time - if cancellation didn't actually
    // take, callCount would be > 0 by now.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(callCount, 0);
    }

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostDelayed, CancelDelayedStopsAnAlreadyTickingTimer) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    std::mutex mutex;
    std::condition_variable cv;
    int callCount = 0;
    newui::RunLoop::TimerHandle handle = newui::RunLoop::kInvalidTimerHandle;

    runLoop.post([&]() {
        handle = runLoop.postDelayed(std::chrono::milliseconds(15), [&]() {
            std::lock_guard<std::mutex> lock(mutex);
            ++callCount;
            cv.notify_all();
            return false;  // keeps repeating unless cancelled
            });
        });

    // Let it tick at least once for real before cancelling.
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return callCount >= 1; }));
    }

    int callCountAtCancel;
    runLoop.post([&]() {
        std::lock_guard<std::mutex> lock(mutex);
        callCountAtCancel = callCount;
        runLoop.cancelDelayed(handle);
        });

    // Several more intervals' worth of time - if cancellation didn't
    // actually take, callCount would keep climbing well past this.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    {
        std::lock_guard<std::mutex> lock(mutex);
        // Allow one extra tick that may have already been queued (posted
        // by SetTimer's own periodic re-arm) before the cancelling post()
        // task actually ran.
        EXPECT_LE(callCount, callCountAtCancel + 1);
    }

    runLoop.quit();
    loopThread.join();
}

// RunLoop::current() - lets code reach "whichever RunLoop is actively
// pumping this thread right now" without going through
// Application::instance().runLoop(), so RootView/Caret/ScrollBar/Stepper/
// ViewController auto-repeat and idle-task scheduling work identically
// whether the running loop is Application's own owned instance or a
// standalone one a caller constructs and runs itself (no Application/
// Frame involved at all).

TEST(RunLoopCurrent, NotRunningWhenNoLoopIsRunningOnThisThread) {
    // current() is now always a valid reference (a thread-local singleton,
    // auto-created on first access) rather than a nullable pointer - the
    // meaningful check is whether it's actually running, via operator
    // bool()/isRunning(), not identity against nullptr.
    EXPECT_FALSE(newui::RunLoop::current());
}

TEST(RunLoopCurrent, ResolvesToTheInstanceActuallyRunningOnThatThread) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool ran = false;
    newui::RunLoop* seenFromLoopThread = nullptr;

    runLoop.post([&]() {
        seenFromLoopThread = &newui::RunLoop::current();
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

    EXPECT_EQ(seenFromLoopThread, &runLoop);
    // This (the calling/test) thread's own current() is a distinct
    // instance, never run() here, regardless of what's running on
    // loopThread - thread-local, not process-global.
    EXPECT_NE(&newui::RunLoop::current(), &runLoop);
    EXPECT_FALSE(newui::RunLoop::current());

    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopCurrent, NotRunningOnThisThreadAfterAnotherThreadsLoopReturns) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    runLoop.quit();
    loopThread.join();

    // current() is thread-local to loopThread, which has already exited -
    // this checks the test thread's own instance never got started by a
    // loop running on a different thread (same "thread-local, not
    // process-global" property as above, from the other direction).
    EXPECT_FALSE(newui::RunLoop::current());
}

// Regression coverage for the RunLoop::run() WM_KEYDOWN/WM_SYSKEYDOWN case
// unconditionally dereferencing Application::instance().getFrame() (now
// removed - that call was dead code, its result never actually used) -
// this is what made hotkey matching, and therefore any standalone RunLoop
// with no Application/Frame at all, crash on the very first keystroke.
// No newui::Application/Frame is touched anywhere in this test.
TEST(RunLoopRegisterAction, MatchesHotkeyOnRealKeydownWithNoApplicationOrFrame) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    newui::Action action("test action");
    action.setHotkey(newui::vkF1);
    runLoop.registerAction(&action);

    std::atomic<int> performCount{0};
    action.onActionPerformed.add([&](newui::Action&) {
        ++performCount;
        return newui::SyncReturn::Handled;
        });

    std::mutex doneMutex;
    std::condition_variable doneCv;
    bool ran = false;

    runLoop.post([&]() {
        // Real, plain (non-newui) window - a synthetic WM_KEYDOWN has to
        // target a window actually owned by loopThread, same reasoning as
        // RunLoopRunModal's own window-creation tests above.
        HINSTANCE moduleHandle = ::GetModuleHandleA(nullptr);
        HWND hwnd = ::CreateWindowExA(0, "STATIC", "", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, moduleHandle, nullptr);

        ::PostMessage(hwnd, WM_KEYDOWN, VK_F1, 0);

        // A postIdle task only runs once run()'s own do-while loop has
        // fully drained the message queue (see RunLoop::run()'s idle
        // loop) - the queued WM_KEYDOWN above, posted first, is
        // guaranteed to already have been retrieved and dispatched by
        // then. Sleeping here instead would block this same loop thread
        // from ever pumping GetMessage() to retrieve it in the first
        // place - postIdle is the correct way to sequence after it.
        runLoop.postIdle([&, hwnd]() {
            ::DestroyWindow(hwnd);
            {
                std::lock_guard<std::mutex> lock(doneMutex);
                ran = true;
            }
            doneCv.notify_all();
            return true;  // one-shot
            });
        });

    {
        std::unique_lock<std::mutex> lock(doneMutex);
        ASSERT_TRUE(doneCv.wait_for(lock, std::chrono::seconds(5), [&] { return ran; }));
    }

    EXPECT_EQ(performCount.load(), 1);

    runLoop.unregisterAction(&action);
    runLoop.quit();
    loopThread.join();
}

TEST(RunLoopPostIdle, MultipleUnfinishedTasksInterleaveRoundRobin) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

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

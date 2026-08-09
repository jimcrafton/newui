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

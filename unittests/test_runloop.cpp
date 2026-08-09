#include "newui/delegate.h"
#include "newui/runloop.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

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

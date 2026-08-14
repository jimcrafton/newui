#include "newui/delegate.h"

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <type_traits>
#include <vector>

namespace {

std::atomic<int> g_callCount{0};
std::atomic<int> g_lastValue{0};
std::atomic<int> g_lastSenderId{0};

// In these single-template-argument delegates, int itself plays the role of
// Sender (passed by reference, per Delegate's SenderRefT) and there are no
// additional Args.
newui::SyncReturn RecordCall(int& value) {
    ++g_callCount;
    g_lastValue = value;
    return newui::SyncReturn::Handled;
}

newui::SyncReturn IncrementCount(int&) {
    ++g_callCount;
    return newui::SyncReturn::Handled;
}

newui::SyncReturn IgnoreCall(int&) {
    ++g_callCount;
    return newui::SyncReturn::Ignored;
}

newui::SyncReturn FailCall(int&) {
    ++g_callCount;
    return newui::SyncReturn::Error;
}

// A stand-in for the instance that would call syncCall (e.g. a View or
// Frame), used to exercise Delegate<Sender, Args...> where Sender is
// distinct from the trailing Args.
struct FakeSender {
    int id = 0;
};

newui::SyncReturn RecordSenderAndValue(FakeSender& sender, int value) {
    ++g_callCount;
    g_lastSenderId = sender.id;
    g_lastValue = value;
    return newui::SyncReturn::Handled;
}

void ResetRecorder() {
    g_callCount = 0;
    g_lastValue = 0;
    g_lastSenderId = 0;
}

// Exercises Delegate::add(instance, &T::Method), which binds a real
// (non-static) member function rather than requiring a hand-written static
// trampoline.
class Listener {
public:
    newui::SyncReturn record(FakeSender& sender, int value) {
        ++callCount;
        lastSenderId = sender.id;
        lastValue = value;
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn recordConst(FakeSender& sender, int value) const {
        ++callCount;
        lastSenderId = sender.id;
        lastValue = value;
        return newui::SyncReturn::Handled;
    }

    mutable int callCount = 0;
    mutable int lastSenderId = 0;
    mutable int lastValue = 0;
};

}  // namespace

TEST(Delegate, SyncCallWithNoFunctionsDoesNothing) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    int sender = 42;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 0);
}

TEST(Delegate, SyncCallInvokesAddedFunction) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&RecordCall);
    int sender = 7;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 1);
    EXPECT_EQ(g_lastValue, 7);
}

TEST(Delegate, SyncCallInvokesAllAddedFunctionsInOrder) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&IncrementCount);
    delegate.add(&IncrementCount);
    delegate.add(&RecordCall);
    int sender = 9;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 3);
    EXPECT_EQ(g_lastValue, 9);
}

TEST(Delegate, SyncCallStopsAtFirstError) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&FailCall);
    delegate.add(&IncrementCount);
    int sender = 1;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 1);
}

TEST(Delegate, AddIgnoresNullFunctionPointer) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(nullptr);
    int sender = 1;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 0);
}

TEST(Delegate, RemoveStopsFurtherCalls) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    newui::Connection connection = delegate.add(&IncrementCount);
    delegate.remove(connection);
    int sender = 1;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 0);
}

TEST(Delegate, RemoveOnDefaultConnectionIsNoOp) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&IncrementCount);
    delegate.remove(newui::Connection());
    int sender = 1;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 1);
}

TEST(Delegate, SyncCallFirstReturnsIgnoredWhenNoneHandle) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&IgnoreCall);
    delegate.add(&IgnoreCall);
    int sender = 1;
    newui::SyncReturn result = delegate.syncCallFirst(sender);
    EXPECT_TRUE(result.ignored());
    EXPECT_EQ(g_callCount, 2);
}

TEST(Delegate, SyncCallFirstStopsAtFirstHandled) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&IgnoreCall);
    delegate.add(&RecordCall);
    delegate.add(&IncrementCount);
    int sender = 5;
    newui::SyncReturn result = delegate.syncCallFirst(sender);
    EXPECT_TRUE(result.handled());
    EXPECT_EQ(g_callCount, 2);
    EXPECT_EQ(g_lastValue, 5);
}

TEST(Delegate, ConcurrentAddFromMultipleThreadsRetainsAllFunctions) {
    ResetRecorder();
    newui::Delegate<int> delegate;

    constexpr int kThreadCount = 8;
    std::vector<std::thread> threads;
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&delegate]() {
            delegate.add(&IncrementCount);
        });
    }
    for (auto& t : threads) {
        t.join();
    }

    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, kThreadCount);
}

TEST(Delegate, SyncCallPassesSenderAndArgsToFunction) {
    ResetRecorder();
    newui::Delegate<FakeSender, int> delegate;
    delegate.add(&RecordSenderAndValue);
    FakeSender sender{5};
    delegate.syncCall(sender, 7);
    EXPECT_EQ(g_callCount, 1);
    EXPECT_EQ(g_lastSenderId, 5);
    EXPECT_EQ(g_lastValue, 7);
}

TEST(Delegate, OperatorCallPassesSenderAndArgsToFunction) {
    ResetRecorder();
    newui::Delegate<FakeSender, int> delegate;
    delegate.add(&RecordSenderAndValue);
    FakeSender sender{3};
    delegate(sender, 11);
    EXPECT_EQ(g_callCount, 1);
    EXPECT_EQ(g_lastSenderId, 3);
    EXPECT_EQ(g_lastValue, 11);
}

TEST(Delegate, SyncCallFirstPassesSenderAndArgsToFunction) {
    ResetRecorder();
    newui::Delegate<FakeSender, int> delegate;
    delegate.add(&RecordSenderAndValue);
    FakeSender sender{9};
    newui::SyncReturn result = delegate.syncCallFirst(sender, 13);
    EXPECT_TRUE(result.handled());
    EXPECT_EQ(g_lastSenderId, 9);
    EXPECT_EQ(g_lastValue, 13);
}

TEST(Delegate, FunctionPtrTakesSenderByReferenceAsFirstParameter) {
    static_assert(std::is_same<newui::Delegate<FakeSender, int>::FunctionPtr,
                                newui::SyncReturn (*)(FakeSender&, int)>::value,
                  "FunctionPtr's first parameter must be a reference to Sender");
}

// Delegate<FakeSender&, ...> (a reference as the Sender template argument)
// is intentionally a compile error - see the static_assert in delegate.h.
// There's no standard way to assert that here without a compile-fail test
// harness, so this is left as documentation.

TEST(Delegate, SyncCallInvokesBoundMemberFunction) {
    newui::Delegate<FakeSender, int> delegate;
    Listener listener;
    delegate.add(&listener, &Listener::record);

    FakeSender sender{4};
    delegate.syncCall(sender, 21);

    EXPECT_EQ(listener.callCount, 1);
    EXPECT_EQ(listener.lastSenderId, 4);
    EXPECT_EQ(listener.lastValue, 21);
}

TEST(Delegate, SyncCallInvokesBoundConstMemberFunction) {
    newui::Delegate<FakeSender, int> delegate;
    const Listener listener;
    delegate.add(&listener, &Listener::recordConst);

    FakeSender sender{6};
    delegate.syncCall(sender, 8);

    EXPECT_EQ(listener.callCount, 1);
    EXPECT_EQ(listener.lastSenderId, 6);
    EXPECT_EQ(listener.lastValue, 8);
}

TEST(Delegate, RemoveMemberFunctionStopsFurtherCalls) {
    newui::Delegate<FakeSender, int> delegate;
    Listener listener;
    newui::Connection connection = delegate.add(&listener, &Listener::record);
    delegate.remove(connection);

    FakeSender sender{1};
    delegate.syncCall(sender, 1);

    EXPECT_EQ(listener.callCount, 0);
}

TEST(Delegate, BoundMemberFunctionsOnDifferentInstancesAreIndependent) {
    newui::Delegate<FakeSender, int> delegate;
    Listener a;
    Listener b;
    newui::Connection connectionA = delegate.add(&a, &Listener::record);
    delegate.add(&b, &Listener::record);

    delegate.remove(connectionA);

    FakeSender sender{2};
    delegate.syncCall(sender, 5);

    EXPECT_EQ(a.callCount, 0);
    EXPECT_EQ(b.callCount, 1);
}

TEST(Delegate, SyncCallInvokesCapturingLambda) {
    newui::Delegate<FakeSender, int> delegate;
    int captured = 0;
    delegate.add([&captured](FakeSender&, int value) {
        captured = value;
        return newui::SyncReturn::Handled;
    });

    FakeSender sender{1};
    delegate.syncCall(sender, 99);

    EXPECT_EQ(captured, 99);
}

TEST(Delegate, ConcurrentSyncCallDuringAddDoesNotCrash) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&IncrementCount);

    std::atomic<bool> stop{false};
    int sender = 1;
    std::thread caller([&]() {
        while (!stop) {
            delegate.syncCall(sender);
        }
    });

    for (int i = 0; i < 100; ++i) {
        delegate.add(&IncrementCount);
    }

    stop = true;
    caller.join();

    SUCCEED();
}

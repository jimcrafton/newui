#include "newui/delegate.h"

#include <gtest/gtest.h>

#include <atomic>
#include <memory>
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

// ---------------------------------------------------------------------
// Named add() overloads / describedListeners() - the descriptor-tracking
// added for newui::reflection's "delegates" JSON block (reflectionio.h):
// a connection made through one of these is otherwise functionally
// identical to its plain (unnamed) counterpart above - same dispatch,
// same removal via Connection - the descriptor is pure metadata,
// consulted only by describedListeners() itself.
// ---------------------------------------------------------------------

TEST(Delegate, UnnamedAddIsNotDescribed) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&IncrementCount);

    EXPECT_TRUE(delegate.describedListeners().empty());
}

TEST(Delegate, NamedAddOfFunctionPtrIsDescribed) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add("IncrementCount", &IncrementCount);

    EXPECT_EQ(delegate.describedListeners(), std::vector<std::string>{"IncrementCount"});
}

TEST(Delegate, NamedAddOfFunctionPtrStillCallsIt) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add("RecordCall", &RecordCall);
    int sender = 4;
    delegate.syncCall(sender);

    EXPECT_EQ(g_callCount, 1);
    EXPECT_EQ(g_lastValue, 4);
}

TEST(Delegate, NamedAddOfMemberFunctionIsDescribed) {
    newui::Delegate<FakeSender, int> delegate;
    Listener listener;
    delegate.add("listener@Listener.record", &listener, &Listener::record);

    EXPECT_EQ(delegate.describedListeners(), std::vector<std::string>{"listener@Listener.record"});
}

TEST(Delegate, NamedAddOfConstMemberFunctionIsDescribedAndStillCalled) {
    const Listener listener;
    newui::Delegate<FakeSender, int> delegate;
    delegate.add("listener@Listener.recordConst", &listener, &Listener::recordConst);
    FakeSender sender{6};
    delegate.syncCall(sender, 21);

    EXPECT_EQ(delegate.describedListeners(), std::vector<std::string>{"listener@Listener.recordConst"});
    EXPECT_EQ(listener.callCount, 1);
    EXPECT_EQ(listener.lastSenderId, 6);
    EXPECT_EQ(listener.lastValue, 21);
}

TEST(Delegate, DescribedListenersOmitsUndescribedConnectionsAmongDescribedOnes) {
    newui::Delegate<int> delegate;
    delegate.add(&IncrementCount);              // undescribed
    delegate.add("RecordCall", &RecordCall);    // described

    EXPECT_EQ(delegate.describedListeners(), std::vector<std::string>{"RecordCall"});
}

TEST(Delegate, RemovingADescribedListenerDropsItFromDescribedListeners) {
    newui::Delegate<int> delegate;
    newui::Connection connection = delegate.add("IncrementCount", &IncrementCount);
    delegate.remove(connection);

    EXPECT_TRUE(delegate.describedListeners().empty());
}

TEST(Connections, DestructorRemovesEveryListener) {
    ResetRecorder();
    newui::Delegate<int> first;
    newui::Delegate<int> second;
    {
        newui::Connections connections;
        connections.add(first, [](int&) { ++g_callCount; return newui::SyncReturn::Handled; });
        connections.add(first, [](int&) { ++g_callCount; return newui::SyncReturn::Handled; });
        connections.add(second, [](int&) { ++g_callCount; return newui::SyncReturn::Handled; });
        EXPECT_EQ(connections.size(), 3u);
    }
    int sender = 0;
    first.syncCall(sender);
    second.syncCall(sender);

    EXPECT_EQ(g_callCount, 0);
    EXPECT_TRUE(first.empty());
    EXPECT_TRUE(second.empty());
}

TEST(Connections, ListenersStayConnectedWhileTheOwnerLives) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    newui::Connections connections;
    connections.add(delegate, [](int&) { ++g_callCount; return newui::SyncReturn::Handled; });
    int sender = 0;
    delegate.syncCall(sender);

    EXPECT_EQ(g_callCount, 1);
}

TEST(Connections, ClearLeavesListenersOfOtherOwnersAlone) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    delegate.add(&IncrementCount);
    newui::Connections connections;
    connections.add(delegate, [](int&) { g_callCount += 10; return newui::SyncReturn::Handled; });
    connections.clear();
    int sender = 0;
    delegate.syncCall(sender);

    EXPECT_EQ(connections.size(), 0u);
    EXPECT_EQ(g_callCount, 1);
}

TEST(Connections, MovedFromOwnerNoLongerRemoves) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    newui::Connections moved;
    {
        newui::Connections original;
        original.add(delegate, [](int&) { ++g_callCount; return newui::SyncReturn::Handled; });
        moved = std::move(original);
        EXPECT_EQ(original.size(), 0u);
    }
    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 1);

    moved.clear();
    delegate.syncCall(sender);
    EXPECT_EQ(g_callCount, 1);
}

namespace {

newui::Delegate<int>::Callback countInto(int& count) {
    return [&count](int&) {
        ++count;
        return newui::SyncReturn::Handled;
    };
}

}  // namespace

TEST(Connections, CanBeDestroyedAfterItsDelegate) {
    int count = 0;
    auto delegate = std::make_unique<newui::Delegate<int>>();
    {
        newui::Connections connections;
        connections.add(*delegate, countInto(count));
        delegate.reset();
    }
    EXPECT_EQ(count, 0);
}

TEST(Connections, SizeAndClearStillWorkOnceTheDelegatesAreGone) {
    int count = 0;
    newui::Connections connections;
    {
        newui::Delegate<int> delegate;
        connections.add(delegate, countInto(count));
    }
    EXPECT_EQ(connections.size(), 1u);
    connections.clear();
    EXPECT_EQ(connections.size(), 0u);
}

TEST(Connection, DisconnectRemovesOnlyItsOwnListener) {
    int first = 0;
    int second = 0;
    newui::Delegate<int> delegate;
    newui::Connection connection = delegate.add(countInto(first));
    delegate.add(countInto(second));

    connection.disconnect();
    EXPECT_FALSE(connection.connected());
    connection.disconnect();

    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

TEST(Connection, DisconnectAfterTheDelegateIsGoneDoesNothing) {
    int count = 0;
    newui::Connection connection;
    {
        newui::Delegate<int> delegate;
        connection = delegate.add(countInto(count));
    }
    EXPECT_TRUE(connection.connected()) << "it still names a listener";
    connection.disconnect();
    EXPECT_FALSE(connection.connected());
}

TEST(Connection, DisconnectOnADefaultConnectionDoesNothing) {
    newui::Connection connection;
    connection.disconnect();
    EXPECT_FALSE(connection.connected());
}

TEST(Connection, DestroyingTheDelegateReleasesItsListeners) {
    auto token = std::make_shared<int>(0);
    newui::Connection connection;
    {
        newui::Delegate<int> delegate;
        connection = delegate.add([token](int&) { return newui::SyncReturn::Handled; });
        EXPECT_EQ(token.use_count(), 2);
    }
    EXPECT_EQ(token.use_count(), 1) << "an outstanding Connection does not keep the listener alive";
}

TEST(ScopedConnection, DisconnectsWhenDestroyed) {
    int count = 0;
    newui::Delegate<int> delegate;
    {
        newui::ScopedConnection connection = delegate.add(countInto(count));
        EXPECT_TRUE(connection.connected());
        int sender = 0;
        delegate.syncCall(sender);
    }
    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(delegate.empty());
}

TEST(ScopedConnection, AssigningOverItDisconnectsThePreviousListener) {
    int first = 0;
    int second = 0;
    newui::Delegate<int> delegate;
    newui::ScopedConnection connection = delegate.add(countInto(first));
    connection = delegate.add(countInto(second));

    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 1);
}

TEST(ScopedConnection, MovingHandsTheListenerOver) {
    int count = 0;
    newui::Delegate<int> delegate;
    newui::ScopedConnection outer;
    {
        newui::ScopedConnection inner = delegate.add(countInto(count));
        outer = std::move(inner);
        EXPECT_FALSE(inner.connected());
        newui::ScopedConnection constructed = std::move(outer);
        outer = std::move(constructed);
    }
    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(count, 1);
    EXPECT_TRUE(outer.connected());
}

TEST(ScopedConnection, CanBeDestroyedAfterItsDelegate) {
    int count = 0;
    newui::ScopedConnection connection;
    {
        newui::Delegate<int> delegate;
        connection = delegate.add(countInto(count));
    }
    connection.disconnect();
    EXPECT_FALSE(connection.connected());
}

TEST(Delegate, AListenerRemovedByAnEarlierOneIsNotCalledInThatPass) {
    int removed = 0;
    int after = 0;
    newui::Delegate<int> delegate;
    newui::Connection victim;
    delegate.add([&](int&) {
        victim.disconnect();
        return newui::SyncReturn::Handled;
    });
    victim = delegate.add(countInto(removed));
    delegate.add(countInto(after));

    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(removed, 0);
    EXPECT_EQ(after, 1);
}

TEST(Delegate, AListenerRemovedByAnEarlierOneIsNotCalledBySyncCallFirst) {
    int removed = 0;
    newui::Delegate<int> delegate;
    newui::Connection victim;
    delegate.add([&](int&) {
        delegate.remove(victim);
        return newui::SyncReturn::Ignored;
    });
    victim = delegate.add(countInto(removed));

    int sender = 0;
    EXPECT_TRUE(delegate.syncCallFirst(sender).ignored());
    EXPECT_EQ(removed, 0);
}

TEST(Delegate, AListenerRemovedByDescriptorDuringACallIsNotCalled) {
    int removed = 0;
    newui::Delegate<int> delegate;
    delegate.add([&](int&) {
        delegate.removeDescribed("victim");
        return newui::SyncReturn::Handled;
    });
    delegate.add("victim", countInto(removed));

    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(removed, 0);
}

TEST(Delegate, AListenerAddedDuringACallWaitsForTheNextOne) {
    int added = 0;
    newui::Delegate<int> delegate;
    bool done = false;
    delegate.add([&](int&) {
        if (!done) {
            done = true;
            delegate.add(countInto(added));
        }
        return newui::SyncReturn::Handled;
    });

    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(added, 0);
    delegate.syncCall(sender);
    EXPECT_EQ(added, 1);
}

TEST(Delegate, CopiesHaveIndependentListenerLists) {
    int shared = 0;
    int onlyOriginal = 0;
    int onlyCopy = 0;
    newui::Delegate<int> original;
    newui::Connection connection = original.add(countInto(shared));
    newui::Delegate<int> copy = original;
    original.add(countInto(onlyOriginal));
    copy.add(countInto(onlyCopy));

    const newui::Connection handle = connection;
    connection.disconnect();
    int sender = 0;
    original.syncCall(sender);
    EXPECT_EQ(shared, 0);
    EXPECT_EQ(onlyOriginal, 1);
    EXPECT_EQ(onlyCopy, 0);

    copy.syncCall(sender);
    EXPECT_EQ(shared, 1) << "disconnect() acts on the delegate the listener was added to, not on copies";
    EXPECT_EQ(onlyCopy, 1);

    copy.remove(handle);
    copy.syncCall(sender);
    EXPECT_EQ(shared, 1) << "remove() on the copy takes it out of the copy";
}

TEST(Delegate, CopyAssignmentKeepsTheTargetsConnectionsUsable) {
    int count = 0;
    newui::Delegate<int> source;
    newui::Connection fromSource = source.add(countInto(count));
    newui::Delegate<int> target;
    target = source;
    target = *&target;

    int sender = 0;
    target.syncCall(sender);
    EXPECT_EQ(count, 1);

    target.remove(fromSource);
    target.syncCall(sender);
    EXPECT_EQ(count, 1);
    source.syncCall(sender);
    EXPECT_EQ(count, 2);
}

TEST(Delegate, MovingTakesTheListenersAndTheirConnections) {
    int count = 0;
    newui::Delegate<int> source;
    newui::Connection connection = source.add(countInto(count));
    newui::Delegate<int> moved = std::move(source);

    int sender = 0;
    EXPECT_TRUE(source.empty());
    source.syncCall(sender);
    EXPECT_EQ(count, 0);
    moved.syncCall(sender);
    EXPECT_EQ(count, 1);

    newui::Delegate<int> assigned;
    assigned = std::move(moved);
    connection.disconnect();
    assigned.syncCall(sender);
    EXPECT_EQ(count, 1);

    source.add(countInto(count));
    source.syncCall(sender);
    EXPECT_EQ(count, 2) << "a moved-from delegate is still usable";
}

TEST(Delegate, AddAndRemoveFromTwoThreadsWhileAThirdCalls) {
    newui::Delegate<int> delegate;
    std::atomic<int> permanentCalls{0};
    delegate.add([&permanentCalls](int&) {
        ++permanentCalls;
        return newui::SyncReturn::Handled;
    });

    std::atomic<bool> stop{false};
    std::thread caller([&]() {
        int sender = 0;
        while (!stop) {
            delegate.syncCall(sender);
        }
    });
    auto churn = [&delegate]() {
        for (int i = 0; i < 2000; ++i) {
            newui::ScopedConnection connection = delegate.add([](int&) { return newui::SyncReturn::Handled; });
        }
    };
    std::thread first(churn);
    std::thread second(churn);
    first.join();
    second.join();
    stop = true;
    caller.join();

    const int before = permanentCalls;
    int sender = 0;
    delegate.syncCall(sender);
    EXPECT_EQ(permanentCalls, before + 1);
}

TEST(Connections, ListenerCanBeRemovedWhileItIsRunning) {
    ResetRecorder();
    newui::Delegate<int> delegate;
    newui::Connections connections;
    connections.add(delegate, [&connections](int&) {
        ++g_callCount;
        connections.clear();
        return newui::SyncReturn::Handled;
    });
    int sender = 0;
    delegate.syncCall(sender);
    delegate.syncCall(sender);

    EXPECT_EQ(g_callCount, 1);
}

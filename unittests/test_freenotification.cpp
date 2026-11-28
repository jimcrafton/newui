// Free notification (freeNotification / notification / onFreeing) and Callback<R(Args...)>.

#include "newui/callback.h"
#include "newui/component.h"
#include "newui/componentref.h"
#include "newui/runloop.h"
#include "newui/subview.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

namespace {

using newui::Callback;
using newui::Component;
using newui::ComponentRef;

std::vector<std::string> g_told;

// Notes every notification it receives as "<this> <added|lost> <other>".
class Watcher : public Component {
public:
    explicit Watcher(const std::string& name) { setName(name); }

    std::function<void(Component&)> onLost;
    std::function<void()> onBeforeDestruction;

protected:
    void notification(Component& component, Operation operation) override {
        g_told.push_back(name() + (operation == Operation::Inserted ? " added " : " lost ") + component.name());
        if (operation == Operation::Removing && onLost) {
            onLost(component);
        }
    }

    void beforeDestruction() override {
        if (onBeforeDestruction) {
            onBeforeDestruction();
        }
    }
};

class FreeNotification : public ::testing::Test {
protected:
    void SetUp() override { g_told.clear(); }
};

std::vector<std::string> told() { return g_told; }

}  // namespace

TEST_F(FreeNotification, TheWatcherIsToldWhileTheOtherIsStillWhole) {
    Watcher* watcher = new Watcher("watcher");
    Watcher* target = new Watcher("target");
    ComponentRef<Watcher> reference(target);
    watcher->freeNotification(*target);
    bool whole = false;
    watcher->onLost = [&](Component& gone) {
        whole = &gone == target && reference.get() == target && target->name() == "target";
    };

    target->free();

    EXPECT_TRUE(whole);
    EXPECT_FALSE(reference);
    EXPECT_EQ(told(), (std::vector<std::string>{"watcher lost target"}));
    watcher->free();
}

TEST_F(FreeNotification, MutualNotificationWorksWhicheverGoesFirst) {
    for (const bool firstGoesFirst : {true, false}) {
        g_told.clear();
        Watcher* a = new Watcher("a");
        Watcher* b = new Watcher("b");
        a->freeNotification(*b);
        b->freeNotification(*a);
        Watcher* going = firstGoesFirst ? a : b;
        Watcher* staying = firstGoesFirst ? b : a;
        const std::string expected = staying->name() + " lost " + going->name();

        going->free();
        EXPECT_EQ(told(), (std::vector<std::string>{expected}));
        staying->free();
        EXPECT_EQ(told(), (std::vector<std::string>{expected})) << "the one already gone is not told, nor touched";
    }
}

TEST_F(FreeNotification, AWatcherThatGoesFirstIsForgotten) {
    Watcher* watcher = new Watcher("watcher");
    Watcher* target = new Watcher("target");
    watcher->freeNotification(*target);
    watcher->free();
    target->free();
    EXPECT_TRUE(told().empty());
}

TEST_F(FreeNotification, AskingTwiceAboutItselfOrAboutOneOnItsWayOutDoesNothing) {
    Watcher* watcher = new Watcher("watcher");
    Watcher* target = new Watcher("target");
    watcher->freeNotification(*target);
    watcher->freeNotification(*target);
    watcher->freeNotification(*watcher);
    target->onBeforeDestruction = [&]() {
        Watcher late("late");
        late.freeNotification(*target);
    };

    target->free();

    EXPECT_EQ(told(), (std::vector<std::string>{"watcher lost target"}));
    watcher->free();
}

TEST_F(FreeNotification, EveryWatcherIsToldInTheOrderTheyAsked) {
    Watcher* target = new Watcher("target");
    Watcher* a = new Watcher("a");
    Watcher* b = new Watcher("b");
    a->freeNotification(*target);
    b->freeNotification(*target);

    target->free();

    EXPECT_EQ(told(), (std::vector<std::string>{"a lost target", "b lost target"}));
    a->free();
    b->free();
}

TEST_F(FreeNotification, AHandlerMayFreeAnotherWatcher) {
    Watcher* target = new Watcher("target");
    Watcher* a = new Watcher("a");
    Watcher* b = new Watcher("b");
    ComponentRef<Watcher> second(b);
    a->freeNotification(*target);
    b->freeNotification(*target);
    a->onLost = [&](Component&) { b->free(); };

    target->free();

    EXPECT_FALSE(second);
    EXPECT_EQ(told(), (std::vector<std::string>{"a lost target"}));
    a->free();
}

TEST_F(FreeNotification, AHandlerMayFreeItsOwnWatcher) {
    Watcher* target = new Watcher("target");
    Watcher* a = new Watcher("a");
    ComponentRef<Watcher> reference(a);
    a->freeNotification(*target);
    a->onLost = [a](Component&) { a->free(); };

    target->free();

    EXPECT_FALSE(reference);
}

TEST_F(FreeNotification, AnOwnerThatAlsoAskedIsToldOnce) {
    Watcher* owner = new Watcher("owner");
    Watcher* child = owner->createOwned<Watcher>("child");
    owner->freeNotification(*child);
    int removed = 0;
    owner->onComponentRemoved().add([&removed](Component&, Component&) {
        ++removed;
        return newui::SyncReturn::Handled;
    });
    g_told.clear();

    child->free();

    EXPECT_EQ(told(), (std::vector<std::string>{"owner lost child"}));
    EXPECT_EQ(removed, 1);
    owner->free();
}

TEST_F(FreeNotification, ASiblingStillWholeIsToldButTheOwnerFreeingThemIsNot) {
    Watcher* owner = new Watcher("owner");
    Watcher* first = owner->createOwned<Watcher>("first");
    Watcher* second = owner->createOwned<Watcher>("second");
    first->freeNotification(*second);
    owner->freeNotification(*second);
    g_told.clear();

    owner->free();

    EXPECT_EQ(told(), (std::vector<std::string>{"first lost second"})) << "it drops its pointer before its own turn comes";
}

TEST_F(FreeNotification, DeletingAnUnownedComponentStillTellsItsWatchers) {
    Watcher* watcher = new Watcher("watcher");
    {
        Watcher target("target");
        watcher->freeNotification(target);
    }
    EXPECT_EQ(told(), (std::vector<std::string>{"watcher lost target"}));
    watcher->free();
}

TEST_F(FreeNotification, DestroyingAViewTellsItsWatchersOnce) {
    Watcher* watcher = new Watcher("watcher");
    auto* view = new newui::SubView();
    view->setName("view");
    watcher->freeNotification(*view);
    int fired = 0;
    view->onFreeing().add([&fired](Component&) {
        ++fired;
        return newui::SyncReturn::Handled;
    });

    view->destroy();
    delete view;

    EXPECT_EQ(told(), (std::vector<std::string>{"watcher lost view"}));
    EXPECT_EQ(fired, 1);
    watcher->free();
}

TEST_F(FreeNotification, OnFreeingFiresAfterTheWatchersForNonComponentListeners) {
    Watcher* watcher = new Watcher("watcher");
    Watcher* target = new Watcher("target");
    watcher->freeNotification(*target);
    target->onFreeing().add([](Component& gone) {
        g_told.push_back("event " + gone.name());
        return newui::SyncReturn::Handled;
    });
    target->onBeforeDestruction = []() { g_told.push_back("before target"); };

    target->free();

    EXPECT_EQ(told(), (std::vector<std::string>{"watcher lost target", "event target", "before target"}));
    watcher->free();
}

TEST_F(FreeNotification, ReleasingAViewThatWasAlreadyDestroyedStillFreesIt) {
    struct Counted : newui::SubView {
        explicit Counted(int& deleted) : deleted_(deleted) {}
        ~Counted() override { ++deleted_; }
        int& deleted_;
    };
    auto loop = newui::RunLoop::runThreaded();
    loop.loop->waitForStart();
    int deleted = 0;
    auto run = [&](std::function<void()> task) {
        std::promise<void> done;
        loop.loop->post([&]() {
            task();
            done.set_value();
        });
        ASSERT_EQ(done.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);
    };

    run([&]() {
        auto* view = new Counted(deleted);
        view->destroy();
        view->release();
    });
    run([&]() { EXPECT_EQ(deleted, 1); });

    loop.loop->quit();
    loop.thread.join();
}

// ---- Callback ----

TEST(Callback, StartsUnsetAndReturnsTheFallback) {
    Callback<int(int)> callback;
    EXPECT_FALSE(callback);
    EXPECT_EQ(callback.call(-1, 5), -1);
}

TEST(Callback, CallsTheListenerWhileItsOwnerLives) {
    Component* owner = new Component();
    Callback<int(int, int)> callback;
    callback.set(*owner, [](int a, int b) { return a + b; });

    ASSERT_TRUE(callback);
    EXPECT_EQ(callback(2, 3), 5);
    EXPECT_EQ(callback.call(-1, 4, 5), 9);
    owner->free();
}

TEST(Callback, IsUnsetOnceItsOwnerIsFreed) {
    Component* owner = new Component();
    int calls = 0;
    Callback<std::string(const std::string&)> callback;
    callback.set(*owner, [&calls](const std::string& text) {
        ++calls;
        return text + "!";
    });

    owner->free();

    EXPECT_FALSE(callback);
    EXPECT_EQ(callback.call("none", "hello"), "none");
    EXPECT_EQ(calls, 0);
}

TEST(Callback, IsUnsetAlreadyWhileItsOwnerIsBeingFreed) {
    class Owner : public Component {
    public:
        Callback<int()> callback;
        bool unset = false;
        bool* result = nullptr;

    protected:
        void beforeDestruction() override { *result = !callback && callback.call(7) == 7; }
    };
    bool unsetDuringFree = false;
    Owner* owner = new Owner();
    owner->result = &unsetDuringFree;
    owner->callback.set(*owner, []() { return 1; });

    owner->free();

    EXPECT_TRUE(unsetDuringFree);
}

TEST(Callback, SettingAgainReplacesTheListenerAndTheOwner) {
    Component* first = new Component();
    Component* second = new Component();
    Callback<int()> callback;
    callback.set(*first, []() { return 1; });
    callback.set(*second, []() { return 2; });

    first->free();
    ASSERT_TRUE(callback) << "the first owner no longer matters";
    EXPECT_EQ(callback(), 2);
    second->free();
    EXPECT_FALSE(callback);
}

TEST(Callback, ClearAndAnEmptyFunctionLeaveItUnset) {
    Component* owner = new Component();
    Callback<int()> callback;
    callback.set(*owner, []() { return 1; });
    callback.clear();
    EXPECT_FALSE(callback);
    callback.set(*owner, []() { return 1; });
    callback.set(*owner, std::function<int()>());
    EXPECT_FALSE(callback);
    owner->free();
}

TEST(Callback, MayClearItselfWhileRunning) {
    Component* owner = new Component();
    Callback<std::string()> callback;
    const std::string kept(100, 'x');
    callback.set(*owner, [&callback, kept]() {
        callback.clear();
        return kept;
    });

    EXPECT_EQ(callback(), kept);
    EXPECT_FALSE(callback);
    owner->free();
}

TEST(Callback, CanReturnVoidAndPassReferencesThrough) {
    Component* owner = new Component();
    Callback<void(int&)> callback;
    callback.set(*owner, [](int& value) { value += 1; });
    int value = 1;
    callback(value);
    EXPECT_EQ(value, 2);
    owner->free();
}

TEST(Callback, CanTakeAMoveOnlyArgument) {
    Component* owner = new Component();
    Callback<int(std::unique_ptr<int>)> callback;
    callback.set(*owner, [](std::unique_ptr<int> value) { return *value; });
    EXPECT_EQ(callback(std::make_unique<int>(4)), 4);
    EXPECT_EQ(callback.call(0, std::make_unique<int>(5)), 5);
    owner->free();
}

TEST(Callback, CallingAnUnsetOneIsRefused) {
    Callback<int()> callback;
    EXPECT_DEBUG_DEATH(callback(), "");
#ifdef NDEBUG
    EXPECT_EQ(callback(), 0);
#endif
}

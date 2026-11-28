#include "newui/component.h"
#include "newui/componentref.h"
#include "newui/runloop.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

using newui::Component;
using newui::ComponentRef;

// Records what happens to it in a shared log, as "<event> <name>".
class Probe : public Component {
public:
    Probe(std::vector<std::string>& log, const std::string& name) : log_(log) { setName(name); }
    ~Probe() override { log_.push_back("deleted " + name()); }

    std::function<void(Probe&)> onBeforeDestruction;
    using Component::connect;
    using Component::connections;

protected:
    void beforeDestruction() override {
        log_.push_back("before " + name());
        if (onBeforeDestruction) {
            onBeforeDestruction(*this);
        }
    }

    void notification(Component& component, Operation operation) override {
        log_.push_back((operation == Operation::Inserted ? "inserted " : "removing ") + component.name());
    }

private:
    std::vector<std::string>& log_;
};

std::vector<std::string> only(const std::vector<std::string>& log, const std::string& event) {
    std::vector<std::string> result;
    for (const std::string& entry : log) {
        if (entry.rfind(event + " ", 0) == 0) {
            result.push_back(entry.substr(event.size() + 1));
        }
    }
    return result;
}

std::vector<std::string> names(const Component& component) {
    std::vector<std::string> result;
    for (const Component* child : component.components()) {
        result.push_back(child->name());
    }
    return result;
}

class ComponentOwnership : public ::testing::Test {
protected:
    Probe* make(const std::string& name) { return new Probe(log_, name); }

    std::vector<std::string> log_;
};

}  // namespace

TEST_F(ComponentOwnership, APlainComponentOwnsNothingAndHasNoOwner) {
    Probe plain(log_, "plain");
    EXPECT_EQ(plain.owner(), nullptr);
    EXPECT_EQ(plain.componentCount(), 0u);
    EXPECT_EQ(plain.findComponent("x"), nullptr);
}

TEST_F(ComponentOwnership, AddOwnsInOrderAndAddingAgainKeepsThePlace) {
    Probe* root = make("root");
    Probe* a = make("a");
    Probe* b = make("b");
    root->addComponent(a);
    root->addComponent(b);
    root->addComponent(a);

    EXPECT_EQ(names(*root), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(a->owner(), root);
    EXPECT_EQ(only(log_, "inserted"), (std::vector<std::string>{"a", "b"}));
    root->free();
}

TEST_F(ComponentOwnership, AddingToAnotherOwnerMovesItAndGoesToTheEnd) {
    Probe* first = make("first");
    Probe* second = make("second");
    Probe* child = first->createOwned<Probe>(log_, "child");
    second->createOwned<Probe>(log_, "other");

    second->addComponent(child);
    EXPECT_EQ(first->componentCount(), 0u);
    EXPECT_EQ(names(*second), (std::vector<std::string>{"other", "child"}));
    EXPECT_EQ(child->owner(), second);

    first->addComponent(child);
    second->addComponent(child);
    EXPECT_EQ(names(*second), (std::vector<std::string>{"other", "child"}));
    first->free();
    second->free();
}

TEST_F(ComponentOwnership, RemoveHandsOwnershipBackWithoutFreeing) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    root->removeComponent(child);

    EXPECT_EQ(child->owner(), nullptr);
    EXPECT_EQ(root->componentCount(), 0u);
    EXPECT_FALSE(child->isDestroying());
    root->free();
    EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"root"}));
    delete child;
}

TEST_F(ComponentOwnership, RemovingAComponentItDoesNotOwnDoesNothing) {
    Probe* root = make("root");
    Probe* other = make("other");
    Probe* child = other->createOwned<Probe>(log_, "child");
    root->removeComponent(child);
    root->removeComponent(nullptr);
    EXPECT_EQ(child->owner(), other);
    root->free();
    other->free();
}

TEST_F(ComponentOwnership, SetOwnerAddsAndNullRemoves) {
    Probe* root = make("root");
    Probe* child = make("child");
    child->setOwner(root);
    EXPECT_EQ(child->owner(), root);
    child->setOwner(nullptr);
    EXPECT_EQ(child->owner(), nullptr);
    child->setOwner(nullptr);
    child->free();
    root->free();
}

TEST_F(ComponentOwnership, FindComponentLooksOnlyAtDirectlyOwnedOnes) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    child->createOwned<Probe>(log_, "grandchild");
    Probe* twin = root->createOwned<Probe>(log_, "child");

    EXPECT_EQ(root->findComponent("child"), child);
    EXPECT_NE(root->findComponent("child"), twin);
    EXPECT_EQ(root->findComponent("grandchild"), nullptr);
    root->free();
}

TEST_F(ComponentOwnership, AddedAndRemovedDelegatesFire) {
    Probe* root = make("root");
    std::vector<std::string> events;
    root->onComponentAdded().add([&events](Component&, Component& child) {
        events.push_back("added " + child.name());
        return newui::SyncReturn::Handled;
    });
    root->onComponentRemoved().add([&events](Component&, Component& child) {
        events.push_back("removed " + child.name());
        return newui::SyncReturn::Handled;
    });

    Probe* a = root->createOwned<Probe>(log_, "a");
    Probe* b = root->createOwned<Probe>(log_, "b");
    root->removeComponent(a);
    b->free();

    EXPECT_EQ(events, (std::vector<std::string>{"added a", "added b", "removed a", "removed b"}));
    EXPECT_EQ(root->componentCount(), 0u);
    a->free();
    root->free();
}

TEST_F(ComponentOwnership, FreeFreesOwnedComponentsLastAddedFirst) {
    Probe* root = make("root");
    Probe* a = root->createOwned<Probe>(log_, "a");
    a->createOwned<Probe>(log_, "a1");
    a->createOwned<Probe>(log_, "a2");
    root->createOwned<Probe>(log_, "b");

    root->free();

    EXPECT_EQ(only(log_, "before"), (std::vector<std::string>{"root", "b", "a", "a2", "a1"}));
    EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"b", "a2", "a1", "a", "root"}));
}

TEST_F(ComponentOwnership, EverythingIsDestroyingBeforeAnyIsFreedAndOwnersAreStillIntact) {
    Probe* root = make("root");
    Probe* a = root->createOwned<Probe>(log_, "a");
    Probe* grandchild = a->createOwned<Probe>(log_, "grandchild");
    Probe* b = root->createOwned<Probe>(log_, "b");

    int checked = 0;
    root->onBeforeDestruction = [&](Probe&) {
        EXPECT_TRUE(a->isDestroying());
        EXPECT_TRUE(b->isDestroying());
        EXPECT_TRUE(grandchild->isDestroying());
        EXPECT_EQ(root->componentCount(), 2u);
        ++checked;
    };
    grandchild->onBeforeDestruction = [&](Probe& self) {
        EXPECT_EQ(self.owner(), a);
        EXPECT_EQ(a->owner(), root);
        EXPECT_EQ(a->name(), "a");
        ++checked;
    };

    root->free();
    EXPECT_EQ(checked, 2);
}

TEST_F(ComponentOwnership, FreeingAnOwnedComponentLeavesItsOwner) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    child->free();
    EXPECT_EQ(root->componentCount(), 0u);
    EXPECT_EQ(only(log_, "removing"), (std::vector<std::string>{"child"}));
    root->free();
}

TEST_F(ComponentOwnership, ASiblingFreedFromBeforeDestructionIsFreedOnce) {
    Probe* root = make("root");
    Probe* a = root->createOwned<Probe>(log_, "a");
    Probe* b = root->createOwned<Probe>(log_, "b");
    b->onBeforeDestruction = [a](Probe&) { a->free(); };

    root->free();
    EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"a", "b", "root"}));
}

TEST_F(ComponentOwnership, FreeingTheOwnerFromAChildsBeforeDestructionFreesEachOnce) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    child->onBeforeDestruction = [root](Probe&) { root->free(); };

    root->free();
    EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"child", "root"}));
}

TEST_F(ComponentOwnership, AChildFreedOnItsOwnCanFreeItsOwnerOnTheWay) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    child->onBeforeDestruction = [root](Probe&) { root->free(); };

    child->free();
    EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"root", "child"}));
}

TEST_F(ComponentOwnership, ComponentRefBecomesNullWhenFreed) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    ComponentRef<Probe> reference(child);
    ComponentRef<Probe> empty;
    EXPECT_FALSE(empty);
    ASSERT_TRUE(reference);
    EXPECT_EQ(reference->name(), "child");
    EXPECT_EQ(reference.get(), child);

    root->free();
    EXPECT_FALSE(reference);
    EXPECT_EQ(reference.get(), nullptr);
}

TEST_F(ComponentOwnership, ComponentRefBecomesNullWhenAnUnownedComponentIsDeleted) {
    auto plain = std::make_unique<Probe>(log_, "plain");
    ComponentRef<Probe> reference;
    reference = plain.get();
    ASSERT_TRUE(reference);
    plain.reset();
    EXPECT_FALSE(reference);
}

TEST_F(ComponentOwnership, LifetimeAndRefsAreExpiredByTheTimeBeforeDestructionRuns) {
    Probe* target = make("target");
    ComponentRef<Probe> reference(target);
    std::weak_ptr<const void> life = target->lifetime();
    bool ran = false;
    target->onBeforeDestruction = [&](Probe& self) {
        EXPECT_TRUE(life.expired());
        EXPECT_FALSE(reference);
        EXPECT_TRUE(self.lifetime().expired()) << "asked for again while being freed";
        ComponentRef<Probe> late(&self);
        EXPECT_FALSE(late);
        ran = true;
    };
    target->free();
    EXPECT_TRUE(ran);
}

TEST_F(ComponentOwnership, ConnectionsAreGoneBeforeBeforeDestruction) {
    newui::Delegate<int> delegate;
    int calls = 0;
    Probe* listener = make("listener");
    listener->connect(delegate, [&calls](int&) {
        ++calls;
        return newui::SyncReturn::Handled;
    });
    int sender = 0;
    delegate.syncCall(sender);
    listener->onBeforeDestruction = [&](Probe&) { delegate.syncCall(sender); };

    listener->free();
    delegate.syncCall(sender);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(delegate.empty());
}

TEST_F(ComponentOwnership, ConnectionsSurviveTheDelegateGoingFirst) {
    Probe* listener = make("listener");
    {
        newui::Delegate<int> delegate;
        listener->connect(delegate, [](int&) { return newui::SyncReturn::Handled; });
    }
    listener->free();
    EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"listener"}));
}

TEST_F(ComponentOwnership, DeletingAnUnownedComponentDisconnectsIt) {
    newui::Delegate<int> delegate;
    {
        Probe listener(log_, "listener");
        listener.connect(delegate, [](int&) { return newui::SyncReturn::Handled; });
        EXPECT_FALSE(delegate.empty());
    }
    EXPECT_TRUE(delegate.empty());
}

TEST_F(ComponentOwnership, CopiesShareNoOwnership) {
    class Plain : public Component {};
    Plain* original = new Plain();
    original->setName("original");
    original->createOwned<Plain>();
    const std::weak_ptr<const void> life = original->lifetime();

    Plain copy(*original);
    EXPECT_EQ(copy.name(), "original");
    EXPECT_EQ(copy.componentCount(), 0u);
    EXPECT_EQ(copy.owner(), nullptr);
    Plain assigned;
    assigned = *original;
    EXPECT_EQ(assigned.componentCount(), 0u);
    original->free();
    EXPECT_TRUE(life.expired());
    EXPECT_FALSE(copy.lifetime().expired());
}

// ---- Mistakes: they stop a debug build and are ignored in a release build. ----

class ComponentOwnershipMisuse : public ComponentOwnership {};

TEST_F(ComponentOwnershipMisuse, OwningNothingItselfOrAnOwnerIsRefused) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    Probe* grandchild = child->createOwned<Probe>(log_, "grandchild");

    EXPECT_DEBUG_DEATH(root->addComponent(nullptr), "");
    EXPECT_DEBUG_DEATH(root->addComponent(root), "");
    EXPECT_DEBUG_DEATH(grandchild->addComponent(root), "");

    EXPECT_EQ(root->owner(), nullptr);
    EXPECT_EQ(grandchild->componentCount(), 0u);
    root->free();
}

TEST_F(ComponentOwnershipMisuse, AddingOrRemovingDuringAFreeIsRefused) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    Probe* outsider = make("outsider");
    root->onBeforeDestruction = [&](Probe& self) {
        EXPECT_DEBUG_DEATH(self.addComponent(outsider), "");
        EXPECT_DEBUG_DEATH(outsider->addComponent(&self), "");
        EXPECT_DEBUG_DEATH(self.removeComponent(child), "");
#ifdef NDEBUG
        EXPECT_EQ(self.createOwned<Probe>(log_, "late"), nullptr);
#endif
    };
    root->free();
    EXPECT_EQ(outsider->owner(), nullptr);
    EXPECT_EQ(outsider->componentCount(), 0u);
    outsider->free();
}

TEST_F(ComponentOwnershipMisuse, DeletingAnOwnedComponentIsRefusedAndStillLeavesTheOwnerSound) {
    Probe* root = make("root");
    Probe* child = root->createOwned<Probe>(log_, "child");
    EXPECT_DEBUG_DEATH(delete child, "");
#ifdef NDEBUG
    EXPECT_EQ(root->componentCount(), 0u);
#else
    root->removeComponent(child);
    delete child;
#endif
    root->free();
}

TEST_F(ComponentOwnershipMisuse, UsingOwnershipFromAnotherThreadIsRefused) {
    Probe* root = make("root");
    root->createOwned<Probe>(log_, "child");
    Probe* outsider = make("outsider");
    outsider->createOwned<Probe>(log_, "mine");

    EXPECT_DEBUG_DEATH(std::thread([&]() { root->addComponent(outsider); }).join(), "");
    EXPECT_EQ(outsider->owner(), nullptr);
    root->free();
    outsider->free();
}

// ---- release() ----

namespace {

class ComponentRelease : public ComponentOwnership {
protected:
    void SetUp() override {
        thread_ = newui::RunLoop::runThreaded();
        thread_.loop->waitForStart();
    }

    void TearDown() override {
        thread_.loop->quit();
        thread_.thread.join();
    }

    // Runs task on the loop thread and waits for it: ownership belongs to that thread here.
    void onLoop(std::function<void()> task) {
        std::promise<void> done;
        thread_.loop->post([&]() {
            task();
            done.set_value();
        });
        ASSERT_EQ(done.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);
    }

    newui::RunLoop::RunLoopThread thread_;
};

}  // namespace

TEST_F(ComponentRelease, FreesOnTheNextTurnAndMarksDestroyingAtOnce) {
    ComponentRef<Probe> reference;
    onLoop([&]() {
        Probe* root = make("root");
        Probe* child = root->createOwned<Probe>(log_, "child");
        reference = root;
        root->release();
        EXPECT_TRUE(root->isDestroying());
        EXPECT_TRUE(child->isDestroying());
        EXPECT_TRUE(reference) << "still alive until the loop turns";
        EXPECT_TRUE(only(log_, "deleted").empty());
    });
    onLoop([&]() {
        EXPECT_FALSE(reference);
        EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"child", "root"}));
    });
}

TEST_F(ComponentRelease, TwiceFreesOnce) {
    onLoop([&]() {
        Probe* target = make("target");
        target->release();
        target->release();
    });
    onLoop([&]() { EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"target"})); });
}

TEST_F(ComponentRelease, FreeBeforeTheLoopTurnsFreesOnce) {
    onLoop([&]() {
        Probe* target = make("target");
        target->release();
        target->free();
        EXPECT_EQ(only(log_, "deleted").size(), 1u);
    });
    onLoop([&]() { EXPECT_EQ(only(log_, "deleted").size(), 1u); });
}

TEST_F(ComponentRelease, FreeingTheOwnerFirstLeavesNothingForTheQueuedFree) {
    onLoop([&]() {
        Probe* root = make("root");
        Probe* child = root->createOwned<Probe>(log_, "child");
        child->release();
        root->free();
    });
    onLoop([&]() { EXPECT_EQ(only(log_, "deleted"), (std::vector<std::string>{"child", "root"})); });
}

TEST_F(ComponentRelease, AComponentCanReleaseItselfFromItsOwnEventHandler) {
    struct Button : Component {
        newui::Delegate<Button> onClick;
    };
    ComponentRef<Button> reference;
    onLoop([&]() {
        Button* button = new Button();
        reference = button;
        button->onClick.add([](Button& sender) {
            sender.release();
            return newui::SyncReturn::Handled;
        });
        button->onClick.syncCall(*button);
        EXPECT_TRUE(reference);
    });
    onLoop([&]() { EXPECT_FALSE(reference); });
}

TEST_F(ComponentRelease, ACallQueuedForASenderThatIsThenFreedCallsNothing) {
    struct Sender : Component {
        newui::Delegate<Sender> onDone;
    };
    // The delegate outlives its sender here, so only the sender's lifetime can stop the call.
    newui::Delegate<Sender> delegate;
    int calls = 0;
    delegate.add([&calls](Sender&) {
        ++calls;
        return newui::SyncReturn::Handled;
    });
    newui::AsyncReturn freed;
    newui::AsyncReturn releasing;
    newui::AsyncReturn alive;
    Sender* kept = nullptr;
    onLoop([&]() {
        Sender* gone = new Sender();
        freed = delegate.postCall(*thread_.loop, gone);
        gone->free();

        Sender* leaving = new Sender();
        leaving->release();
        releasing = delegate.postCall(*thread_.loop, leaving);

        kept = new Sender();
        alive = delegate.postCall(*thread_.loop, kept);
    });
    ASSERT_TRUE(alive.waitFor(std::chrono::seconds(5)));
    ASSERT_TRUE(freed.isDone());
    ASSERT_TRUE(releasing.isDone());
    EXPECT_TRUE(freed.result().ignored());
    EXPECT_TRUE(releasing.result().ignored());
    onLoop([&]() {
        EXPECT_EQ(calls, 1);
        kept->free();
    });
}

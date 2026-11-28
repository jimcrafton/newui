// Views with owners: a view's parent no longer decides when an owned view is deleted.

#include "newui/componentref.h"
#include "newui/controllers.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/popuptool.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/splitter.h"
#include "newui/subview.h"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <future>
#include <map>
#include <string>
#include <vector>

namespace {

using newui::Component;
using newui::ComponentRef;
using newui::Rect;
using newui::SubView;

// What happened to the probes of the current test, in order.
std::vector<std::string> g_events;

std::size_t countOf(const std::string& event) {
    std::size_t count = 0;
    for (const std::string& entry : g_events) {
        if (entry == event) {
            ++count;
        }
    }
    return count;
}

class ProbeView : public SubView {
public:
    explicit ProbeView(const std::string& name) {
        setName(name);
        setVisible(true);
        onDestroyed.add([](newui::View& view) {
            g_events.push_back("destroyed " + view.name());
            return newui::SyncReturn::Handled;
        });
    }
    ~ProbeView() override { g_events.push_back("deleted " + name()); }

    std::function<void()> onBeforeDestruction;

protected:
    void beforeDestruction() override {
        g_events.push_back("before " + name());
        if (onBeforeDestruction) {
            onBeforeDestruction();
        }
    }
};

class ProbeController : public Component {
public:
    explicit ProbeController(const std::string& name) { setName(name); }
    ~ProbeController() override { g_events.push_back("deleted " + name()); }

    std::function<void()> onBeforeDestruction;
    using Component::connect;

protected:
    void beforeDestruction() override {
        g_events.push_back("before " + name());
        if (onBeforeDestruction) {
            onBeforeDestruction();
        }
    }
};

// A RootView with no window, as test_rootview.cpp uses.
class HeadlessRoot : public newui::RootView {
public:
    explicit HeadlessRoot(const std::string& name = "root") : newui::RootView(nullptr, Rect(0, 0, 200, 200), name) {}

    using newui::RootView::mouseDown;
    using newui::RootView::mouseMove;
};

void dispose(newui::View* view) {
    view->destroy();
    delete view;
}

class ViewOwnership : public ::testing::Test {
protected:
    void SetUp() override { g_events.clear(); }
};

}  // namespace

TEST_F(ViewOwnership, AnUnownedChildIsStillDestroyedAndDeletedByItsParent) {
    auto* parent = new ProbeView("parent");
    auto* child = new ProbeView("child");
    parent->addChild(child);
    ComponentRef<ProbeView> reference(child);

    dispose(parent);

    EXPECT_FALSE(reference);
    EXPECT_EQ(countOf("destroyed child"), 1u);
    EXPECT_EQ(countOf("deleted child"), 1u);
    EXPECT_EQ(countOf("before child"), 1u);
}

TEST_F(ViewOwnership, AnOwnedChildIsOnlyDetachedAndStaysUsable) {
    Component* keeper = new Component();
    auto* parent = new ProbeView("parent");
    ProbeView* child = keeper->createOwned<ProbeView>("child");
    auto* grandchild = new ProbeView("grandchild");
    child->addChild(grandchild);
    parent->addChild(child);

    dispose(parent);

    EXPECT_EQ(countOf("destroyed child"), 0u);
    EXPECT_EQ(countOf("deleted child"), 0u);
    EXPECT_FALSE(child->isDestroying());
    EXPECT_FALSE(grandchild->isDestroying());
    EXPECT_EQ(child->parent(), nullptr);
    EXPECT_EQ(child->rootView(), nullptr);
    EXPECT_EQ(child->childViews().size(), 1u);

    auto* other = new ProbeView("other");
    other->addChild(child);
    child->setBounds(Rect(1, 2, 30, 40));
    EXPECT_EQ(child->bounds(), Rect(1, 2, 30, 40)) << "layout and resize still work on it";

    keeper->free();
    EXPECT_TRUE(other->childViews().empty());
    EXPECT_EQ(countOf("deleted child"), 1u);
    EXPECT_EQ(countOf("deleted grandchild"), 1u);
    dispose(other);
}

TEST_F(ViewOwnership, ARootOwnedViewParentedInAContainerIsFreedExactlyOnce) {
    auto* root = new HeadlessRoot();
    auto* container = new ProbeView("container");
    root->addChild(container);
    ProbeView* panel = root->createOwned<ProbeView>("panel");
    container->addChild(panel);
    ComponentRef<ProbeView> reference(panel);

    dispose(root);

    EXPECT_FALSE(reference);
    EXPECT_EQ(countOf("before panel"), 1u);
    EXPECT_EQ(countOf("destroyed panel"), 1u);
    EXPECT_EQ(countOf("deleted panel"), 1u);
    EXPECT_EQ(countOf("deleted container"), 1u);
}

TEST_F(ViewOwnership, RootOwnedComponentsAreFreedWhileTheViewTreeIsStillWhole) {
    auto* root = new HeadlessRoot();
    auto* container = new ProbeView("container");
    auto* leaf = new ProbeView("leaf");
    root->addChild(container);
    container->addChild(leaf);

    ProbeController* controller = root->createOwned<ProbeController>("controller");
    int heard = 0;
    controller->connect(leaf->onDestroyed, [&heard](newui::View&) {
        ++heard;
        return newui::SyncReturn::Handled;
    });
    bool checked = false;
    controller->onBeforeDestruction = [&]() {
        EXPECT_EQ(root->childViews().size(), 1u);
        EXPECT_EQ(container->childViews().size(), 1u);
        EXPECT_EQ(leaf->parent(), container);
        EXPECT_EQ(countOf("destroyed leaf"), 0u);
        checked = true;
    };

    dispose(root);

    EXPECT_TRUE(checked);
    EXPECT_EQ(heard, 0) << "its subscriptions were gone before any view was torn down";
    EXPECT_EQ(countOf("deleted controller"), 1u);
}

TEST_F(ViewOwnership, ADestroyedViewFreesWhatItOwnsBeforeItsChildren) {
    auto* parent = new ProbeView("parent");
    auto* child = new ProbeView("child");
    parent->addChild(child);
    child->createOwned<ProbeController>("childs");
    parent->createOwned<ProbeController>("parents");

    dispose(parent);

    EXPECT_EQ(g_events, (std::vector<std::string>{
        "before parent", "before parents", "deleted parents",
        "before child", "before childs", "deleted childs", "destroyed child", "deleted child",
        "destroyed parent", "deleted parent"}));
}

TEST_F(ViewOwnership, AChildOwnedByItsOwnParentIsFreedOnce) {
    auto* parent = new ProbeView("parent");
    ProbeView* child = parent->createOwned<ProbeView>("child");
    parent->addChild(child);

    dispose(parent);

    EXPECT_EQ(countOf("deleted child"), 1u);
    EXPECT_EQ(countOf("destroyed child"), 1u);
}

TEST_F(ViewOwnership, AViewOwnedByAViewElsewhereInTheTreeIsFreedOnce) {
    auto* top = new ProbeView("top");
    auto* first = new ProbeView("first");
    auto* second = new ProbeView("second");
    top->addChild(first);
    top->addChild(second);
    ProbeView* owned = second->createOwned<ProbeView>("owned");
    first->addChild(owned);

    dispose(top);

    EXPECT_EQ(countOf("deleted owned"), 1u);
    EXPECT_EQ(countOf("destroyed owned"), 1u);
}

TEST_F(ViewOwnership, MovingAnOwnedViewBetweenContainersAndRootsNeverDeletesIt) {
    Component* keeper = new Component();
    auto* mainRoot = new HeadlessRoot("main");
    auto* floatingRoot = new HeadlessRoot("floating");
    auto* dockA = new ProbeView("dockA");
    auto* dockB = new ProbeView("dockB");
    mainRoot->addChild(dockA);
    mainRoot->addChild(dockB);
    ProbeView* panel = keeper->createOwned<ProbeView>("panel");

    panel->setParent(dockA);
    panel->setParent(dockB);
    panel->setParent(floatingRoot);
    EXPECT_EQ(panel->rootView(), floatingRoot);
    panel->setParent(dockA);
    EXPECT_EQ(panel->rootView(), mainRoot);
    panel->setParent(floatingRoot);

    dispose(floatingRoot);  // the floating window closes
    EXPECT_EQ(countOf("deleted panel"), 0u);
    EXPECT_EQ(panel->parent(), nullptr);
    panel->setParent(dockB);
    EXPECT_EQ(panel->rootView(), mainRoot);

    dispose(mainRoot);
    EXPECT_EQ(countOf("deleted panel"), 0u);
    keeper->free();
    EXPECT_EQ(countOf("deleted panel"), 1u);
}

TEST_F(ViewOwnership, FreeingADisplayedViewDetachesItAndClearsFocusHoverAndCapture) {
    auto* root = new HeadlessRoot();
    ProbeView* panel = root->createOwned<ProbeView>("panel");
    auto* field = new ProbeView("field");
    root->addChild(panel);
    panel->addChild(field);
    panel->setBounds(Rect(0, 0, 100, 100));
    field->setBounds(Rect(0, 0, 100, 100));
    field->setAcceptsFocus(true);

    root->mouseMove(newui::Point(10, 10), 0, 0);
    root->mouseDown(newui::Point(10, 10), 1, 0);
    root->setFocusedSubView(field);
    ASSERT_EQ(root->hoveredSubView(), field);
    ASSERT_EQ(root->capturedSubView(), field);
    ASSERT_EQ(root->focusedSubView(), field);

    panel->free();

    EXPECT_TRUE(root->childViews().empty());
    EXPECT_EQ(root->hoveredSubView(), nullptr);
    EXPECT_EQ(root->capturedSubView(), nullptr);
    EXPECT_EQ(root->focusedSubView(), nullptr);
    EXPECT_EQ(root->componentCount(), 0u);
    EXPECT_EQ(countOf("deleted panel"), 1u);
    EXPECT_EQ(countOf("deleted field"), 1u);
    dispose(root);
}

TEST_F(ViewOwnership, AnUnownedViewCanBeFreedToo) {
    auto* parent = new ProbeView("parent");
    auto* child = new ProbeView("child");
    parent->addChild(child);

    child->free();

    EXPECT_TRUE(parent->childViews().empty());
    EXPECT_EQ(countOf("destroyed child"), 1u);
    EXPECT_EQ(countOf("deleted child"), 1u);
    dispose(parent);
}

TEST_F(ViewOwnership, DestroyThenFreeDestroysOnce) {
    Component* keeper = new Component();
    ProbeView* view = keeper->createOwned<ProbeView>("view");
    ComponentRef<ProbeView> reference(view);

    view->destroy();
    EXPECT_FALSE(reference) << "a destroyed view counts as gone";
    keeper->free();

    EXPECT_EQ(countOf("destroyed view"), 1u);
    EXPECT_EQ(countOf("before view"), 1u);
    EXPECT_EQ(countOf("deleted view"), 1u);
}

TEST_F(ViewOwnership, AChildThatFreesItsOwnerWhileItsParentIsDestroyedIsSafe) {
    Component* keeper = new Component();
    ProbeView* owned = keeper->createOwned<ProbeView>("owned");
    auto* parent = new ProbeView("parent");
    auto* child = new ProbeView("child");
    parent->addChild(child);
    parent->addChild(owned);
    child->onBeforeDestruction = [keeper]() { keeper->free(); };

    dispose(parent);

    EXPECT_EQ(countOf("deleted owned"), 1u);
    EXPECT_EQ(countOf("deleted child"), 1u);
}

TEST_F(ViewOwnership, OwnedContentSurvivesAScrollViewAndASplitter) {
    Component* keeper = new Component();
    auto* scroll = new newui::ScrollView();
    ProbeView* content = keeper->createOwned<ProbeView>("content");
    scroll->addChild(content);
    auto* splitter = new newui::Splitter();
    ProbeView* pane = keeper->createOwned<ProbeView>("pane");
    splitter->addChild(pane);

    dispose(scroll);
    dispose(splitter);

    EXPECT_EQ(countOf("destroyed content"), 0u);
    EXPECT_EQ(countOf("destroyed pane"), 0u);
    EXPECT_EQ(content->parent(), nullptr);
    EXPECT_EQ(pane->parent(), nullptr);
    keeper->free();
}

TEST_F(ViewOwnership, FreeingContentInsideAScrollViewLeavesTheScrollViewWorking) {
    auto* scroll = new newui::ScrollView();
    const std::size_t chrome = scroll->childViews().size();
    ProbeView* content = scroll->createOwned<ProbeView>("content");
    scroll->addChild(content);

    content->free();

    EXPECT_EQ(scroll->childViews().size(), chrome);
    EXPECT_EQ(countOf("deleted content"), 1u);
    dispose(scroll);
}

namespace {

class OnePaneController : public newui::ViewController {
protected:
    SubView* loadView() override { return new ProbeView("controlled"); }
};

class OwnedPaneController : public newui::ViewController {
public:
    explicit OwnedPaneController(Component& keeper) : keeper_(keeper) {}

protected:
    SubView* loadView() override { return keeper_.createOwned<ProbeView>("controlled"); }

private:
    Component& keeper_;
};

}  // namespace

TEST_F(ViewOwnership, AViewControllerStillDeletesAViewNothingOwns) {
    auto* container = new ProbeView("container");
    {
        OnePaneController controller;
        controller.present(container);
    }
    EXPECT_TRUE(container->childViews().empty());
    EXPECT_EQ(countOf("deleted controlled"), 1u);
    dispose(container);
}

TEST_F(ViewOwnership, AViewControllerLeavesAnOwnedViewToItsOwner) {
    Component* keeper = new Component();
    auto* container = new ProbeView("container");
    {
        OwnedPaneController controller(*keeper);
        controller.present(container);
    }
    EXPECT_TRUE(container->childViews().empty());
    EXPECT_EQ(countOf("destroyed controlled"), 0u);
    EXPECT_EQ(keeper->componentCount(), 1u);
    keeper->free();
    dispose(container);
}

TEST_F(ViewOwnership, AViewControllerOutlivingItsViewDoesNotDeleteItAgain) {
    auto* container = new ProbeView("container");
    {
        OnePaneController controller;
        controller.present(container);
        dispose(container);
    }
    EXPECT_EQ(countOf("deleted controlled"), 1u);
}

namespace {

// Runs each step on a run loop's own thread, waiting for it.
class LoopThread {
public:
    LoopThread() : thread_(newui::RunLoop::runThreaded()) { thread_.loop->waitForStart(); }
    ~LoopThread() {
        thread_.loop->quit();
        thread_.thread.join();
    }

    void run(std::function<void()> task) {
        std::promise<void> done;
        thread_.loop->post([&]() {
            task();
            done.set_value();
        });
        ASSERT_EQ(done.get_future().wait_for(std::chrono::seconds(5)), std::future_status::ready);
    }

private:
    newui::RunLoop::RunLoopThread thread_;
};

}  // namespace

TEST_F(ViewOwnership, AReleasedViewIsHiddenAtOnceAndGoesOnTheNextTurn) {
    LoopThread loop;
    HeadlessRoot* root = nullptr;
    ComponentRef<ProbeView> reference;
    loop.run([&]() {
        root = new HeadlessRoot();
        ProbeView* panel = root->createOwned<ProbeView>("panel");
        root->addChild(panel);
        reference = panel;

        panel->release();

        EXPECT_FALSE(panel->isVisible());
        EXPECT_TRUE(panel->isDestroying());
        EXPECT_EQ(panel->parent(), root) << "still attached until the loop turns";
        EXPECT_FALSE(root->isDestroying());
    });
    loop.run([&]() {
        EXPECT_FALSE(reference);
        EXPECT_TRUE(root->childViews().empty());
        EXPECT_EQ(countOf("destroyed panel"), 1u);
        EXPECT_EQ(countOf("deleted panel"), 1u);
        dispose(root);
    });
}

TEST_F(ViewOwnership, ReleasingAViewThenDestroyingTheRootFreesItOnce) {
    LoopThread loop;
    loop.run([&]() {
        auto* root = new HeadlessRoot();
        ProbeView* panel = root->createOwned<ProbeView>("panel");
        root->addChild(panel);
        panel->release();
        dispose(root);
    });
    loop.run([&]() { EXPECT_EQ(countOf("deleted panel"), 1u); });
}

TEST_F(ViewOwnership, AnOwnedPopupDismissedLeavesItsOwner) {
    LoopThread loop;
    Component* keeper = nullptr;
    HWND owner = nullptr;
    ComponentRef<newui::PopupTool> popup;
    bool shown = false;
    loop.run([&]() {
        keeper = new Component();
        owner = ::CreateWindowExA(0, "STATIC", "owner", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200, nullptr, nullptr,
                                  ::GetModuleHandleA(nullptr), nullptr);
        auto* tool = keeper->createOwned<newui::CalloutTool>(owner, ::GetModuleHandleA(nullptr), Rect(10, 10, 120, 80), "p");
        popup = tool;
        shown = tool->initialize();
        tool->dismiss();
    });
    loop.run([&]() {
        EXPECT_TRUE(shown);
        EXPECT_FALSE(popup);
        EXPECT_EQ(keeper->componentCount(), 0u);
        keeper->free();
        ::DestroyWindow(owner);
    });
}

namespace {

// Destroying a Frame's window posts WM_QUIT, which later tests must not see.
void drainQuitMessages() {
    MSG quit;
    while (::PeekMessage(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

}  // namespace

TEST_F(ViewOwnership, ClosingARealFrameFreesRootOwnedComponentsBeforeTheViews) {
    auto* frame = new newui::Frame();
    ASSERT_TRUE(frame->initialize());
    ::ShowWindow(frame->frameHandle(), SW_HIDE);
    newui::RootView& root = frame->rootView();

    auto* container = new ProbeView("container");
    root.addChild(container);
    ProbeController* controller = root.createOwned<ProbeController>("controller");
    ProbeView* panel = root.createOwned<ProbeView>("panel");
    container->addChild(panel);
    bool viewsAlive = false;
    controller->onBeforeDestruction = [&]() { viewsAlive = countOf("destroyed container") == 0 && container->parent() != nullptr; };

    ::DestroyWindow(frame->frameHandle());
    drainQuitMessages();

    EXPECT_TRUE(viewsAlive);
    EXPECT_EQ(countOf("deleted controller"), 1u);
    EXPECT_EQ(countOf("deleted panel"), 1u);
    EXPECT_EQ(countOf("deleted container"), 1u);
    delete frame;
}

TEST_F(ViewOwnership, AFrameNeverShownStillFreesWhatItsRootViewOwns) {
    {
        newui::Frame frame;
        frame.rootView().createOwned<ProbeController>("controller");
    }
    EXPECT_EQ(countOf("deleted controller"), 1u);
}

TEST(ViewOwnershipDeathTest, DeletingAFrameWithALiveWindowAndRootViewStillFailsLoudly) {
    EXPECT_DEATH(
        {
            auto* frame = new newui::Frame();
            if (frame->initialize()) {
                ::ShowWindow(frame->frameHandle(), SW_HIDE);
                delete frame;
            }
        },
        "");
}

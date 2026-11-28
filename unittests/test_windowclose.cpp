// Controllers and window close: what a root view owns is freed first, while the window and the whole view tree still exist.

#include "newui/animation.h"
#include "newui/componentref.h"
#include "newui/controllers.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/subview.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <thread>
#include <vector>

namespace {

using newui::ChildOwnership;
using newui::Component;
using newui::ComponentRef;
using newui::Frame;
using newui::SubView;
using newui::SyncReturn;

// What happened in the current test, in order.
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

// A view with a delegate of its own, fired once more while the view itself is torn down.
class PingView : public SubView {
public:
    explicit PingView(const std::string& name) {
        setName(name);
        setVisible(true);
    }
    ~PingView() override { g_events.push_back("deleted " + name()); }

    newui::Delegate<PingView> onPing;

protected:
    void beforeDestruction() override {
        g_events.push_back("before " + name());
        onPing(*this);
    }
};

class FeatureController : public newui::Controller {
public:
    FeatureController(const std::string& name, PingView& view) : view_(view) {
        setName(name);
        connect(view.onPing, [this](PingView&) {
            g_events.push_back(this->name() + " heard ping");
            return SyncReturn::Handled;
        });
        connect(view.onDestroyed, [this](newui::View&) {
            g_events.push_back(this->name() + " heard destroyed");
            return SyncReturn::Handled;
        });
    }
    ~FeatureController() override { g_events.push_back("deleted " + name()); }

    std::function<void()> onBeforeDestruction;

protected:
    void beforeDestruction() override {
        g_events.push_back("before " + name());
        view_.onPing(view_);
        if (onBeforeDestruction) {
            onBeforeDestruction();
        }
    }

private:
    PingView& view_;
};

class MainWindowController : public newui::FrameController {
public:
    using newui::FrameController::FrameController;

    PingView* view = nullptr;
    ComponentRef<FeatureController> first;
    ComponentRef<FeatureController> second;
    bool allowClose = true;

protected:
    void frameDidLoad() override {
        newui::RootView& root = frame().rootView();
        view = new PingView("view");
        root.addChild(view);
        first = root.createOwned<FeatureController>("first", *view);
        second = root.createOwned<FeatureController>("second", *view);
    }
    void frameShouldClose(bool& canClose) override { canClose = allowClose; }
    void frameWillClose() override { g_events.push_back("will close"); }
};

// Destroying a Frame's window posts WM_QUIT, which later tests must not see.
void drainQuitMessages() {
    MSG quit;
    while (::PeekMessage(&quit, nullptr, WM_QUIT, WM_QUIT, PM_REMOVE)) {
    }
}

class WindowClose : public ::testing::Test {
protected:
    void SetUp() override { g_events.clear(); }
    void TearDown() override { drainQuitMessages(); }
};

// What a user's click on the title bar X sends.
void closeWindow(HWND window) {
    ::SendMessage(window, WM_CLOSE, 0, 0);
}

class PaneController : public newui::ViewController {
public:
    explicit PaneController(const std::string& name) { setName(name); }
    ~PaneController() override { g_events.push_back("deleted " + name()); }

    static std::atomic<int> appeared;
    newui::Animation* animation = nullptr;

protected:
    SubView* loadView() override { return new PingView(name() + " view"); }
    newui::Animation* appearingAnimation() override { return animation; }
    void viewDidAppear() override { ++appeared; }
};

std::atomic<int> PaneController::appeared{0};

}  // namespace

TEST_F(WindowClose, FeatureControllersAreFreedBeforeAnyViewAndHearNothingAfterwards) {
    auto* frame = new Frame();
    {
        MainWindowController controller(*frame);
        ASSERT_TRUE(frame->initialize());
        ::ShowWindow(frame->frameHandle(), SW_HIDE);
        ASSERT_TRUE(controller.isLoaded());
        ASSERT_TRUE(controller.first);
        ASSERT_TRUE(controller.second);

        controller.view->onPing(*controller.view);
        ASSERT_EQ(countOf("first heard ping"), 1u);
        ASSERT_EQ(countOf("second heard ping"), 1u);
        g_events.clear();

        closeWindow(frame->frameHandle());

        // "second" goes first: "first" still hears its ping, then neither hears the view's.
        EXPECT_EQ(g_events, (std::vector<std::string>{
            "will close",
            "before second", "first heard ping", "deleted second",
            "before first", "deleted first",
            "before view", "deleted view"}));
        EXPECT_FALSE(controller.first);
        EXPECT_FALSE(controller.second);
    }
    delete frame;
}

TEST_F(WindowClose, TheWindowIsStillUsableWhileRootOwnedControllersAreFreed) {
    auto* frame = new Frame();
    {
        MainWindowController controller(*frame);
        ASSERT_TRUE(frame->initialize());
        ::ShowWindow(frame->frameHandle(), SW_HIDE);

        bool checked = false;
        controller.first->onBeforeDestruction = [&]() {
            EXPECT_TRUE(::IsWindow(frame->frameHandle()));
            WINDOWPLACEMENT placement = {};
            placement.length = sizeof(placement);
            EXPECT_TRUE(::GetWindowPlacement(frame->frameHandle(), &placement));
            EXPECT_TRUE(::IsWindow(frame->rootView().windowHandle()));
            EXPECT_EQ(controller.view->parent(), &frame->rootView());
            checked = true;
        };

        closeWindow(frame->frameHandle());

        EXPECT_TRUE(checked);
    }
    delete frame;
}

TEST_F(WindowClose, AVetoedCloseFreesNothing) {
    auto* frame = new Frame();
    {
        MainWindowController controller(*frame);
        ASSERT_TRUE(frame->initialize());
        ::ShowWindow(frame->frameHandle(), SW_HIDE);
        controller.allowClose = false;

        closeWindow(frame->frameHandle());

        EXPECT_TRUE(::IsWindow(frame->frameHandle()));
        EXPECT_TRUE(controller.first);
        EXPECT_TRUE(controller.second);
        EXPECT_EQ(countOf("will close"), 0u);

        controller.allowClose = true;
        closeWindow(frame->frameHandle());
        EXPECT_FALSE(controller.first);
        EXPECT_EQ(countOf("deleted first"), 1u);
    }
    delete frame;
}

TEST_F(WindowClose, RootOwnedViewControllersAndTheirChildrenAreFreedOnce) {
    auto* frame = new Frame();
    ASSERT_TRUE(frame->initialize());
    ::ShowWindow(frame->frameHandle(), SW_HIDE);
    newui::RootView& root = frame->rootView();
    PaneController* parent = root.createOwned<PaneController>("parent");
    parent->present(&root);
    PaneController* child = new PaneController("child");
    parent->addChildController(child, ChildOwnership::Parent);
    ComponentRef<PaneController> childReference(child);

    closeWindow(frame->frameHandle());

    EXPECT_FALSE(childReference);
    EXPECT_EQ(countOf("deleted child"), 1u);
    EXPECT_EQ(countOf("deleted parent"), 1u);
    EXPECT_EQ(countOf("deleted child view"), 1u);
    EXPECT_EQ(countOf("deleted parent view"), 1u);
    delete frame;
}

TEST_F(WindowClose, AParentOwnedChildSurvivesDismissAndGoesWithItsParent) {
    Component* keeper = new Component();
    PaneController* parent = keeper->createOwned<PaneController>("parent");
    PaneController* child = new PaneController("child");
    parent->addChildController(child, ChildOwnership::Parent);
    EXPECT_EQ(child->owner(), parent);
    EXPECT_EQ(child->parentController(), parent);

    child->removeFromParentController();
    EXPECT_EQ(child->owner(), parent);
    EXPECT_EQ(countOf("deleted child"), 0u);

    keeper->free();

    EXPECT_EQ(countOf("deleted child"), 1u);
    EXPECT_EQ(countOf("deleted child view"), 1u);
    EXPECT_EQ(countOf("deleted parent"), 1u);
    EXPECT_EQ(countOf("deleted parent view"), 1u);
}

TEST_F(WindowClose, ByDefaultAChildControllerStaysTheCallersAndMayOutliveItsParent) {
    auto* parent = new PaneController("parent");
    {
        PaneController child("child");
        parent->addChildController(&child);
        EXPECT_EQ(child.owner(), nullptr);

        parent->free();

        EXPECT_EQ(countOf("deleted child"), 0u);
        EXPECT_EQ(child.parentController(), nullptr);
        child.dismiss();  // its view went with the parent's
    }
    EXPECT_EQ(countOf("deleted child view"), 1u);
    EXPECT_EQ(countOf("deleted child"), 1u);
}

TEST_F(WindowClose, AControllerFreedWhileItsViewIsAppearingIsNotCalledBack) {
    PaneController::appeared = 0;
    newui::Animation* animation = newui::AnimationManager::addAnimation("appear", 0, 0);
    newui::RunLoop::RunLoopThread thread = newui::RunLoop::runThreaded();
    thread.loop->waitForStart();

    Component* keeper = nullptr;
    SubView* container = nullptr;
    std::promise<void> started;
    thread.loop->post([&]() {
        keeper = new Component();
        container = new SubView();
        PaneController* gone = keeper->createOwned<PaneController>("gone");
        gone->animation = animation;
        gone->present(container);
        gone->free();
        PaneController* kept = keeper->createOwned<PaneController>("kept");
        kept->animation = animation;
        kept->present(container);
        started.set_value();
    });
    started.get_future().wait();
    for (int i = 0; i < 500 && PaneController::appeared == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    std::promise<void> cleaned;
    thread.loop->post([&]() {
        keeper->free();
        container->destroy();
        delete container;
        cleaned.set_value();
    });
    cleaned.get_future().wait();
    thread.loop->quit();
    thread.thread.join();
    newui::AnimationManager::removeAnimation(animation);

    EXPECT_EQ(PaneController::appeared, 1) << "only the controller that is still there";
}

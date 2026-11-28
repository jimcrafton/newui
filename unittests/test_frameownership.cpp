// Frames as components: who frees a frame, in what order its parts go, and which window ends the application.

#include "newui/application.h"
#include "newui/componentref.h"
#include "newui/controllers.h"
#include "newui/dialogs.h"
#include "newui/frame.h"
#include "newui/popupframe.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/subview.h"
#include "newui/toolframe.h"

#include <gtest/gtest.h>

#include <functional>
#include <future>
#include <string>
#include <vector>

namespace {

using newui::Application;
using newui::Component;
using newui::ComponentRef;
using newui::Dialog;
using newui::Frame;
using newui::PopupFrame;
using newui::Rect;
using newui::SubView;
using newui::SyncReturn;
using newui::ToolFrame;

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

class ProbeView : public SubView {
public:
    explicit ProbeView(const std::string& name) {
        setName(name);
        setVisible(true);
    }
    ~ProbeView() override { g_events.push_back("deleted " + name()); }
};

template<typename Base>
class Probe : public Base {
public:
    explicit Probe(const std::string& name = "frame") { this->setName(name); }
    ~Probe() override { g_events.push_back("deleted " + this->name()); }

    std::function<void()> onBefore;

protected:
    void beforeDestruction() override {
        g_events.push_back("before " + this->name());
        if (onBefore) {
            onBefore();
        }
    }
};

class ProbeController : public newui::FrameController {
public:
    ProbeController(Frame& target, const std::string& name) : newui::FrameController(target) { setName(name); }
    ~ProbeController() override { g_events.push_back("deleted " + name()); }

    std::function<void()> onBefore;

protected:
    void beforeDestruction() override {
        g_events.push_back("before " + name());
        if (onBefore) {
            onBefore();
        }
    }
};

// Empties this thread's queue, saying whether a WM_QUIT was in it. Everything else has to be dispatched on the way: Windows
// hands WM_QUIT out only once no posted message is left.
bool takeQuit() {
    bool found = false;
    MSG message;
    while (::PeekMessage(&message, nullptr, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) {
            found = true;
        } else {
            ::DispatchMessage(&message);
        }
    }
    return found;
}

// Runs each step on a run loop's own thread, waiting for it, so that release() has a loop to come back on.
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
        done.get_future().wait();
    }

private:
    newui::RunLoop::RunLoopThread thread_;
};

void showHidden(Frame& frame) {
    ASSERT_TRUE(frame.initialize());
    ::ShowWindow(frame.frameHandle(), SW_HIDE);
}

class FrameOwnership : public ::testing::Test {
protected:
    void SetUp() override {
        g_events.clear();
        takeQuit();
    }
    void TearDown() override {
        Application::instance().setFrame(nullptr);
        takeQuit();
    }
};

}  // namespace

TEST_F(FrameOwnership, AFrameIsAComponentThatOwnsItsRootView) {
    Frame frame;
    EXPECT_EQ(frame.rootView().owner(), &frame);
    EXPECT_EQ(frame.findComponent("rootview"), &frame.rootView());
    frame.setName("main");
    EXPECT_EQ(frame.getName(), "main");
    EXPECT_FALSE(frame.isInitialized());
    showHidden(frame);
    EXPECT_TRUE(frame.isInitialized());
    const HWND window = frame.frameHandle();
    EXPECT_TRUE(frame.initialize()) << "a second call keeps the window";
    EXPECT_EQ(frame.frameHandle(), window);
    ::DestroyWindow(window);
}

TEST_F(FrameOwnership, ADestroyedWindowLeavesNoHandleOrRootView) {
    Frame frame;
    showHidden(frame);
    ComponentRef<newui::RootView> root(&frame.rootView());

    ::DestroyWindow(frame.frameHandle());

    EXPECT_EQ(frame.frameHandle(), nullptr);
    EXPECT_FALSE(root);
    EXPECT_EQ(frame.componentCount(), 0u);
    EXPECT_FALSE(frame.initialize()) << "a frame's window cannot be made again";
}

TEST_F(FrameOwnership, AFrameConstructedOnOneThreadBelongsToItsWindowsThread) {
    auto* frame = new Probe<Frame>();
    auto* controller = new ProbeController(*frame, "controller");
    frame->addComponent(controller);
    auto* feature = frame->rootView().createOwned<Probe<newui::Controller>>("feature");
    ASSERT_EQ(controller->owner(), frame);
    ASSERT_NE(feature, nullptr);
    LoopThread loop;
    loop.run([&]() {
        showHidden(*frame);
        ::DestroyWindow(frame->frameHandle());
        takeQuit();
        EXPECT_EQ(countOf("deleted feature"), 1u);
        EXPECT_EQ(frame->componentCount(), 1u);
        frame->free();
        EXPECT_EQ(countOf("deleted controller"), 1u);
    });
}

#ifdef NDEBUG
TEST_F(FrameOwnership, AControllerItsFrameDoesNotOwnDoesNotLoad) {
    Frame frame;
    {
        ProbeController unowned(frame, "unowned");
        unowned.load();
        EXPECT_FALSE(unowned.isLoaded());
        EXPECT_NE(unowned.loadError().find("owned by its frame"), std::string::npos);
    }
    auto* misplaced = new ProbeController(frame, "misplaced");
    frame.rootView().addComponent(misplaced);
    misplaced->load();
    EXPECT_FALSE(misplaced->isLoaded());

    frame.addComponent(misplaced);
    misplaced->load();

    EXPECT_TRUE(misplaced->isLoaded());
    EXPECT_TRUE(misplaced->loadError().empty());
    misplaced->free();
}
#else
TEST(FrameOwnershipDeathTest, LoadingAControllerItsFrameDoesNotOwnAsserts) {
    EXPECT_DEATH(
        {
            Frame frame;
            ProbeController unowned(frame, "unowned");
            unowned.load();
        },
        "");
}

TEST(FrameOwnershipDeathTest, CreatingTheWindowOfAnUnownedControllerAsserts) {
    EXPECT_DEATH(
        {
            Frame frame;
            ProbeController unowned(frame, "unowned");
            frame.initialize();
        },
        "");
}
#endif

TEST_F(FrameOwnership, AnOwnedControllerAttachedAfterTheWindowExistsLoadsWhenAsked) {
    auto* frame = new Frame();
    showHidden(*frame);
    auto* controller = new ProbeController(*frame, "late");
    frame->addComponent(controller);
    EXPECT_FALSE(controller->isLoaded());

    controller->load();

    EXPECT_TRUE(controller->isLoaded());
    frame->free();
}

TEST_F(FrameOwnership, FreeingAFrameTakesOwnedFramesThenTheWindowThenTheViews) {
    auto* main = Application::instance().createFrame<Probe<Frame>>("main");
    Application::instance().setFrame(main);
    showHidden(*main);
    const HWND mainWindow = main->frameHandle();
    main->rootView().addChild(new ProbeView("main view"));
    auto* controller = new ProbeController(*main, "controller");
    main->addComponent(controller);
    auto* rootOwned = main->rootView().createOwned<Probe<newui::Controller>>("root owned");

    auto* first = main->createOwned<Probe<ToolFrame>>("first tool");
    auto* second = main->createOwned<Probe<ToolFrame>>("second tool");
    auto* popup = main->createOwned<Probe<PopupFrame>>("popup");
    ASSERT_TRUE(first->initialize(mainWindow));
    ASSERT_TRUE(second->initialize(mainWindow));
    ASSERT_TRUE(popup->initialize(mainWindow));
    first->rootView().addChild(new ProbeView("tool view"));

    auto windowAlive = [&]() { EXPECT_TRUE(::IsWindow(mainWindow)); };
    main->onBefore = windowAlive;
    first->onBefore = windowAlive;
    second->onBefore = windowAlive;
    popup->onBefore = windowAlive;
    controller->onBefore = windowAlive;
    rootOwned->onBefore = [&]() { EXPECT_EQ(countOf("deleted main view"), 0u); };

    main->free();

    EXPECT_EQ(g_events, (std::vector<std::string>{
        "before main",
        "before popup", "deleted popup",
        "before second tool", "deleted second tool",
        "before first tool", "deleted tool view", "deleted first tool",
        "before controller", "deleted controller",
        "before root owned", "deleted root owned", "deleted main view",
        "deleted main"}));
    EXPECT_FALSE(::IsWindow(mainWindow));
    EXPECT_TRUE(takeQuit());
    EXPECT_TRUE(Application::instance().frames().empty());
    EXPECT_EQ(Application::instance().getFrame(), nullptr);
}

TEST_F(FrameOwnership, ClosingTheMainWindowQuitsOnceAndOwnedFramesStillGoOnce) {
    LoopThread loop;
    Probe<Frame>* main = nullptr;
    ComponentRef<Probe<ToolFrame>> tool;
    loop.run([&]() {
        main = Application::instance().createFrame<Probe<Frame>>("main");
        Application::instance().setFrame(main);
        showHidden(*main);
        auto* made = main->createOwned<Probe<ToolFrame>>("tool");
        made->initialize(main->frameHandle());
        tool = made;
        ::SendMessage(main->frameHandle(), WM_CLOSE, 0, 0);
        EXPECT_TRUE(takeQuit());
        EXPECT_FALSE(takeQuit());
    });
    loop.run([&]() {
        EXPECT_FALSE(tool) << "Windows destroyed it with its owner window, so it released itself";
        EXPECT_EQ(countOf("deleted tool"), 1u);
        EXPECT_EQ(countOf("deleted main"), 1u) << "owned by the application, so closing it freed it";
        EXPECT_TRUE(Application::instance().frames().empty());
    });
}

TEST_F(FrameOwnership, ASecondFrameClosingDoesNotQuit) {
    Frame main;
    Application::instance().setFrame(&main);
    showHidden(main);
    Frame second;
    showHidden(second);

    ::SendMessage(second.frameHandle(), WM_CLOSE, 0, 0);
    EXPECT_FALSE(takeQuit());

    ::SendMessage(main.frameHandle(), WM_CLOSE, 0, 0);
    EXPECT_TRUE(takeQuit());
}

TEST_F(FrameOwnership, WithNoMainFrameADestroyedFrameStillQuits) {
    Frame frame;
    showHidden(frame);

    ::DestroyWindow(frame.frameHandle());

    EXPECT_TRUE(takeQuit());
}

TEST_F(FrameOwnership, AStackFrameStopsBeingTheMainFrameWhenItGoes) {
    {
        Frame frame;
        Application::instance().setFrame(&frame);
    }
    EXPECT_EQ(Application::instance().getFrame(), nullptr);
}

TEST_F(FrameOwnership, AToolFrameReleasedFromItsClosedHandlerGoesOnTheNextTurn) {
    LoopThread loop;
    Probe<Frame>* main = nullptr;
    HWND toolWindow = nullptr;
    ComponentRef<Probe<ToolFrame>> tool;
    int closed = 0;
    loop.run([&]() {
        main = Application::instance().createFrame<Probe<Frame>>("main");
        showHidden(*main);
        auto* made = main->createOwned<Probe<ToolFrame>>("tool");
        made->initialize(main->frameHandle());
        made->onClosed.add([&closed](ToolFrame& sender) {
            ++closed;
            sender.release();
            return SyncReturn::Handled;
        });
        toolWindow = made->frameHandle();
        tool = made;
        made->show();

        ::SendMessage(toolWindow, WM_CLOSE, 0, 0);

        EXPECT_EQ(closed, 1);
        EXPECT_TRUE(tool);
        EXPECT_TRUE(::IsWindow(toolWindow));
        EXPECT_FALSE(::IsWindowVisible(toolWindow));
    });
    loop.run([&]() {
        EXPECT_FALSE(tool);
        EXPECT_FALSE(::IsWindow(toolWindow));
        EXPECT_EQ(countOf("deleted tool"), 1u);
        EXPECT_EQ(main->componentCount(), 1u) << "only the root view is left";
        main->free();
        takeQuit();
    });
}

TEST_F(FrameOwnership, ClosingAToolFrameOnlyHidesIt) {
    Component* keeper = new Component();
    Frame main;
    showHidden(main);
    auto* tool = keeper->createOwned<Probe<ToolFrame>>("tool");
    ASSERT_TRUE(tool->initialize(main.frameHandle()));

    ::SendMessage(tool->frameHandle(), WM_CLOSE, 0, 0);

    EXPECT_NE(tool->frameHandle(), nullptr);
    EXPECT_FALSE(tool->isDestroying());
    keeper->free();
    ::DestroyWindow(main.frameHandle());
}

TEST_F(FrameOwnership, AToolFrameCanBeFreedShownOrHidden) {
    Component* keeper = new Component();
    Frame main;
    Application::instance().setFrame(&main);
    showHidden(main);
    auto* shown = keeper->createOwned<Probe<ToolFrame>>("shown");
    auto* hidden = keeper->createOwned<Probe<ToolFrame>>("hidden");
    ASSERT_TRUE(shown->initialize(main.frameHandle()));
    ASSERT_TRUE(hidden->initialize(main.frameHandle()));
    shown->show();
    const HWND shownWindow = shown->frameHandle();
    const HWND hiddenWindow = hidden->frameHandle();

    shown->free();
    hidden->free();

    EXPECT_FALSE(::IsWindow(shownWindow));
    EXPECT_FALSE(::IsWindow(hiddenWindow));
    EXPECT_FALSE(takeQuit());
    EXPECT_EQ(keeper->componentCount(), 0u);
    keeper->free();
    ::DestroyWindow(main.frameHandle());
}

TEST_F(FrameOwnership, FramesWhoseOwnerWindowIsDestroyedTearDownAndReleaseThemselves) {
    LoopThread loop;
    Component* keeper = nullptr;
    ComponentRef<Probe<ToolFrame>> tool;
    ComponentRef<Probe<PopupFrame>> popup;
    loop.run([&]() {
        keeper = new Component();
        auto* main = new Frame();
        showHidden(*main);
        auto* madeTool = keeper->createOwned<Probe<ToolFrame>>("tool");
        auto* madePopup = keeper->createOwned<Probe<PopupFrame>>("popup");
        madeTool->initialize(main->frameHandle());
        madePopup->initialize(main->frameHandle());
        madeTool->rootView().addChild(new ProbeView("tool view"));
        tool = madeTool;
        popup = madePopup;

        ::DestroyWindow(main->frameHandle());

        EXPECT_EQ(countOf("deleted tool view"), 1u);
        EXPECT_TRUE(tool) << "still there until the loop turns";
        EXPECT_EQ(tool->frameHandle(), nullptr);
        EXPECT_TRUE(tool->isDestroying());
        delete main;
        takeQuit();
    });
    loop.run([&]() {
        EXPECT_FALSE(tool);
        EXPECT_FALSE(popup);
        EXPECT_EQ(countOf("deleted tool"), 1u);
        EXPECT_EQ(countOf("deleted popup"), 1u);
        EXPECT_EQ(keeper->componentCount(), 0u);
        keeper->free();
    });
}

TEST_F(FrameOwnership, FreeingAReleasedFrameBeforeTheLoopTurnsFreesItOnce) {
    LoopThread loop;
    loop.run([&]() {
        auto* frame = Application::instance().createFrame<Probe<Frame>>("frame");
        showHidden(*frame);
        ::DestroyWindow(frame->frameHandle());
        frame->free();
        takeQuit();
    });
    loop.run([&]() { EXPECT_EQ(countOf("deleted frame"), 1u); });
}

TEST_F(FrameOwnership, AFrameThatNeverGotAWindowIsFreedWithItsRootView) {
    auto* frame = Application::instance().createFrame<Probe<Frame>>("frame");
    frame->rootView().addChild(new ProbeView("view"));

    frame->free();

    EXPECT_EQ(countOf("deleted view"), 1u);
    EXPECT_EQ(countOf("deleted frame"), 1u);
    EXPECT_FALSE(takeQuit());
}

TEST_F(FrameOwnership, ReleasingAFrameFromItsOwnViewsEventIsSafe) {
    LoopThread loop;
    ComponentRef<Probe<Frame>> reference;
    loop.run([&]() {
        auto* frame = Application::instance().createFrame<Probe<Frame>>("frame");
        showHidden(*frame);
        reference = frame;
        auto* view = new ProbeView("view");
        frame->rootView().addChild(view);
        view->setBounds(Rect(0, 0, 100, 100));
        view->onMouseDown.add([frame](newui::View&, const newui::Point&, int, int) {
            frame->release();
            return SyncReturn::Handled;
        });
        ::SendMessage(frame->rootView().windowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
        EXPECT_TRUE(reference);
        EXPECT_TRUE(frame->isDestroying());
    });
    loop.run([&]() {
        EXPECT_FALSE(reference);
        EXPECT_EQ(countOf("deleted view"), 1u);
        takeQuit();
    });
}

#ifndef NDEBUG
TEST(FrameOwnershipDeathTest, FreeingAFrameFromItsOwnEventHandlerAsserts) {
    EXPECT_DEATH(
        {
            auto* frame = Application::instance().createFrame<Frame>();
            if (frame->initialize()) {
                ::ShowWindow(frame->frameHandle(), SW_HIDE);
                frame->onClosed.add([](Frame& sender) {
                    sender.free();
                    return SyncReturn::Handled;
                });
                ::SendMessage(frame->frameHandle(), WM_CLOSE, 0, 0);
            }
        },
        "");
}

TEST(FrameOwnershipDeathTest, FreeingAFrameFromItsViewsEventHandlerAsserts) {
    EXPECT_DEATH(
        {
            auto* frame = Application::instance().createFrame<Frame>();
            if (frame->initialize()) {
                ::ShowWindow(frame->frameHandle(), SW_HIDE);
                auto* view = new SubView();
                view->setVisible(true);
                frame->rootView().addChild(view);
                view->setBounds(Rect(0, 0, 100, 100));
                view->onMouseDown.add([frame](newui::View&, const newui::Point&, int, int) {
                    frame->free();
                    return SyncReturn::Handled;
                });
                ::SendMessage(frame->rootView().windowHandle(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(10, 10));
            }
        },
        "");
}
#endif

TEST_F(FrameOwnership, AFloatingPanelSurvivesItsToolFrameAndCanGoBack) {
    Component* keeper = new Component();
    Frame main;
    showHidden(main);
    ProbeView* panel = keeper->createOwned<ProbeView>("panel");
    auto* tool = keeper->createOwned<Probe<ToolFrame>>("tool");
    ASSERT_TRUE(tool->initialize(main.frameHandle()));
    panel->setParent(&tool->rootView());

    tool->free();

    EXPECT_EQ(countOf("deleted panel"), 0u);
    EXPECT_EQ(panel->parent(), nullptr);
    EXPECT_FALSE(panel->isDestroying());
    panel->setParent(&main.rootView());
    EXPECT_EQ(panel->rootView(), &main.rootView());
    keeper->free();
    ::DestroyWindow(main.frameHandle());
}

TEST_F(FrameOwnership, AnOwnedDialogKeepsItsViewsUntilItIsFreed) {
    LoopThread loop;
    Component* keeper = nullptr;
    ComponentRef<Probe<Dialog>> dialog;
    loop.run([&]() {
        keeper = new Component();
        auto* made = keeper->createOwned<Probe<Dialog>>("dialog");
        made->rootView().addChild(new ProbeView("field"));
        made->show();
        dialog = made;
        ::SendMessage(made->frameHandle(), WM_CLOSE, 0, 0);
    });
    loop.run([&]() {
        ASSERT_TRUE(dialog);
        EXPECT_EQ(dialog->frameHandle(), nullptr);
        EXPECT_EQ(dialog->rootView().childViews().size(), 1u);
        EXPECT_FALSE(takeQuit());
        keeper->free();
        EXPECT_EQ(countOf("deleted field"), 1u);
        EXPECT_EQ(countOf("deleted dialog"), 1u);
    });
}

TEST_F(FrameOwnership, FreeingAShownDialogTakesItsWindowDown) {
    auto* dialog = Application::instance().createFrame<Probe<Dialog>>("dialog");
    ASSERT_TRUE(dialog->show());
    const HWND window = dialog->frameHandle();
    dialog->rootView().addChild(new ProbeView("field"));

    dialog->free();

    EXPECT_FALSE(::IsWindow(window));
    EXPECT_EQ(countOf("deleted field"), 1u);
    EXPECT_EQ(countOf("deleted dialog"), 1u);
}

TEST_F(FrameOwnership, FreeFramesFreesNewestFirst) {
    Application& app = Application::instance();
    app.createFrame<Probe<Frame>>("older");
    app.createFrame<Probe<Frame>>("newer");
    ASSERT_EQ(app.frames().size(), 2u);

    app.freeFrames();

    EXPECT_EQ(g_events, (std::vector<std::string>{"before newer", "deleted newer", "before older", "deleted older"}));
    EXPECT_TRUE(app.frames().empty());
}

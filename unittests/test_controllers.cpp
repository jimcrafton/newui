#include "newui/controllers.h"
#include "newui/controls.h"
#include "newui/models.h"
#include "newui/subview.h"

#include <string>
#include <vector>

#include <gtest/gtest.h>

// Covers the fully headless-testable surface of Model/Controller/Control/
// ViewController - no live HWND, RootView, or RunLoop needed, same as
// test_tabcontrol.cpp's simulated-click pattern. The one piece this
// deliberately doesn't cover: ViewController's animated present()/dismiss()
// path (appearingAnimation()/disappearingAnimation()) only actually defers
// anything once a live, pumped RunLoop is processing the idle task it
// posts to Application::instance().runLoop() - same "needs a real message
// pump, not covered headlessly" gap already documented for
// ThemedViewStyle::paint()/RootView elsewhere in this suite. The
// synchronous (no-animation) path exercised here is the default/common
// case and is fully covered.

using namespace newui;

namespace {

// ---------------------------------------------------------------------
// Model / Controller test fixtures
// ---------------------------------------------------------------------

class RecordingController : public Controller {
public:
    int modelChangedCount = 0;

    SyncReturn modelChanged(Model&) override {
        ++modelChangedCount;
        return SyncReturn::Handled;
    }
};

// ---------------------------------------------------------------------
// ViewController test fixture - loadView()/viewDidLoad() and every
// lifecycle hook increment a counter and append to a shared order log, so
// a test can assert both "did this fire" and "in what order".
// ---------------------------------------------------------------------

class RecordingViewController : public ViewController {
public:
    std::vector<std::string>* log = nullptr;
    int loadViewCallCount = 0;

protected:
    SubView* loadView() override {
        ++loadViewCallCount;
        if (log) log->push_back("loadView");
        auto* v = new SubView();
        v->setVisible(true);
        return v;
    }

    void viewDidLoad() override { if (log) log->push_back("viewDidLoad"); }
    void viewWillAppear() override { if (log) log->push_back("viewWillAppear"); }
    void viewDidAppear() override { if (log) log->push_back("viewDidAppear"); }
    void viewWillDisappear() override { if (log) log->push_back("viewWillDisappear"); }
    void viewDidDisappear() override { if (log) log->push_back("viewDidDisappear"); }
};

}  // namespace

// ---------------------------------------------------------------------
// Model
// ---------------------------------------------------------------------

TEST(Model, AddViewIncreasesCount) {
    Model model;
    auto* view = new SubView();

    EXPECT_EQ(model.viewCount(), 0u);
    model.addView(view);
    EXPECT_EQ(model.viewCount(), 1u);

    model.removeView(view);
    view->destroy();
    delete view;
}

TEST(Model, AddingSameViewTwiceIsNoOp) {
    Model model;
    auto* view = new SubView();

    model.addView(view);
    model.addView(view);
    EXPECT_EQ(model.viewCount(), 1u);

    model.removeView(view);
    view->destroy();
    delete view;
}

TEST(Model, RemoveViewDecreasesCount) {
    Model model;
    auto* view = new SubView();

    model.addView(view);
    model.removeView(view);
    EXPECT_EQ(model.viewCount(), 0u);

    view->destroy();
    delete view;
}

TEST(Model, ViewDestroyedAutoRemovesFromModel) {
    Model model;
    auto* view = new SubView();

    model.addView(view);
    ASSERT_EQ(model.viewCount(), 1u);

    view->destroy();
    delete view;

    EXPECT_EQ(model.viewCount(), 0u);
}

TEST(Model, UpdateAllViewsDoesNotCrashWithRegisteredViews) {
    Model model;
    auto* a = new SubView();
    auto* b = new SubView();

    model.addView(a);
    model.addView(b);

    // Neither view has a live RootView here - markDirty() no-ops safely
    // in that case (see ViewStyle::markDirty()); this is really a
    // regression guard against a dangling views_ entry crashing this call.
    model.updateAllViews();

    model.removeView(a);
    model.removeView(b);
    a->destroy();
    delete a;
    b->destroy();
    delete b;
}

TEST(Model, SizeDefaultsToZero) {
    Model model;
    EXPECT_EQ(model.size(), 0u);
}

namespace {

class FixedSizeModel : public Model {
public:
    std::size_t count = 0;
    std::size_t size() const override { return count; }
};

}  // namespace

TEST(Model, SizeIsOverridableByASubclass) {
    FixedSizeModel model;
    EXPECT_EQ(model.size(), 0u);

    model.count = 42;
    EXPECT_EQ(model.size(), 42u);
}

// ---------------------------------------------------------------------
// Controller
// ---------------------------------------------------------------------

TEST(Controller, SetModelWiresModelChanged) {
    RecordingController controller;
    Model model;

    controller.setModel(&model);
    EXPECT_EQ(controller.modelChangedCount, 0);

    model.setValue(std::any(42));
    EXPECT_EQ(controller.modelChangedCount, 1);

    model.setValue(std::any(43));
    EXPECT_EQ(controller.modelChangedCount, 2);
}

TEST(Controller, ReplacingModelUnsubscribesFromThePrevious) {
    RecordingController controller;
    Model modelA;
    Model modelB;

    controller.setModel(&modelA);
    controller.setModel(&modelB);

    modelA.setValue(std::any(1));
    EXPECT_EQ(controller.modelChangedCount, 0);

    modelB.setValue(std::any(1));
    EXPECT_EQ(controller.modelChangedCount, 1);
}

// ---------------------------------------------------------------------
// Control - click tracking (see controls.cpp)
// ---------------------------------------------------------------------

TEST(Control, ClickFiresWhenReleaseIsInsideBounds) {
    auto* button = new Button();
    button->setBounds(Rect(0, 0, 100, 30));

    int clickCount = 0;
    button->onClick.add([&clickCount](Control&) {
        ++clickCount;
        return SyncReturn::Handled;
    });

    button->onMouseDown(*button, Point(10, 10), 0, 0);
    button->onMouseUp(*button, Point(20, 20), 0, 0);

    EXPECT_EQ(clickCount, 1);

    button->destroy();
    delete button;
}

TEST(Control, ClickDoesNotFireWhenReleaseIsOutsideBounds) {
    auto* button = new Button();
    button->setBounds(Rect(0, 0, 100, 30));

    int clickCount = 0;
    button->onClick.add([&clickCount](Control&) {
        ++clickCount;
        return SyncReturn::Handled;
    });

    button->onMouseDown(*button, Point(10, 10), 0, 0);
    button->onMouseUp(*button, Point(500, 500), 0, 0);  // released well outside

    EXPECT_EQ(clickCount, 0);

    button->destroy();
    delete button;
}

TEST(Control, ClickDoesNotFireWithoutAPrecedingMouseDown) {
    auto* button = new Button();
    button->setBounds(Rect(0, 0, 100, 30));

    int clickCount = 0;
    button->onClick.add([&clickCount](Control&) {
        ++clickCount;
        return SyncReturn::Handled;
    });

    button->onMouseUp(*button, Point(10, 10), 0, 0);

    EXPECT_EQ(clickCount, 0);

    button->destroy();
    delete button;
}

TEST(Control, ClickDoesNotFireWhenDisabled) {
    auto* button = new Button();
    button->setBounds(Rect(0, 0, 100, 30));
    button->setEnabled(false);

    int clickCount = 0;
    button->onClick.add([&clickCount](Control&) {
        ++clickCount;
        return SyncReturn::Handled;
    });

    button->onMouseDown(*button, Point(10, 10), 0, 0);
    button->onMouseUp(*button, Point(10, 10), 0, 0);

    EXPECT_EQ(clickCount, 0);

    button->destroy();
    delete button;
}

TEST(Control, DisablingMidPressCancelsTracking) {
    auto* button = new Button();
    button->setBounds(Rect(0, 0, 100, 30));

    int clickCount = 0;
    button->onClick.add([&clickCount](Control&) {
        ++clickCount;
        return SyncReturn::Handled;
    });

    button->onMouseDown(*button, Point(10, 10), 0, 0);
    button->setEnabled(false);
    button->setEnabled(true);
    button->onMouseUp(*button, Point(10, 10), 0, 0);

    EXPECT_EQ(clickCount, 0);

    button->destroy();
    delete button;
}

// ---------------------------------------------------------------------
// ViewController - synchronous (no-animation) present()/dismiss() path
// ---------------------------------------------------------------------

TEST(ViewController, ViewIsLazilyBuiltOnFirstAccessOnly) {
    RecordingViewController vc;

    EXPECT_FALSE(vc.isViewLoaded());
    EXPECT_EQ(vc.loadViewCallCount, 0);

    SubView* v1 = vc.view();
    EXPECT_TRUE(vc.isViewLoaded());
    EXPECT_EQ(vc.loadViewCallCount, 1);

    SubView* v2 = vc.view();
    EXPECT_EQ(v1, v2);
    EXPECT_EQ(vc.loadViewCallCount, 1);
}

TEST(ViewController, PresentAttachesViewAndFiresLifecycleInOrder) {
    std::vector<std::string> log;
    RecordingViewController vc;
    vc.log = &log;

    auto* container = new SubView();
    vc.present(container);

    ASSERT_EQ(container->childViews().size(), 1u);
    EXPECT_EQ(container->childViews()[0], vc.view());

    EXPECT_EQ(log, (std::vector<std::string>{"loadView", "viewDidLoad", "viewWillAppear", "viewDidAppear"}));

    vc.dismiss();
    container->destroy();
    delete container;
}

TEST(ViewController, DismissDetachesViewAndFiresLifecycleInOrder) {
    std::vector<std::string> log;
    RecordingViewController vc;
    vc.log = &log;

    auto* container = new SubView();
    vc.present(container);
    log.clear();

    vc.dismiss();

    EXPECT_EQ(container->childViews().size(), 0u);
    EXPECT_EQ(log, (std::vector<std::string>{"viewWillDisappear", "viewDidDisappear"}));

    container->destroy();
    delete container;
}

TEST(ViewController, DismissWithoutPresentIsNoOp) {
    std::vector<std::string> log;
    RecordingViewController vc;
    vc.log = &log;

    vc.dismiss();

    EXPECT_TRUE(log.empty());
}

TEST(ViewController, AddChildControllerSetsContainmentAndAttachesIntoParentsView) {
    RecordingViewController parent;
    auto* child = new RecordingViewController();

    parent.addChildController(child);

    EXPECT_EQ(child->parentController(), &parent);
    ASSERT_EQ(parent.childControllers().size(), 1u);
    EXPECT_EQ(parent.childControllers()[0], child);

    ASSERT_EQ(parent.view()->childViews().size(), 1u);
    EXPECT_EQ(parent.view()->childViews()[0], child->view());

    // Deleting a still-presented child directly (not via dismiss()/
    // removeFromParentController() first) must still unlink it from the
    // parent's childControllers_ - otherwise parent is left holding a
    // dangling pointer.
    delete child;
    EXPECT_TRUE(parent.childControllers().empty());
}

TEST(ViewController, RemoveFromParentControllerClearsContainment) {
    RecordingViewController parent;
    RecordingViewController child;

    parent.addChildController(&child);
    child.removeFromParentController();

    EXPECT_EQ(child.parentController(), nullptr);
    EXPECT_TRUE(parent.childControllers().empty());
    EXPECT_TRUE(parent.view()->childViews().empty());
}

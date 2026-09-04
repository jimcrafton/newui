#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/controls.h"
#include "newui/keyboard_constants.h"

#include <gtest/gtest.h>

// RootView's mouse/keyboard dispatch methods (mouseDown()/mouseMove()/etc.,
// keyEvent()) are protected, not public - purely for testability, so a
// test-local subclass can drive them directly without a real HWND/message
// pump. None of them touch viewHwnd_ (only handleMessage()'s Win32-message
// cases do that - see rootview.cpp), so a RootView constructed with a null
// Frame* and never initialize()'d (same pattern test_view.cpp's
// ViewDestroy.DestroysDirectRootViewChildrenWithoutCorruptingIteration
// already uses) is enough to exercise the real routing logic headlessly.

namespace {

class TestableRootView : public newui::RootView {
public:
    using newui::RootView::RootView;

    using newui::RootView::mouseDown;
    using newui::RootView::mouseMove;
    using newui::RootView::mouseUp;
    using newui::RootView::mouseWheel;
    using newui::RootView::mouseLeft;
    using newui::RootView::mouseDblClick;
    using newui::RootView::gotFocus;
    using newui::RootView::lostFocus;
    using newui::RootView::keyEvent;
    using newui::RootView::cursorTargetAt;
};

// Delegate::FunctionPtr is a plain function pointer (no capturing lambdas),
// so recorders have to be free functions over namespace-scope state - same
// convention test_view.cpp's g_destroyedCount/RecordDestroyed use.

struct RecordedMouseEvent {
    int count = 0;
    void* sender = nullptr;
    newui::Point pt;
    std::uint32_t btnMask = 0;
    std::uint32_t keyMask = 0;
};

RecordedMouseEvent g_downEvent;
RecordedMouseEvent g_moveEvent;
RecordedMouseEvent g_upEvent;
RecordedMouseEvent g_enteredEvent;
RecordedMouseEvent g_leftEvent;
RecordedMouseEvent g_wheelEvent;

void ResetMouseEvents() {
    g_downEvent = {};
    g_moveEvent = {};
    g_upEvent = {};
    g_enteredEvent = {};
    g_leftEvent = {};
    g_wheelEvent = {};
}

newui::SyncReturn RecordDown(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
    g_downEvent = {g_downEvent.count + 1, &sender, pt, btnMask, keyMask};
    return newui::SyncReturn::Handled;
}

newui::SyncReturn RecordMove(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
    g_moveEvent = {g_moveEvent.count + 1, &sender, pt, btnMask, keyMask};
    return newui::SyncReturn::Handled;
}

newui::SyncReturn RecordUp(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
    g_upEvent = {g_upEvent.count + 1, &sender, pt, btnMask, keyMask};
    return newui::SyncReturn::Handled;
}

newui::SyncReturn RecordEntered(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
    g_enteredEvent = {g_enteredEvent.count + 1, &sender, pt, btnMask, keyMask};
    return newui::SyncReturn::Handled;
}

newui::SyncReturn RecordLeft(newui::View& sender, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
    g_leftEvent = {g_leftEvent.count + 1, &sender, pt, btnMask, keyMask};
    return newui::SyncReturn::Handled;
}

newui::SyncReturn RecordWheel(newui::View& sender, const newui::Point& pt, float delta) {
    g_wheelEvent = {g_wheelEvent.count + 1, &sender, pt, 0, 0};
    return newui::SyncReturn::Handled;
}

struct RecordedKeyEvent {
    int count = 0;
    void* sender = nullptr;
    int keyCharVal = 0;
};

RecordedKeyEvent g_keyDownEvent;

void ResetKeyEvents() {
    g_keyDownEvent = {};
}

newui::SyncReturn RecordKeyDown(newui::View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) {
    g_keyDownEvent = {g_keyDownEvent.count + 1, &sender, keyCharVal};
    return newui::SyncReturn::Handled;
}

}  // namespace

TEST(RootViewMouseEvents, MouseDownRoutesToHitChildAndSetsCaptureAndFocus) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->onMouseDown += RecordDown;
    root->addChild(child);

    root->mouseDown(newui::Point(20, 20), 1, 0);

    EXPECT_EQ(g_downEvent.count, 1);
    EXPECT_EQ(g_downEvent.sender, child);
    EXPECT_FLOAT_EQ(g_downEvent.pt.x, 10.0f);
    EXPECT_FLOAT_EQ(g_downEvent.pt.y, 10.0f);
    EXPECT_EQ(root->capturedSubView(), child);
    EXPECT_EQ(root->focusedSubView(), child);

    root->destroy();
    delete root;
}

TEST(RootViewMouseEvents, MouseDownOnEmptyAreaClearsCaptureAndFocus) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    root->mouseDown(newui::Point(20, 20), 1, 0);
    ASSERT_EQ(root->focusedSubView(), child);

    root->mouseDown(newui::Point(150, 150), 1, 0);

    EXPECT_EQ(root->capturedSubView(), nullptr);
    EXPECT_EQ(root->focusedSubView(), nullptr);

    root->destroy();
    delete root;
}

TEST(RootViewMouseEvents, MouseMoveDuringCaptureTargetsCapturedViewEvenOutsideItsBounds) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->onMouseMove += RecordMove;
    root->addChild(child);

    root->mouseDown(newui::Point(20, 20), 1, 0);
    ResetMouseEvents();

    // Far outside child's bounds (10,10,50,50) - still captured, so the
    // event should still reach it, with the point translated as if it
    // were still local to child (140,140 = 150 - child's (10,10) origin).
    root->mouseMove(newui::Point(150, 150), 1, 0);

    EXPECT_EQ(g_moveEvent.count, 1);
    EXPECT_EQ(g_moveEvent.sender, child);
    EXPECT_FLOAT_EQ(g_moveEvent.pt.x, 140.0f);
    EXPECT_FLOAT_EQ(g_moveEvent.pt.y, 140.0f);

    root->destroy();
    delete root;
}

TEST(RootViewMouseEvents, MouseMoveDuringCaptureOfNestedSubViewUsesFullAncestorOffset) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(20, 20, 100, 100));
    container->setVisible(true);
    root->addChild(container);

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(5, 5, 30, 30));  // local to container
    child->setVisible(true);
    child->onMouseMove += RecordMove;
    container->addChild(child);

    // root-local (30,30) -> container-local (10,10) -> child-local (5,5):
    // inside child, so mouseDown captures it.
    root->mouseDown(newui::Point(30, 30), 1, 0);
    ASSERT_EQ(root->capturedSubView(), child);

    // Move far away - still routed to child. Total ancestor offset is
    // container's (20,20) + child's own (5,5) = (25,25).
    root->mouseMove(newui::Point(150, 150), 1, 0);

    EXPECT_EQ(g_moveEvent.sender, child);
    EXPECT_FLOAT_EQ(g_moveEvent.pt.x, 125.0f);
    EXPECT_FLOAT_EQ(g_moveEvent.pt.y, 125.0f);

    root->destroy();
    delete root;
}

TEST(RootViewMouseEvents, MouseMoveWithoutCaptureUpdatesHoverEnterLeaveAndHighlight) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* childA = new newui::SubView();
    childA->setBounds(newui::Rect(0, 0, 50, 50));
    childA->setVisible(true);
    childA->onMouseEntered += RecordEntered;
    childA->onMouseLeft += RecordLeft;
    root->addChild(childA);

    auto* childB = new newui::SubView();
    childB->setBounds(newui::Rect(100, 100, 50, 50));
    childB->setVisible(true);
    childB->onMouseEntered += RecordEntered;
    childB->onMouseLeft += RecordLeft;
    root->addChild(childB);

    root->mouseMove(newui::Point(10, 10), 0, 0);
    EXPECT_EQ(root->hoveredSubView(), childA);
    EXPECT_TRUE(childA->isHighlighted());
    EXPECT_EQ(g_enteredEvent.sender, childA);
    EXPECT_EQ(g_enteredEvent.count, 1);

    root->mouseMove(newui::Point(110, 110), 0, 0);
    EXPECT_EQ(root->hoveredSubView(), childB);
    EXPECT_FALSE(childA->isHighlighted());
    EXPECT_TRUE(childB->isHighlighted());
    EXPECT_EQ(g_leftEvent.sender, childA);
    EXPECT_EQ(g_leftEvent.count, 1);
    EXPECT_EQ(g_enteredEvent.sender, childB);
    EXPECT_EQ(g_enteredEvent.count, 2);

    root->destroy();
    delete root;
}

TEST(RootViewMouseEvents, MouseUpDispatchesToCapturedViewThenClearsCapture) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->onMouseUp += RecordUp;
    root->addChild(child);

    root->mouseDown(newui::Point(20, 20), 1, 0);
    // Inside the window but outside child's bounds - still captured.
    root->mouseUp(newui::Point(190, 190), 1, 0);

    EXPECT_EQ(g_upEvent.count, 1);
    EXPECT_EQ(g_upEvent.sender, child);
    EXPECT_FLOAT_EQ(g_upEvent.pt.x, 180.0f);
    EXPECT_FLOAT_EQ(g_upEvent.pt.y, 180.0f);
    EXPECT_EQ(root->capturedSubView(), nullptr);

    root->destroy();
    delete root;
}

TEST(RootViewMouseEvents, MouseWheelRoutesToHitTestTargetIgnoringCapture) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* childA = new newui::SubView();
    childA->setBounds(newui::Rect(0, 0, 50, 50));
    childA->setVisible(true);
    root->addChild(childA);

    auto* childB = new newui::SubView();
    childB->setBounds(newui::Rect(100, 100, 50, 50));
    childB->setVisible(true);
    childB->onMouseWheel += RecordWheel;
    root->addChild(childB);

    // Capture childA, then wheel over childB - wheel isn't subject to
    // capture, so it should still go to whatever's actually under the
    // cursor (childB), not the captured view (childA).
    root->mouseDown(newui::Point(10, 10), 0, 0);
    root->mouseWheel(newui::Point(110, 110), 120.0f, 0, 0);

    EXPECT_EQ(g_wheelEvent.count, 1);
    EXPECT_EQ(g_wheelEvent.sender, childB);
    EXPECT_FLOAT_EQ(g_wheelEvent.pt.x, 10.0f);
    EXPECT_FLOAT_EQ(g_wheelEvent.pt.y, 10.0f);

    root->destroy();
    delete root;
}

// A wheel event over a leaf with no handler of its own bubbles up through
// its ancestor chain (mirroring accumulatedOffset()'s own per-level walk)
// until one actually handles it - what lets a ScrollView (controls.h)
// catch a wheel event over any of its nested content without that content
// needing to know scrolling exists above it. leaf itself has no
// onMouseWheel subscriber, so this only passes if RootView::mouseWheel()
// actually climbs past it to container.
TEST(RootViewMouseEvents, MouseWheelBubblesToNearestAncestorThatHandlesIt) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(20, 20, 100, 100));
    container->setVisible(true);
    container->onMouseWheel += RecordWheel;
    root->addChild(container);

    auto* leaf = new newui::SubView();
    leaf->setBounds(newui::Rect(5, 5, 30, 30));  // local to container
    leaf->setVisible(true);
    container->addChild(leaf);

    // root-local (30,30) -> container-local (10,10) -> leaf-local (5,5).
    root->mouseWheel(newui::Point(30, 30), 120.0f, 0, 0);

    EXPECT_EQ(g_wheelEvent.count, 1);
    EXPECT_EQ(g_wheelEvent.sender, container);
    // container's own local space - container's (20,20) undone, not
    // leaf's - see handler placement above.
    EXPECT_FLOAT_EQ(g_wheelEvent.pt.x, 10.0f);
    EXPECT_FLOAT_EQ(g_wheelEvent.pt.y, 10.0f);

    root->destroy();
    delete root;
}

// A container whose origin() is nonzero (View::origin() - view.h) shifts
// where its *children* paint/hit-test, not the container's own position -
// mouseDown routing (which walks through hitTestChildren()) has to land on
// the child at its origin-shifted position, and the dispatched localPt has
// to already have that shift undone (matching where the child was actually
// drawn) - both exercised together since they're the same accumulatedOffset()-
// vs-hitTestChildren() relationship a real ScrollView's viewport relies on.
TEST(RootViewMouseEvents, MouseDownAccountsForAncestorOrigin) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* viewport = new newui::SubView();
    viewport->setBounds(newui::Rect(0, 0, 100, 100));
    viewport->setVisible(true);
    root->addChild(viewport);

    auto* content = new newui::SubView();
    content->setBounds(newui::Rect(0, 0, 80, 80));  // content-space, not viewport-local
    content->setVisible(true);
    content->onMouseDown += RecordDown;
    viewport->addChild(content);

    // Scrolled down/right by (40,40): viewport-local (0,0) now shows
    // content-space (40,40) - content's own (0,0)-(80,80) footprint
    // shifts to viewport-local (-40,-40)-(40,40), so only its bottom-
    // right quadrant remains within the viewport's own (0,0)-(100,100)
    // bounds.
    viewport->setOrigin(newui::Point(40.0f, 40.0f));

    // viewport-local (20,20) -> content-space (60,60): still inside
    // content's (0,0)-(80,80) - should hit, with content's own dispatched
    // localPt reflecting content-space (60,60), not the raw click point.
    root->mouseDown(newui::Point(20, 20), 1, 0);
    EXPECT_EQ(g_downEvent.count, 1);
    EXPECT_EQ(g_downEvent.sender, content);
    EXPECT_FLOAT_EQ(g_downEvent.pt.x, 60.0f);
    EXPECT_FLOAT_EQ(g_downEvent.pt.y, 60.0f);
    EXPECT_EQ(root->capturedSubView(), content);

    // viewport-local (90,90) -> content-space (130,130): past content's
    // far edge, so content itself isn't hit (no second RecordDown call) -
    // hitTestChildren() falls back to viewport (the click is still within
    // *its* own bounds, just not within any of its children's), not
    // nullptr - same "no deeper match - this View itself is the target"
    // behavior hitTestChildren() always has.
    root->mouseDown(newui::Point(90, 90), 1, 0);
    EXPECT_EQ(g_downEvent.count, 1);  // unchanged - no second hit on content
    EXPECT_EQ(root->capturedSubView(), viewport);

    root->destroy();
    delete root;
}

TEST(RootViewMouseEvents, MouseLeftClearsHoverAndFiresLeaveButKeepsCapture) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->onMouseLeft += RecordLeft;
    root->addChild(child);

    root->mouseMove(newui::Point(10, 10), 0, 0);
    ASSERT_EQ(root->hoveredSubView(), child);

    root->mouseDown(newui::Point(10, 10), 1, 0);
    ASSERT_EQ(root->capturedSubView(), child);

    root->mouseLeft(newui::Point(-1, -1), 0, 0);

    EXPECT_EQ(root->hoveredSubView(), nullptr);
    EXPECT_EQ(g_leftEvent.sender, child);
    // Capture is a distinct concept from hover - a drag that started on
    // child keeps routing to it even once the cursor leaves the window
    // entirely (see handleMessage()'s SetCapture()).
    EXPECT_EQ(root->capturedSubView(), child);

    root->destroy();
    delete root;
}

TEST(RootViewKeyEvents, KeyEventRoutesToFocusedSubViewAfterRootViewItself) {
    ResetKeyEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->onKeyDown += RecordKeyDown;
    child->onKeyDown += RecordKeyDown;
    root->addChild(child);

    root->mouseDown(newui::Point(10, 10), 1, 0);
    ASSERT_EQ(root->focusedSubView(), child);

    root->keyEvent(newui::keKeyDown, 0, 'A', 1, 65);

    // Both fired (RootView's own delegate first, then focusedSubView_'s -
    // see RootView::keyEvent()), so the final recorded sender is child's.
    EXPECT_EQ(g_keyDownEvent.count, 2);
    EXPECT_EQ(g_keyDownEvent.sender, child);
    EXPECT_EQ(g_keyDownEvent.keyCharVal, 'A');

    root->destroy();
    delete root;
}

TEST(RootViewKeyEvents, KeyEventIsNotRoutedToASubViewWhenNothingIsFocused) {
    ResetKeyEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->onKeyDown += RecordKeyDown;
    root->addChild(child);

    ASSERT_EQ(root->focusedSubView(), nullptr);

    root->keyEvent(newui::keKeyDown, 0, 'A', 1, 65);

    EXPECT_EQ(g_keyDownEvent.count, 0);

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// setFocusedSubView() veto hooks (canResignFocus()/canBecomeFocused()) and
// the canPerformCommand()/performCommand() responder-chain walk.
// ---------------------------------------------------------------------------

namespace {

class VetoableSubView : public newui::SubView {
public:
    bool allowResign = true;
    bool allowBecome = true;

    bool canResignFocus() const override { return allowResign; }
    bool canBecomeFocused() const override { return allowBecome; }
};

class CommandAnsweringSubView : public newui::SubView {
public:
    explicit CommandAnsweringSubView(newui::CommandId id) : handledId_(std::move(id)) {}

    int performCount = 0;

    bool canPerformCommand(const newui::CommandId& cmd) const override {
        return cmd == handledId_;
    }
    void performCommand(const newui::CommandId& cmd) override {
        if (cmd == handledId_) {
            ++performCount;
        }
    }

private:
    newui::CommandId handledId_;
};

}  // namespace

TEST(RootViewFocusTransfer, VetoingResignFocusLeavesFocusedSubViewUnchanged) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new VetoableSubView();
    first->setBounds(newui::Rect(0, 0, 50, 50));
    first->setVisible(true);
    root->addChild(first);

    auto* second = new newui::SubView();
    second->setBounds(newui::Rect(60, 0, 50, 50));
    second->setVisible(true);
    root->addChild(second);

    root->setFocusedSubView(first);
    ASSERT_EQ(root->focusedSubView(), first);

    first->allowResign = false;
    root->setFocusedSubView(second);

    EXPECT_EQ(root->focusedSubView(), first);

    root->destroy();
    delete root;
}

TEST(RootViewFocusTransfer, VetoingBecomeFocusedLeavesFocusedSubViewUnchanged) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new newui::SubView();
    first->setBounds(newui::Rect(0, 0, 50, 50));
    first->setVisible(true);
    root->addChild(first);

    auto* second = new VetoableSubView();
    second->setBounds(newui::Rect(60, 0, 50, 50));
    second->setVisible(true);
    second->allowBecome = false;
    root->addChild(second);

    root->setFocusedSubView(first);
    ASSERT_EQ(root->focusedSubView(), first);

    root->setFocusedSubView(second);

    EXPECT_EQ(root->focusedSubView(), first);

    root->destroy();
    delete root;
}

TEST(RootViewFocusTransfer, AllowingBothTransfersFocusAndFiresEvents) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new VetoableSubView();
    first->setBounds(newui::Rect(0, 0, 50, 50));
    first->setVisible(true);
    root->addChild(first);

    auto* second = new VetoableSubView();
    second->setBounds(newui::Rect(60, 0, 50, 50));
    second->setVisible(true);
    root->addChild(second);

    root->setFocusedSubView(first);

    int lostCount = 0;
    int gotCount = 0;
    first->onLostFocus.add([&lostCount](newui::View&) { ++lostCount; return newui::SyncReturn::Handled; });
    second->onGotFocus.add([&gotCount](newui::View&) { ++gotCount; return newui::SyncReturn::Handled; });

    root->setFocusedSubView(second);

    EXPECT_EQ(root->focusedSubView(), second);
    EXPECT_EQ(lostCount, 1);
    EXPECT_EQ(gotCount, 1);

    root->destroy();
    delete root;
}

TEST(RootViewCommandDispatch, CanPerformCommandChecksFocusedSubViewFirst) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new CommandAnsweringSubView(newui::commands::copy);
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    root->setFocusedSubView(child);

    EXPECT_TRUE(root->canPerformCommand(newui::commands::copy));
    EXPECT_FALSE(root->canPerformCommand(newui::commands::paste));

    root->destroy();
    delete root;
}

TEST(RootViewCommandDispatch, WalksUpParentChainWhenFocusedSubViewDoesNotHandleIt) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* parent = new CommandAnsweringSubView(newui::commands::copy);
    parent->setBounds(newui::Rect(0, 0, 100, 100));
    parent->setVisible(true);
    root->addChild(parent);

    auto* child = new newui::SubView();  // doesn't answer anything itself
    child->setBounds(newui::Rect(0, 0, 20, 20));
    child->setVisible(true);
    parent->addChild(child);

    root->setFocusedSubView(child);

    EXPECT_TRUE(root->canPerformCommand(newui::commands::copy));

    root->performCommand(newui::commands::copy);
    EXPECT_EQ(parent->performCount, 1);

    root->destroy();
    delete root;
}

TEST(RootViewCommandDispatch, PerformCommandIsANoOpWhenNothingInTheChainHandlesIt) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    root->setFocusedSubView(child);

    EXPECT_FALSE(root->canPerformCommand(newui::commands::copy));
    root->performCommand(newui::commands::copy);  // must not throw or crash

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// cursorTargetAt() - drives handleMessage()'s WM_SETCURSOR case (a real
// HWND is needed to test that message handler end-to-end, so this only
// covers the pure "which View's cursor applies here" logic it delegates
// to - see rootview.h's comment on cursorTargetAt()).
// ---------------------------------------------------------------------------

TEST(RootViewCursor, ReturnsRootViewItselfWhenNothingIsHitAndNothingIsCaptured) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    EXPECT_EQ(root->cursorTargetAt(newui::Point(5, 5)), root);

    root->destroy();
    delete root;
}

TEST(RootViewCursor, ReturnsHitChildWhenNothingIsCaptured) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    EXPECT_EQ(root->cursorTargetAt(newui::Point(20, 20)), child);

    root->destroy();
    delete root;
}

TEST(RootViewCursor, CapturedViewWinsEvenWhenPointIsOutsideItsBounds) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    root->mouseDown(newui::Point(20, 20), 1, 0);
    ASSERT_EQ(root->capturedSubView(), child);

    // Far outside child's bounds - capture still wins over a fresh hit-test
    // (which would otherwise return root itself here).
    EXPECT_EQ(root->cursorTargetAt(newui::Point(150, 150)), child);

    root->destroy();
    delete root;
}

TEST(RootViewSubViewRemoval, RemovingDirectChildClearsHoverCaptureFocus) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    root->mouseMove(newui::Point(10, 10), 0, 0);
    root->mouseDown(newui::Point(10, 10), 1, 0);
    ASSERT_EQ(root->hoveredSubView(), child);
    ASSERT_EQ(root->capturedSubView(), child);
    ASSERT_EQ(root->focusedSubView(), child);

    root->removeChild(child);

    EXPECT_EQ(root->hoveredSubView(), nullptr);
    EXPECT_EQ(root->capturedSubView(), nullptr);
    EXPECT_EQ(root->focusedSubView(), nullptr);

    delete child;
    root->destroy();
    delete root;
}

TEST(RootViewSubViewRemoval, RemovingNestedGrandchildClearsHoverCaptureFocus) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 100));
    container->setVisible(true);
    root->addChild(container);

    auto* grandchild = new newui::SubView();
    grandchild->setBounds(newui::Rect(0, 0, 50, 50));
    grandchild->setVisible(true);
    container->addChild(grandchild);

    root->mouseMove(newui::Point(10, 10), 0, 0);
    root->mouseDown(newui::Point(10, 10), 1, 0);
    ASSERT_EQ(root->hoveredSubView(), grandchild);
    ASSERT_EQ(root->capturedSubView(), grandchild);
    ASSERT_EQ(root->focusedSubView(), grandchild);

    // Removed via the nested SubView::removeChild() path, not
    // RootView::removeChild() directly - the gap notifySubViewRemoved()'s
    // wiring in subview.cpp specifically covers.
    container->removeChild(grandchild);

    EXPECT_EQ(root->hoveredSubView(), nullptr);
    EXPECT_EQ(root->capturedSubView(), nullptr);
    EXPECT_EQ(root->focusedSubView(), nullptr);

    delete grandchild;
    root->destroy();
    delete root;
}

TEST(RootViewDefaultNaming, FirstButtonAttachedGetsNameButton1) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* button = new newui::Button();
    EXPECT_EQ(button->name(), "");

    root->addChild(button);

    EXPECT_EQ(button->name(), "button1");

    root->destroy();
    delete root;
}

TEST(RootViewDefaultNaming, SecondButtonAttachedGetsNameButton2) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new newui::Button();
    auto* second = new newui::Button();
    root->addChild(first);
    root->addChild(second);

    EXPECT_EQ(first->name(), "button1");
    EXPECT_EQ(second->name(), "button2");

    root->destroy();
    delete root;
}

TEST(RootViewDefaultNaming, LeavesAHandAssignedNameAlone) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* button = new newui::Button();
    button->setName("myButton");

    root->addChild(button);

    EXPECT_EQ(button->name(), "myButton");

    root->destroy();
    delete root;
}

TEST(RootViewDefaultNaming, SkipsOverAHandAssignedNameThatWouldCollide) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    // Hand-assigned before attachment - propagateRootView() must reserve
    // it (View::propagateRootView(), view.cpp) so the next auto-named
    // Button doesn't collide with it once attached.
    auto* preNamed = new newui::Button();
    preNamed->setName("button1");
    root->addChild(preNamed);

    auto* autoNamed = new newui::Button();
    root->addChild(autoNamed);

    EXPECT_EQ(preNamed->name(), "button1");
    EXPECT_EQ(autoNamed->name(), "button2");

    root->destroy();
    delete root;
}

TEST(RootViewDefaultNaming, NestedSubtreeGetsNamedOnAttachment) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* row = new newui::SubView();
    auto* button = new newui::Button();
    row->addChild(button);  // pre-built, not yet rooted

    EXPECT_EQ(row->name(), "");
    EXPECT_EQ(button->name(), "");

    root->addChild(row);

    EXPECT_EQ(row->name(), "subView1");
    EXPECT_EQ(button->name(), "button1");

    root->destroy();
    delete root;
}

TEST(ViewFindView, FindsAnImmediateChildByName) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* button = new newui::Button();
    root->addChild(button);

    EXPECT_EQ(root->findView("button1"), button);

    root->destroy();
    delete root;
}

TEST(ViewFindView, FindsANestedDescendantByName) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* row = new newui::SubView();
    root->addChild(row);
    auto* button = new newui::Button();
    row->addChild(button);

    EXPECT_EQ(root->findView("button1"), button);

    root->destroy();
    delete root;
}

TEST(ViewFindView, ReturnsNullWhenNoMatchExists) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* button = new newui::Button();
    root->addChild(button);

    EXPECT_EQ(root->findView("noSuchView"), nullptr);

    root->destroy();
    delete root;
}

TEST(ViewIsDesignTime, DefaultsFalseOnAFreshRootView) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    EXPECT_FALSE(root->isDesignTime());

    root->destroy();
    delete root;
}

TEST(ViewIsDesignTime, RootViewReflectsItsOwnSetDesignTime) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    root->setDesignTime(true);
    EXPECT_TRUE(root->isDesignTime());

    root->setDesignTime(false);
    EXPECT_FALSE(root->isDesignTime());

    root->destroy();
    delete root;
}

TEST(ViewIsDesignTime, ChildViewDefersToItsRootViewsFlag) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    auto* button = new newui::Button();
    root->addChild(button);

    EXPECT_FALSE(button->isDesignTime());
    root->setDesignTime(true);
    EXPECT_TRUE(button->isDesignTime());

    root->destroy();
    delete root;
}

TEST(ViewIsDesignTime, NestedDescendantDefersToTheSameRootViewsFlag) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    auto* row = new newui::SubView();
    root->addChild(row);
    auto* button = new newui::Button();
    row->addChild(button);

    root->setDesignTime(true);
    EXPECT_TRUE(button->isDesignTime());

    root->destroy();
    delete root;
}

TEST(ViewIsDesignTime, DefaultsFalseOnAViewNotAttachedToAnyRootView) {
    auto* button = new newui::Button();

    EXPECT_FALSE(button->isDesignTime());

    delete button;
}

TEST(RootViewDefaultNaming, ChildAddedToAnAlreadyRootedContainerGetsNamed) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* row = new newui::SubView();
    root->addChild(row);  // row rooted first, while still empty

    auto* button = new newui::Button();
    row->addChild(button);  // then a child added to the already-rooted row

    EXPECT_EQ(row->name(), "subView1");
    EXPECT_EQ(button->name(), "button1");

    root->destroy();
    delete root;
}

// RootView's standalone (Frame-less) constructor - for hosting a RootView
// directly inside a parent HWND some other process/toolkit owns, used
// with a caller-owned RunLoop rather than Application/Frame. Needs a real
// throwaway top-level window to stand in for that external parent - same
// technique test_runloop.cpp's RunLoopRunModal tests already use to
// exercise real Win32 window relationships headlessly, with no actual
// newui::Frame/Application involved.
TEST(RootViewStandaloneConstruction, InitializeSucceedsAgainstAnExternalParentHwnd) {
    HINSTANCE moduleHandle = ::GetModuleHandleA(nullptr);
    HWND externalParent = ::CreateWindowExA(0, "STATIC", "", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, moduleHandle, nullptr);
    ASSERT_NE(externalParent, nullptr);

    newui::RootView root(externalParent, moduleHandle, newui::Rect(0, 0, 100, 100), "standaloneRoot");

    // No Frame - getFrame() must stay null (the already-safe, tested case
    // MenuBar's own popup code and Bundle's Frame-based loadRootView()
    // overload both already null-check).
    EXPECT_EQ(root.getFrame(), nullptr);

    ASSERT_TRUE(root.initialize());
    EXPECT_NE(root.windowHandle(), nullptr);
    EXPECT_EQ(::GetParent(root.windowHandle()), externalParent);

    root.destroy();
    ::DestroyWindow(externalParent);
}

TEST(RootViewStandaloneConstruction, InitializeFailsWithNoParentAtAll) {
    newui::RootView root(nullptr, nullptr, newui::Rect(0, 0, 100, 100), "standaloneRootNoParent");

    EXPECT_FALSE(root.initialize());
    EXPECT_EQ(root.windowHandle(), nullptr);
}

#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/controls.h"
#include "newui/keyboard_constants.h"
#include "newui/uiinputmanager.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
    using newui::RootView::dirtyRect;
    using newui::RootView::repaint;
    using newui::RootView::repaintNow;
    using newui::RootView::setRepaintMode;
    using newui::RootView::setVerifyRepaint;
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
    child->setAcceptsFocus(true);
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
    child->setAcceptsFocus(true);
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
    child->setAcceptsFocus(true);
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
// acceptsFocus()/UIInputManager - mouse-click focus resolution (item 1) and
// Tab/Shift+Tab navigation (item 3). A plain SubView defaults to
// acceptsFocus(false) (view.h) - these cover the click and Tab policy that
// default drives, on top of what the RootViewMouseEvents/RootViewKeyEvents
// tests above already establish for capture/dispatch.
// ---------------------------------------------------------------------------

TEST(RootViewFocusPolicy, MouseDownOnAPlainSubViewCapturesButDoesNotChangeFocus) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    // Default-constructed SubView: acceptsFocus() is false, so this click
    // shouldn't hand it keyboard focus, even though it's still the exact
    // View the mouse hit (and therefore still gets capture/onMouseDown).
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->onMouseDown += RecordDown;
    root->addChild(child);

    root->mouseDown(newui::Point(20, 20), 1, 0);

    EXPECT_EQ(g_downEvent.count, 1);
    EXPECT_EQ(g_downEvent.sender, child);
    EXPECT_EQ(root->capturedSubView(), child);
    EXPECT_EQ(root->focusedSubView(), nullptr);

    root->destroy();
    delete root;
}

TEST(RootViewFocusPolicy, MouseDownOnANonFocusableChildWalksUpToItsFocusableAncestor) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    // e.g. Button's own drawn label: the exact hit target is a plain,
    // non-focusable decoration, but its Control-like ancestor still opts
    // in via acceptsFocus(true) - a click anywhere inside should focus
    // that ancestor, not leave focus cleared.
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 100));
    container->setVisible(true);
    container->setAcceptsFocus(true);
    root->addChild(container);

    auto* label = new newui::SubView();
    label->setBounds(newui::Rect(10, 10, 30, 30));  // local to container
    label->setVisible(true);
    label->onMouseDown += RecordDown;
    container->addChild(label);

    root->mouseDown(newui::Point(20, 20), 1, 0);

    EXPECT_EQ(g_downEvent.count, 1);
    EXPECT_EQ(g_downEvent.sender, label);
    // Capture still targets the exact hit View...
    EXPECT_EQ(root->capturedSubView(), label);
    // ...but focus lands on the nearest ancestor that actually wants it.
    EXPECT_EQ(root->focusedSubView(), container);

    root->destroy();
    delete root;
}

TEST(RootViewFocusPolicy, TabIsNotForwardedAsAnOrdinaryKeyEvent) {
    ResetKeyEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->setAcceptsFocus(true);
    root->onKeyDown += RecordKeyDown;
    child->onKeyDown += RecordKeyDown;
    root->addChild(child);
    root->setFocusedSubView(child);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);

    // Tab is consumed entirely by UIInputManager's focus navigation -
    // neither this RootView's own onKeyDown nor focusedSubView_'s ever
    // sees it, unlike an ordinary key (see
    // KeyEventRoutesToFocusedSubViewAfterRootViewItself above).
    EXPECT_EQ(g_keyDownEvent.count, 0);

    root->destroy();
    delete root;
}

TEST(RootViewFocusPolicy, TabMovesFocusToNextFocusableViewInReadingOrderAndWraps) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    // Added out of reading order (rightmost first) - the Tab order below
    // has to come from geometry, not addChild() order.
    auto* right = new newui::SubView();
    right->setBounds(newui::Rect(100, 0, 50, 50));
    right->setVisible(true);
    right->setAcceptsFocus(true);
    root->addChild(right);

    auto* left = new newui::SubView();
    left->setBounds(newui::Rect(0, 0, 50, 50));
    left->setVisible(true);
    left->setAcceptsFocus(true);
    root->addChild(left);

    ASSERT_EQ(root->focusedSubView(), nullptr);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), left);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), right);

    // Wraps back around to the first candidate.
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), left);

    root->destroy();
    delete root;
}

TEST(RootViewFocusPolicy, ShiftTabMovesFocusToPreviousFocusableViewAndWraps) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* left = new newui::SubView();
    left->setBounds(newui::Rect(0, 0, 50, 50));
    left->setVisible(true);
    left->setAcceptsFocus(true);
    root->addChild(left);

    auto* right = new newui::SubView();
    right->setBounds(newui::Rect(100, 0, 50, 50));
    right->setVisible(true);
    right->setAcceptsFocus(true);
    root->addChild(right);

    ASSERT_EQ(root->focusedSubView(), nullptr);

    // Shift+Tab with nothing focused yet starts from the last candidate.
    root->keyEvent(newui::keKeyDown, newui::kmShift, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), right);

    root->keyEvent(newui::keKeyDown, newui::kmShift, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), left);

    // Wraps back around to the last candidate.
    root->keyEvent(newui::keKeyDown, newui::kmShift, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), right);

    root->destroy();
    delete root;
}

TEST(RootViewFocusPolicy, TabSkipsNonFocusableViewsAndDisabledControls) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new newui::Button();
    first->setBounds(newui::Rect(0, 0, 50, 30));
    first->setVisible(true);
    root->addChild(first);

    // A plain decorative SubView between the two Buttons - never a tab
    // stop regardless of position.
    auto* decoration = new newui::SubView();
    decoration->setBounds(newui::Rect(60, 0, 20, 30));
    decoration->setVisible(true);
    root->addChild(decoration);

    // Disabled - Control::canBecomeFocused() excludes it even though
    // Button::Button() sets acceptsFocus(true).
    auto* disabledButton = new newui::Button();
    disabledButton->setBounds(newui::Rect(90, 0, 50, 30));
    disabledButton->setVisible(true);
    disabledButton->setEnabled(false);
    root->addChild(disabledButton);

    auto* last = new newui::Button();
    last->setBounds(newui::Rect(150, 0, 50, 30));
    last->setVisible(true);
    root->addChild(last);

    root->setFocusedSubView(first);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);

    EXPECT_EQ(root->focusedSubView(), last);

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// isFocusScope()/wantsTabKey() (view.h) - UIInputManager::moveFocus()'s
// scoped Tab cycling ("Scoped Geometric Hierarchy" -
// "UIInputManager possible implementation notes.docx") and RootView::
// keyEvent()'s opt-out from Tab interception entirely, respectively.
// ---------------------------------------------------------------------------

TEST(RootViewFocusScope, TabStaysWithinAFocusScopeAndWraps) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* outerButton = new newui::SubView();
    outerButton->setBounds(newui::Rect(0, 0, 50, 30));
    outerButton->setVisible(true);
    outerButton->setAcceptsFocus(true);
    root->addChild(outerButton);

    auto* panel = new newui::SubView();
    panel->setBounds(newui::Rect(0, 40, 200, 100));
    panel->setVisible(true);
    panel->setAcceptsFocus(true);
    panel->setFocusScope(true);
    root->addChild(panel);

    auto* panelChild1 = new newui::SubView();
    panelChild1->setBounds(newui::Rect(0, 0, 50, 30));
    panelChild1->setVisible(true);
    panelChild1->setAcceptsFocus(true);
    panel->addChild(panelChild1);

    auto* panelChild2 = new newui::SubView();
    panelChild2->setBounds(newui::Rect(60, 0, 50, 30));
    panelChild2->setVisible(true);
    panelChild2->setAcceptsFocus(true);
    panel->addChild(panelChild2);

    root->setFocusedSubView(panelChild1);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), panelChild2);

    // Wraps back to panelChild1 - never escapes to outerButton or the
    // panel container itself, even though both are geometrically/tree-
    // wise reachable from panelChild2.
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), panelChild1);

    root->destroy();
    delete root;
}

TEST(RootViewFocusScope, ShiftTabWithinAFocusScopeWrapsBackward) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* panel = new newui::SubView();
    panel->setBounds(newui::Rect(0, 0, 200, 100));
    panel->setVisible(true);
    panel->setAcceptsFocus(true);
    panel->setFocusScope(true);
    root->addChild(panel);

    auto* panelChild1 = new newui::SubView();
    panelChild1->setBounds(newui::Rect(0, 0, 50, 30));
    panelChild1->setVisible(true);
    panelChild1->setAcceptsFocus(true);
    panel->addChild(panelChild1);

    auto* panelChild2 = new newui::SubView();
    panelChild2->setBounds(newui::Rect(60, 0, 50, 30));
    panelChild2->setVisible(true);
    panelChild2->setAcceptsFocus(true);
    panel->addChild(panelChild2);

    root->setFocusedSubView(panelChild1);

    root->keyEvent(newui::keKeyDown, newui::kmShift, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), panelChild2);

    root->destroy();
    delete root;
}

TEST(RootViewFocusScope, TabFromOutsideLandsOnTheScopeContainerThenEntersItOnTheNextPress) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* outerButton = new newui::SubView();
    outerButton->setBounds(newui::Rect(0, 0, 50, 30));
    outerButton->setVisible(true);
    outerButton->setAcceptsFocus(true);
    root->addChild(outerButton);

    // A focus scope that also opts into acceptsFocus() itself - the
    // "door" a Tab from outside can actually land on (see moveFocus()'s
    // own doc comment, uiinputmanager.h, on why a non-focusable scope
    // can't work as one).
    auto* panel = new newui::SubView();
    panel->setBounds(newui::Rect(0, 40, 200, 100));
    panel->setVisible(true);
    panel->setAcceptsFocus(true);
    panel->setFocusScope(true);
    root->addChild(panel);

    auto* panelChild = new newui::SubView();
    panelChild->setBounds(newui::Rect(0, 0, 50, 30));
    panelChild->setVisible(true);
    panelChild->setAcceptsFocus(true);
    panel->addChild(panelChild);

    root->setFocusedSubView(outerButton);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), panel)
        << "the scope container itself is one opaque stop from outside, not its children directly";

    // Now that the panel itself is focused, findActiveScope() resolves to
    // the panel (it's isFocusScope() and it's now `current`), so this next
    // Tab enters it instead of moving to whatever's after it at the outer
    // level.
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), panelChild);

    root->destroy();
    delete root;
}

TEST(RootViewFocusScope, ANonFocusableScopeIsSkippedFromOutsideRatherThanFreezingTab) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* outerButton = new newui::SubView();
    outerButton->setBounds(newui::Rect(0, 0, 50, 30));
    outerButton->setVisible(true);
    outerButton->setAcceptsFocus(true);
    root->addChild(outerButton);

    // isFocusScope() but never opted into acceptsFocus() - can never
    // itself receive focus, so it must not appear as a dead Tab stop that
    // leaves focus stuck once reached.
    auto* panel = new newui::SubView();
    panel->setBounds(newui::Rect(0, 40, 200, 100));
    panel->setVisible(true);
    panel->setFocusScope(true);
    root->addChild(panel);

    auto* panelChild = new newui::SubView();
    panelChild->setBounds(newui::Rect(0, 0, 50, 30));
    panelChild->setVisible(true);
    panelChild->setAcceptsFocus(true);
    panel->addChild(panelChild);

    auto* afterPanel = new newui::SubView();
    afterPanel->setBounds(newui::Rect(0, 150, 50, 30));
    afterPanel->setVisible(true);
    afterPanel->setAcceptsFocus(true);
    root->addChild(afterPanel);

    root->setFocusedSubView(outerButton);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), afterPanel)
        << "a non-focusable scope's own children stay unreachable from outside via Tab, "
           "but the scope itself must not be a dead stop that freezes on";

    root->destroy();
    delete root;
}

TEST(RootViewFocusScope, NestedFocusScopeIsASingleStopInsideItsParentScope) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* outerScope = new newui::SubView();
    outerScope->setBounds(newui::Rect(0, 0, 200, 200));
    outerScope->setVisible(true);
    outerScope->setAcceptsFocus(true);
    outerScope->setFocusScope(true);
    root->addChild(outerScope);

    auto* outerChild = new newui::SubView();
    outerChild->setBounds(newui::Rect(0, 0, 50, 30));
    outerChild->setVisible(true);
    outerChild->setAcceptsFocus(true);
    outerScope->addChild(outerChild);

    auto* innerScope = new newui::SubView();
    innerScope->setBounds(newui::Rect(60, 0, 100, 100));
    innerScope->setVisible(true);
    innerScope->setAcceptsFocus(true);
    innerScope->setFocusScope(true);
    outerScope->addChild(innerScope);

    auto* innerChild = new newui::SubView();
    innerChild->setBounds(newui::Rect(0, 0, 50, 30));
    innerChild->setVisible(true);
    innerChild->setAcceptsFocus(true);
    innerScope->addChild(innerChild);

    root->setFocusedSubView(outerChild);

    // Cycling the outer scope reaches innerScope as one stop - innerChild
    // is never mixed into this outer cycle.
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), innerScope);

    // Now inside innerScope (it's isFocusScope() and it's `current`) -
    // this Tab enters it instead of returning to outerChild.
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);
    EXPECT_EQ(root->focusedSubView(), innerChild);

    root->destroy();
    delete root;
}

TEST(RootViewFocusScope, WantsTabKeyPreventsTabInterceptionEntirely) {
    ResetKeyEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* editor = new newui::SubView();
    editor->setBounds(newui::Rect(0, 0, 50, 30));
    editor->setVisible(true);
    editor->setAcceptsFocus(true);
    editor->setWantsTabKey(true);
    editor->onKeyDown += RecordKeyDown;
    root->addChild(editor);

    auto* other = new newui::SubView();
    other->setBounds(newui::Rect(60, 0, 50, 30));
    other->setVisible(true);
    other->setAcceptsFocus(true);
    root->addChild(other);

    root->setFocusedSubView(editor);

    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);

    // Never intercepted for navigation - falls through to ordinary
    // dispatch instead, unlike TabIsNotForwardedAsAnOrdinaryKeyEvent above.
    EXPECT_EQ(root->focusedSubView(), editor);
    EXPECT_EQ(g_keyDownEvent.count, 1);
    EXPECT_EQ(g_keyDownEvent.sender, editor);

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// newui::FocusGuide (uiinputmanager.h) - UIKit's UIFocusGuide adapted to
// this framework's list-based Tab order: a position-anchored placeholder
// that redirects to a real View, instead of a numeric priority that can
// silently drift out of sync as a layout changes.
// ---------------------------------------------------------------------------

TEST(RootViewFocusGuide, ForcesZeroSizeRegardlessOfWhatSetBoundsIsGiven) {
    newui::FocusGuide guide;

    guide.setBounds(newui::Rect(10.0f, 20.0f, 100.0f, 50.0f));

    EXPECT_FLOAT_EQ(guide.bounds().pos().x, 10.0f);
    EXPECT_FLOAT_EQ(guide.bounds().pos().y, 20.0f);
    EXPECT_FLOAT_EQ(guide.bounds().size().width, 0.0f);
    EXPECT_FLOAT_EQ(guide.bounds().size().height, 0.0f);
}

TEST(RootViewFocusGuide, TabRedirectsThroughToTheGuidesTargetNotTheGuideItself) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new newui::SubView();
    first->setBounds(newui::Rect(0, 0, 50, 30));
    first->setVisible(true);
    first->setAcceptsFocus(true);
    root->addChild(first);

    // Would be geometrically next after `first` in reading order, but the
    // guide below sits ahead of it (x=30, between `first`'s x=0 and this
    // one's x=60) and intercepts Tab first, redirecting straight past it
    // to `target` instead.
    auto* geometricallyNext = new newui::SubView();
    geometricallyNext->setBounds(newui::Rect(60, 0, 50, 30));
    geometricallyNext->setVisible(true);
    geometricallyNext->setAcceptsFocus(true);
    root->addChild(geometricallyNext);

    auto* target = new newui::SubView();
    target->setBounds(newui::Rect(0, 150, 50, 30));  // geometrically last
    target->setVisible(true);
    target->setAcceptsFocus(true);
    root->addChild(target);

    auto* guide = new newui::FocusGuide();
    guide->setBounds(newui::Rect(30, 0, 0, 0));  // between first and geometricallyNext
    guide->setVisible(true);
    guide->setRedirectTarget(target);
    root->addChild(guide);

    root->setFocusedSubView(first);
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);

    EXPECT_EQ(root->focusedSubView(), target)
        << "the guide must redirect through to its target, never become focusedSubView() itself";

    root->destroy();
    delete root;
}

TEST(RootViewFocusGuide, ShiftTabRedirectsThroughAGuideTheSameWay) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* target = new newui::SubView();
    target->setBounds(newui::Rect(0, 0, 50, 30));
    target->setVisible(true);
    target->setAcceptsFocus(true);
    root->addChild(target);

    auto* current = new newui::SubView();
    current->setBounds(newui::Rect(0, 150, 50, 30));  // geometrically last
    current->setVisible(true);
    current->setAcceptsFocus(true);
    root->addChild(current);

    auto* guide = new newui::FocusGuide();
    guide->setBounds(newui::Rect(0, 75, 0, 0));  // between them in reading order
    guide->setVisible(true);
    guide->setRedirectTarget(target);
    root->addChild(guide);

    root->setFocusedSubView(current);
    root->keyEvent(newui::keKeyDown, newui::kmShift, '\t', 1, newui::vkTab);

    EXPECT_EQ(root->focusedSubView(), target);

    root->destroy();
    delete root;
}

TEST(RootViewFocusGuide, AGuideWithNoRedirectTargetIsANoOp) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new newui::SubView();
    first->setBounds(newui::Rect(0, 0, 50, 30));
    first->setVisible(true);
    first->setAcceptsFocus(true);
    root->addChild(first);

    auto* guide = new newui::FocusGuide();  // redirectTarget() never set
    guide->setBounds(newui::Rect(60, 0, 0, 0));
    guide->setVisible(true);
    root->addChild(guide);

    root->setFocusedSubView(first);
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);

    EXPECT_EQ(root->focusedSubView(), first) << "must not crash or focus the guide itself";

    root->destroy();
    delete root;
}

TEST(RootViewFocusGuide, ACyclicGuideChainIsANoOpRatherThanCrashing) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new newui::SubView();
    first->setBounds(newui::Rect(0, 0, 50, 30));
    first->setVisible(true);
    first->setAcceptsFocus(true);
    root->addChild(first);

    auto* guideA = new newui::FocusGuide();
    guideA->setBounds(newui::Rect(60, 0, 0, 0));
    guideA->setVisible(true);
    root->addChild(guideA);

    auto* guideB = new newui::FocusGuide();
    guideB->setBounds(newui::Rect(70, 0, 0, 0));
    guideB->setVisible(true);
    root->addChild(guideB);

    guideA->setRedirectTarget(guideB);
    guideB->setRedirectTarget(guideA);

    root->setFocusedSubView(first);
    root->keyEvent(newui::keKeyDown, 0, '\t', 1, newui::vkTab);  // must not infinite-loop or crash

    EXPECT_EQ(root->focusedSubView(), first);

    root->destroy();
    delete root;
}

TEST(RootViewFocusGuide, MouseClickResolvesThroughAGuideTooViaResolveClickFocusTarget) {
    // A FocusGuide's forced (0,0) size makes it unhittable on its own -
    // this exercises UIInputManager::resolveClickFocusTarget()'s own
    // redirect resolution directly instead, the same defensive path a
    // real click would only ever reach if something were (unusually)
    // nested inside a guide.
    auto* guide = new newui::FocusGuide();
    auto* target = new newui::SubView();
    target->setAcceptsFocus(true);
    guide->setRedirectTarget(target);

    newui::SubView* resolved = newui::UIInputManager::instance().resolveClickFocusTarget(guide);

    EXPECT_EQ(resolved, target);

    delete guide;
    delete target;
}

// ---------------------------------------------------------------------------
// Arrow-key cross-control spatial jump: UIInputManager::moveFocusSpatially()
// (called via routeArrowKeyDown(), in turn called from RootView::keyEvent()
// for an arrow key the focused View's own onKeyDown doesn't handle) moves
// focus to the nearest focusable View actually positioned in the requested
// direction - real accumulatedOffset()-based geometry, edge-filtered (not
// just "closer by raw distance"), scope-respecting and FocusGuide-aware the
// same way moveFocus() (Tab) already is. See uiinputmanager.h's own doc
// comments on both methods for the full design, adapted from
// uiinputmanager-plan.md's ArrowResult::BoundaryReached/
// HandleArrowKeyPressed.
// ---------------------------------------------------------------------------

namespace {

class StubListRowModel : public newui::ListModel {
public:
    std::size_t rowCount = 0;

    std::any value(const std::any& /*key*/) override { return std::any(); }
    std::size_t size() const override { return rowCount; }
};

}  // namespace

TEST(RootViewArrowKeySpatialJump, DownJumpsToTheNearestFocusableViewBelow) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 300), "root");

    auto* top = new newui::SubView();
    top->setBounds(newui::Rect(0, 0, 50, 50));
    top->setVisible(true);
    top->setAcceptsFocus(true);
    root->addChild(top);

    auto* below = new newui::SubView();
    below->setBounds(newui::Rect(0, 100, 50, 50));
    below->setVisible(true);
    below->setAcceptsFocus(true);
    root->addChild(below);

    root->setFocusedSubView(top);
    ASSERT_TRUE(newui::UIInputManager::instance().moveFocusSpatially(*root, newui::SpatialDirection::Down));

    EXPECT_EQ(root->focusedSubView(), below);

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, EdgeFilterExcludesACandidateNotActuallyBelowEvenIfNumericallyCloser) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 300, 300), "root");

    auto* current = new newui::SubView();
    current->setBounds(newui::Rect(0, 0, 50, 50));  // bottom edge at y=50
    current->setVisible(true);
    current->setAcceptsFocus(true);
    root->addChild(current);

    // Same row, off to the side - raw center-to-center distance (60px) is
    // closer than belowCandidate's (100px), but it overlaps current's own
    // vertical span (top=0 < current's bottom=50), so it must never be a
    // valid Down target no matter how close.
    auto* sideCandidate = new newui::SubView();
    sideCandidate->setBounds(newui::Rect(60, 0, 50, 50));
    sideCandidate->setVisible(true);
    sideCandidate->setAcceptsFocus(true);
    root->addChild(sideCandidate);

    auto* belowCandidate = new newui::SubView();
    belowCandidate->setBounds(newui::Rect(0, 100, 50, 50));  // top=100 >= current's bottom=50
    belowCandidate->setVisible(true);
    belowCandidate->setAcceptsFocus(true);
    root->addChild(belowCandidate);

    root->setFocusedSubView(current);
    newui::UIInputManager::instance().moveFocusSpatially(*root, newui::SpatialDirection::Down);

    EXPECT_EQ(root->focusedSubView(), belowCandidate);

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, PicksTheNearestOfSeveralValidCandidates) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 300, 400), "root");

    auto* current = new newui::SubView();
    current->setBounds(newui::Rect(0, 0, 50, 50));
    current->setVisible(true);
    current->setAcceptsFocus(true);
    root->addChild(current);

    auto* nearer = new newui::SubView();
    nearer->setBounds(newui::Rect(0, 100, 50, 50));
    nearer->setVisible(true);
    nearer->setAcceptsFocus(true);
    root->addChild(nearer);

    auto* farther = new newui::SubView();
    farther->setBounds(newui::Rect(0, 250, 50, 50));
    farther->setVisible(true);
    farther->setAcceptsFocus(true);
    root->addChild(farther);

    root->setFocusedSubView(current);
    newui::UIInputManager::instance().moveFocusSpatially(*root, newui::SpatialDirection::Down);

    EXPECT_EQ(root->focusedSubView(), nearer);

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, NoOpWhenNothingLiesInTheRequestedDirection) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 300), "root");

    auto* current = new newui::SubView();
    current->setBounds(newui::Rect(0, 100, 50, 50));
    current->setVisible(true);
    current->setAcceptsFocus(true);
    root->addChild(current);

    // Only other candidate is above, not below.
    auto* above = new newui::SubView();
    above->setBounds(newui::Rect(0, 0, 50, 50));
    above->setVisible(true);
    above->setAcceptsFocus(true);
    root->addChild(above);

    root->setFocusedSubView(current);
    bool moved = newui::UIInputManager::instance().moveFocusSpatially(*root, newui::SpatialDirection::Down);

    EXPECT_FALSE(moved);
    EXPECT_EQ(root->focusedSubView(), current);

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, RespectsTheActiveFocusScopeAndWontJumpOutOfIt) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 400), "root");

    auto* scope = new newui::SubView();
    scope->setBounds(newui::Rect(0, 0, 200, 100));
    scope->setVisible(true);
    scope->setFocusScope(true);
    root->addChild(scope);

    auto* insideScope = new newui::SubView();
    insideScope->setBounds(newui::Rect(0, 0, 50, 50));
    insideScope->setVisible(true);
    insideScope->setAcceptsFocus(true);
    scope->addChild(insideScope);

    // Outside the scope, and the only thing actually positioned below -
    // trapping must mean the jump finds nothing valid instead of
    // escaping to it, same as Tab can't leak out of a scope either.
    auto* outsideScope = new newui::SubView();
    outsideScope->setBounds(newui::Rect(0, 200, 50, 50));
    outsideScope->setVisible(true);
    outsideScope->setAcceptsFocus(true);
    root->addChild(outsideScope);

    root->setFocusedSubView(insideScope);
    bool moved = newui::UIInputManager::instance().moveFocusSpatially(*root, newui::SpatialDirection::Down);

    EXPECT_FALSE(moved);
    EXPECT_EQ(root->focusedSubView(), insideScope);

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, ResolvesThroughAFocusGuideToItsRealTarget) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 400), "root");

    auto* current = new newui::SubView();
    current->setBounds(newui::Rect(0, 0, 50, 50));
    current->setVisible(true);
    current->setAcceptsFocus(true);
    root->addChild(current);

    auto* realTarget = new newui::SubView();
    realTarget->setBounds(newui::Rect(0, 300, 50, 50));
    realTarget->setVisible(true);
    realTarget->setAcceptsFocus(true);
    root->addChild(realTarget);

    // Nearest thing below current, but a guide - never itself becomes
    // focusedSubView(), same contract moveFocus() (Tab) relies on.
    auto* guide = new newui::FocusGuide();
    guide->setBounds(newui::Rect(0, 100, 0, 0));
    guide->setRedirectTarget(realTarget);
    root->addChild(guide);

    root->setFocusedSubView(current);
    newui::UIInputManager::instance().moveFocusSpatially(*root, newui::SpatialDirection::Down);

    EXPECT_EQ(root->focusedSubView(), realTarget);

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, KeyEventDoesNotJumpWhenTheFocusedViewHandlesTheArrowKeyItself) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 300), "root");

    auto* current = new newui::SubView();
    current->setBounds(newui::Rect(0, 0, 50, 50));
    current->setVisible(true);
    current->setAcceptsFocus(true);
    int handledCount = 0;
    current->onKeyDown.add([&handledCount](newui::View&, std::uint32_t, int, int, std::uint32_t) {
        ++handledCount;
        return newui::SyncReturn::Handled;
    });
    root->addChild(current);

    auto* below = new newui::SubView();
    below->setBounds(newui::Rect(0, 100, 50, 50));
    below->setVisible(true);
    below->setAcceptsFocus(true);
    root->addChild(below);

    root->setFocusedSubView(current);
    root->keyEvent(newui::keKeyDown, 0, 0, 1, newui::vkDownArrow);

    EXPECT_EQ(handledCount, 1);
    EXPECT_EQ(root->focusedSubView(), current) << "the focused View's own onKeyDown handled it - no fallback jump";

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, KeyEventFallsBackToASpatialJumpWhenTheFocusedViewIgnoresTheArrowKey) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 300), "root");

    // A plain SubView never hooks onKeyDown at all - onKeyDown.syncCallFirst()
    // reports Ignored for it automatically, exactly like a Button/Toggle/
    // Label that's never touched arrow keys, and (via the fix in
    // ListView::handleKeyDown()/TreeView::handleKeyDown()) exactly like a
    // ListView/TreeView already clamped at its own first/last row.
    auto* current = new newui::SubView();
    current->setBounds(newui::Rect(0, 0, 50, 50));
    current->setVisible(true);
    current->setAcceptsFocus(true);
    root->addChild(current);

    auto* below = new newui::SubView();
    below->setBounds(newui::Rect(0, 100, 50, 50));
    below->setVisible(true);
    below->setAcceptsFocus(true);
    root->addChild(below);

    root->setFocusedSubView(current);
    root->keyEvent(newui::keKeyDown, 0, 0, 1, newui::vkDownArrow);

    EXPECT_EQ(root->focusedSubView(), below);

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, DownArrowOnAListViewAtItsLastRowJumpsToTheControlBelow) {
    // The original ask this whole feature exists for: a real ListView
    // reaching its own last row (not just a plain SubView that never
    // handled arrows at all) hands off to a spatial jump instead of
    // silently re-selecting the last row forever.
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 300), "root");

    auto* list = new newui::ListView();
    list->setBounds(newui::Rect(0, 0, 200, 60));
    list->setVisible(true);
    StubListRowModel model;
    model.rowCount = 3;
    list->setModel(&model);
    root->addChild(list);

    auto* below = new newui::SubView();
    below->setBounds(newui::Rect(0, 100, 50, 50));
    below->setVisible(true);
    below->setAcceptsFocus(true);
    root->addChild(below);

    root->setFocusedSubView(list);
    list->setSelectedIndex(2u);  // already the last row

    root->keyEvent(newui::keKeyDown, 0, 0, 1, newui::vkDownArrow);

    EXPECT_EQ(root->focusedSubView(), below);
    EXPECT_EQ(*list->selectedIndex(), 2u) << "the ListView's own selection must be untouched by the jump";

    root->destroy();
    delete root;
}

TEST(RootViewArrowKeySpatialJump, DownArrowOnAListViewNotYetAtItsLastRowStillMovesSelectionLocally) {
    // Regression guard for the fix itself: only the *boundary* case hands
    // off - ordinary in-bounds Down must still move the selection exactly
    // as before, never jumping out early.
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 300), "root");

    auto* list = new newui::ListView();
    list->setBounds(newui::Rect(0, 0, 200, 60));
    list->setVisible(true);
    StubListRowModel model;
    model.rowCount = 3;
    list->setModel(&model);
    root->addChild(list);

    auto* below = new newui::SubView();
    below->setBounds(newui::Rect(0, 100, 50, 50));
    below->setVisible(true);
    below->setAcceptsFocus(true);
    root->addChild(below);

    root->setFocusedSubView(list);
    list->setSelectedIndex(0u);

    root->keyEvent(newui::keKeyDown, 0, 0, 1, newui::vkDownArrow);

    EXPECT_EQ(root->focusedSubView(), list);
    EXPECT_EQ(*list->selectedIndex(), 1u);

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// A design-time SubView (isDesignTime(), e.g. content loaded into a
// Designer's RootViewProxy) never receives a real mouse/keyboard event and
// never takes keyboard focus - every dispatch site (onMouseDown/onMouseMove/
// onMouseUp/onMouseDblClick/onMouseWheel/onMouseEntered/onMouseLeft) gates on
// isDesignTime() individually before calling the target's own delegate, and
// setFocusedSubView() is refused via canBecomeFocused()'s own
// `!isDesignTime()` default (view.h). resolveInteractiveHit() itself, and
// hitTestChildren() underneath it, deliberately stay ungated though -
// capturedSubView_/hoveredSubView_ still track a design-time child like any
// other, since mouseMove()'s own ::SetCursor() call and cursorTargetAt() both
// need the real hit target to resolve a design-time child's own cursor while
// hovering/dragging it inside a Designer - it just never gets the event
// itself.
// ---------------------------------------------------------------------------

TEST(RootViewDesignTimeGating, MouseDownDoesNotDispatchToADesignTimeChildButStillCapturesIt) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->setDesignTime(true);
    child->onMouseDown += RecordDown;
    root->addChild(child);

    root->mouseDown(newui::Point(20, 20), 1, 0);

    EXPECT_EQ(g_downEvent.count, 0);
    EXPECT_EQ(root->capturedSubView(), child);
    EXPECT_EQ(root->focusedSubView(), nullptr);

    root->destroy();
    delete root;
}

TEST(RootViewDesignTimeGating, MouseMoveDoesNotDispatchToADesignTimeChildButStillTracksItAsHovered) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->setDesignTime(true);
    child->onMouseMove += RecordMove;
    root->addChild(child);

    root->mouseMove(newui::Point(20, 20), 0, 0);

    EXPECT_EQ(g_moveEvent.count, 0);
    EXPECT_EQ(root->hoveredSubView(), child);

    root->destroy();
    delete root;
}

TEST(RootViewDesignTimeGating, MouseUpWithoutCaptureIgnoresADesignTimeChild) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->setDesignTime(true);
    child->onMouseUp += RecordUp;
    root->addChild(child);

    root->mouseUp(newui::Point(20, 20), 1, 0);

    EXPECT_EQ(g_upEvent.count, 0);

    root->destroy();
    delete root;
}

TEST(RootViewDesignTimeGating, MouseDblClickDoesNotDispatchToADesignTimeChildButStillCapturesIt) {
    ResetMouseEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->setDesignTime(true);
    root->addChild(child);

    root->mouseDblClick(newui::Point(20, 20), 1, 0);

    EXPECT_EQ(root->capturedSubView(), child);
    EXPECT_EQ(root->focusedSubView(), nullptr);

    root->destroy();
    delete root;
}

TEST(RootViewDesignTimeGating, SetFocusedSubViewRefusesADesignTimeTarget) {
    ResetKeyEvents();
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 50, 50));
    child->setVisible(true);
    child->setDesignTime(true);
    child->onKeyDown += RecordKeyDown;
    root->addChild(child);

    root->setFocusedSubView(child);
    EXPECT_EQ(root->focusedSubView(), nullptr);

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
    first->setAcceptsFocus(true);
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
    child->setAcceptsFocus(true);
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
    child->setAcceptsFocus(true);
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
    child->setAcceptsFocus(true);
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
    child->setAcceptsFocus(true);
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
    grandchild->setAcceptsFocus(true);
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

// ---------------------------------------------------------------------------
// View::addChild()/removeChild()/reorderChild() (view.cpp) each call
// redraw() after their own updateLayout() - a real, confirmed live bug
// otherwise: updateLayout() only repositions the *Layout-arranged*
// children still in childViews_, so removing (or reordering) a child left
// its old on-screen pixels stale until some *unrelated* later event (a
// mouse hover, say) happened to repaint over that area. Caught live: an
// example's "destroy the focused button" demo left the button's stale
// pixels on screen after the button object itself was actually gone.
// ---------------------------------------------------------------------------

TEST(RootViewChildListChanges, AddingAChildInvalidatesTheParent) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaint();
    ASSERT_TRUE(root->dirtyRect().empty());

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    EXPECT_FALSE(root->dirtyRect().empty());

    root->destroy();
    delete root;
}

TEST(RootViewChildListChanges, RemovingAChildInvalidatesTheVacatedArea) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    root->repaint();
    ASSERT_TRUE(root->dirtyRect().empty());

    root->removeChild(child);

    EXPECT_FALSE(root->dirtyRect().empty())
        << "the vacated area must be repainted, not left showing the removed child's stale pixels";

    delete child;
    root->destroy();
    delete root;
}

TEST(RootViewChildListChanges, ReorderingAChildInvalidatesTheParent) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* first = new newui::SubView();
    first->setBounds(newui::Rect(0, 0, 50, 50));
    first->setVisible(true);
    root->addChild(first);

    auto* second = new newui::SubView();
    second->setBounds(newui::Rect(60, 0, 50, 50));
    second->setVisible(true);
    root->addChild(second);

    root->repaint();
    ASSERT_TRUE(root->dirtyRect().empty());

    root->reorderChild(first, 1);

    EXPECT_FALSE(root->dirtyRect().empty());

    root->destroy();
    delete root;
}

TEST(RootViewChildListChanges, TogglingAChildsVisibilityInvalidatesTheParent) {
    // Same bug, same fix, one level down from add/remove/reorder above:
    // SubView::setVisible() (subview.cpp) is what CardLayout::arrange()
    // (layout.cpp) actually calls to swap which page a TabControl shows -
    // confirmed live: switching tabs via TabControl's own arrow-key
    // handling correctly changed the active tab button and the underlying
    // page state, but the old page's pixels stayed on screen until an
    // unrelated mouse move happened to repaint over them.
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    root->repaint();
    ASSERT_TRUE(root->dirtyRect().empty());

    child->setVisible(false);
    EXPECT_FALSE(root->dirtyRect().empty()) << "hiding a child must invalidate where it used to be drawn";

    root->repaint();
    ASSERT_TRUE(root->dirtyRect().empty());

    child->setVisible(true);
    EXPECT_FALSE(root->dirtyRect().empty()) << "showing a child must invalidate where it's now drawn";

    root->destroy();
    delete root;
}

// Real, live-reported bug, end-to-end: tabbing through several plain
// controls in a row showed the focus ring on some and not others,
// seemingly at random. Root cause was ViewStyle::computePrePaintBounds()
// (viewstyle.cpp) - see ViewStyleElevation.
// ComputePrePaintBoundsAlwaysReservesRoomForTheFocusRingEvenAtZeroElevation
// (test_viewstyle.cpp) for the isolated version of this same regression -
// being a genuine no-op for any non-elevated View (nearly everything),
// leaving View::redraw()'s own invalidated region at exactly the view's
// plain bounds with zero allowance for postPaint()'s default focus ring,
// which always draws 2px *outside* those bounds. This confirms the fix
// actually reaches RootView::dirtyRect() through the real
// setFocusedSubView() -> style().markDirty() -> View::redraw() ->
// computePrePaintBounds() chain, not just the isolated ViewStyle method.
TEST(RootViewChildListChanges, FocusingAChildInvalidatesEnoughRoomForTheDefaultFocusRing) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->setAcceptsFocus(true);
    root->addChild(child);

    root->repaint();
    ASSERT_TRUE(root->dirtyRect().empty());

    root->setFocusedSubView(child);

    ASSERT_FALSE(root->dirtyRect().empty());
    EXPECT_GT(root->dirtyRect().size().width, child->bounds().size().width)
        << "the invalidated region must extend beyond the child's own plain bounds to cover postPaint()'s ring";
    EXPECT_GT(root->dirtyRect().size().height, child->bounds().size().height);

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// Focus-recovery-on-destroy: RootView::notifySubViewRemoved() no longer just
// drops focusedSubView_ to nullptr when the removed subtree carries it away -
// it walks up from the removed subtree's own (still-alive) parent to the
// nearest ancestor that canBecomeFocused(), same "auto-refocus a fallback"
// idea the docx floated as a follow-up to FocusScope/FocusGuide (see
// [[uiinputmanager-task]]). hoveredSubView_/capturedSubView_ still just clear
// silently - only focus recovers, since only focus has a meaningful "give it
// to something else instead" fallback.
// ---------------------------------------------------------------------------

TEST(RootViewFocusRecovery, RemovingTheFocusedChildRecoversToAFocusableAncestor) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* panel = new newui::SubView();
    panel->setBounds(newui::Rect(0, 0, 100, 100));
    panel->setVisible(true);
    panel->setAcceptsFocus(true);
    root->addChild(panel);

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->setAcceptsFocus(true);
    panel->addChild(child);

    root->setFocusedSubView(child);
    ASSERT_EQ(root->focusedSubView(), child);

    int panelGotFocusCount = 0;
    panel->onGotFocus.add([&panelGotFocusCount](newui::View&) { ++panelGotFocusCount; return newui::SyncReturn::Handled; });

    panel->removeChild(child);

    EXPECT_EQ(root->focusedSubView(), panel) << "focus should recover to the nearest surviving focusable ancestor";
    EXPECT_EQ(panelGotFocusCount, 1) << "the recovered-to ancestor must actually get onGotFocus, not just the raw pointer";

    delete child;
    root->destroy();
    delete root;
}

TEST(RootViewFocusRecovery, SkipsNonFocusableAncestorsToFindOneFurtherUp) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* focusableGrandparent = new newui::SubView();
    focusableGrandparent->setBounds(newui::Rect(0, 0, 150, 150));
    focusableGrandparent->setVisible(true);
    focusableGrandparent->setAcceptsFocus(true);
    root->addChild(focusableGrandparent);

    // Plain, non-focusable intermediate container - recovery must walk
    // straight past it, not stop here (and not treat it as "no ancestor
    // found" either).
    auto* plainContainer = new newui::SubView();
    plainContainer->setBounds(newui::Rect(0, 0, 100, 100));
    plainContainer->setVisible(true);
    focusableGrandparent->addChild(plainContainer);

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->setAcceptsFocus(true);
    plainContainer->addChild(child);

    root->setFocusedSubView(child);
    ASSERT_EQ(root->focusedSubView(), child);

    plainContainer->removeChild(child);

    EXPECT_EQ(root->focusedSubView(), focusableGrandparent);

    delete child;
    root->destroy();
    delete root;
}

TEST(RootViewFocusRecovery, ClearsToNullptrWhenNoAncestorCanBecomeFocused) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* plainContainer = new newui::SubView();
    plainContainer->setBounds(newui::Rect(0, 0, 100, 100));
    plainContainer->setVisible(true);
    root->addChild(plainContainer);

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->setAcceptsFocus(true);
    plainContainer->addChild(child);

    root->setFocusedSubView(child);
    ASSERT_EQ(root->focusedSubView(), child);

    plainContainer->removeChild(child);

    EXPECT_EQ(root->focusedSubView(), nullptr);

    delete child;
    root->destroy();
    delete root;
}

TEST(RootViewFocusRecovery, RemovingTheFocusedViewIgnoresItsOwnCanResignFocusVeto) {
    // A doomed view refusing to resign focus (e.g. an unsaved-edit guard,
    // see command1.cpp's own example) must never block recovery/clearing -
    // it's being destroyed regardless of what it wants. Using
    // setFocusedSubView() for recovery instead of a direct field write
    // would silently leave focusedSubView_ pointing at freed memory the
    // instant the caller deletes it.
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    auto* panel = new newui::SubView();
    panel->setBounds(newui::Rect(0, 0, 100, 100));
    panel->setVisible(true);
    panel->setAcceptsFocus(true);
    root->addChild(panel);

    auto* child = new VetoableSubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    child->allowResign = false;
    panel->addChild(child);

    root->setFocusedSubView(child);
    ASSERT_EQ(root->focusedSubView(), child);

    panel->removeChild(child);

    EXPECT_EQ(root->focusedSubView(), panel);

    delete child;
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

TEST(ViewIsDesignTime, ChildViewDoesNotInheritItsRootViewsFlag) {
    // A View reports only its own explicitly-set flag - it does NOT defer
    // to an owning RootView's flag (an earlier version did; found to make
    // no sense once a design-time host needed some of its own attached
    // chrome to stay permanently non-design-time regardless of its
    // RootView's own state - see CodeToolsVsix::Workspace/DesignerEditor
    // for the real case this was found from). Whoever wants a given View
    // to be design-time sets it there directly.
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    auto* button = new newui::Button();
    root->addChild(button);

    root->setDesignTime(true);
    EXPECT_FALSE(button->isDesignTime());

    button->setDesignTime(true);
    EXPECT_TRUE(button->isDesignTime());
    EXPECT_TRUE(root->isDesignTime());  // unaffected either way - no shared state

    root->destroy();
    delete root;
}

TEST(ViewIsDesignTime, NestedDescendantDoesNotInheritAnAncestorsFlag) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    auto* row = new newui::SubView();
    root->addChild(row);
    auto* button = new newui::Button();
    row->addChild(button);

    root->setDesignTime(true);
    row->setDesignTime(true);
    EXPECT_FALSE(button->isDesignTime());  // neither ancestor's flag propagates down

    button->setDesignTime(true);
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

TEST(DesignTimeFlagsTest, DefaultToNoneAndCombineIndependently) {
    newui::SubView view;
    EXPECT_EQ(view.designTimeFlags(), newui::DesignTimeFlags::None);
    EXPECT_FALSE(view.isDesignTime());
    EXPECT_FALSE(view.isInternal());

    view.setDesignTimeFlag(newui::DesignTimeFlags::Internal);
    EXPECT_TRUE(view.isInternal());
    EXPECT_FALSE(view.isDesignTime());  // a different bit

    view.setDesignTime(true);
    EXPECT_TRUE(view.isDesignTime());
    EXPECT_TRUE(view.isInternal());  // setDesignTime() only touches its own bit
    EXPECT_TRUE(view.hasDesignTimeFlag(newui::DesignTimeFlags::DesignTime | newui::DesignTimeFlags::Internal));
    EXPECT_FALSE(view.hasDesignTimeFlag(newui::DesignTimeFlags::DesignTime | newui::DesignTimeFlags::ReadOnly));

    view.setDesignTime(false);
    EXPECT_FALSE(view.isDesignTime());
    EXPECT_TRUE(view.isInternal());

    view.setDesignTimeFlag(newui::DesignTimeFlags::Internal, false);
    EXPECT_EQ(view.designTimeFlags(), newui::DesignTimeFlags::None);
}

TEST(DesignTimeFlagsTest, NotSelectableIsIndependentOfInternal) {
    newui::SubView view;
    EXPECT_TRUE(view.isSelectableAtDesignTime());

    view.setDesignTimeFlag(newui::DesignTimeFlags::Internal);
    EXPECT_TRUE(view.isSelectableAtDesignTime());  // internal parts can still be selectable

    view.setDesignTimeFlag(newui::DesignTimeFlags::NotSelectable);
    EXPECT_FALSE(view.isSelectableAtDesignTime());

    view.setDesignTimeFlag(newui::DesignTimeFlags::Internal, false);
    EXPECT_FALSE(view.isInternal());
    EXPECT_FALSE(view.isSelectableAtDesignTime());
}

TEST(DesignTimeFlagsTest, ExplicitGetSetPairsEachTouchOnlyTheirOwnBit) {
    newui::SubView view;
    EXPECT_FALSE(view.isInternal());
    EXPECT_FALSE(view.isNotSelectable());
    EXPECT_FALSE(view.isReadOnly());

    view.setInternal(true);
    EXPECT_TRUE(view.isInternal());
    EXPECT_EQ(view.designTimeFlags(), newui::DesignTimeFlags::Internal);

    view.setNotSelectable(true);
    view.setReadOnly(true);
    view.setDesignTime(true);
    EXPECT_EQ(view.designTimeFlags(),
        newui::DesignTimeFlags::Internal | newui::DesignTimeFlags::NotSelectable
        | newui::DesignTimeFlags::ReadOnly | newui::DesignTimeFlags::DesignTime);

    view.setNotSelectable(false);
    EXPECT_FALSE(view.isNotSelectable());
    EXPECT_TRUE(view.isSelectableAtDesignTime());
    EXPECT_TRUE(view.isInternal());
    EXPECT_TRUE(view.isReadOnly());

    view.setInternal(false);
    view.setReadOnly(false);
    EXPECT_EQ(view.designTimeFlags(), newui::DesignTimeFlags::DesignTime);
}

// ---------------------------------------------------------------------------
// RootView::markDirty(fromView, rect) accumulates into dirtyRect_ via
// Rect::united() (geometry.h), and repaint() - not the overridable
// presentRepaintedBuffer() hook - is what consumes it afterward.
// ---------------------------------------------------------------------------

TEST(RootViewMarkDirty, SeparateRegionsAccumulateIntoTheirBoundingBox) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaint();
    ASSERT_TRUE(root->dirtyRect().empty());

    root->markDirty(root, newui::Rect(0, 0, 10, 10));
    root->markDirty(root, newui::Rect(100, 120, 20, 10));

    EXPECT_EQ(root->dirtyRect(), newui::Rect(0, 0, 120, 130));

    root->destroy();
    delete root;
}

TEST(RootViewMarkDirty, FirstRegionIsTakenAsIs) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaint();

    root->markDirty(root, newui::Rect(40, 50, 20, 30));

    EXPECT_EQ(root->dirtyRect(), newui::Rect(40, 50, 20, 30));

    root->destroy();
    delete root;
}

TEST(RootViewMarkDirty, FractionalRegionsAreSnappedOutwardToWholePixels) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaint();

    root->markDirty(root, newui::Rect(10.25f, 20.75f, 30.5f, 40.5f));  // right 40.75, bottom 61.25

    EXPECT_EQ(root->dirtyRect(), newui::Rect(10, 20, 31, 42));

    root->destroy();
    delete root;
}

TEST(RootViewMarkDirty, AnEmptyRegionDoesNotStretchTheDirtyRectToTheOrigin) {
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaint();
    root->markDirty(root, newui::Rect(50, 50, 10, 10));

    root->markDirty(root, newui::Rect(0, 0, 0, 0));

    EXPECT_EQ(root->dirtyRect(), newui::Rect(50, 50, 10, 10));

    root->destroy();
    delete root;
}

namespace {

// Overrides presentRepaintedBuffer() the way PopupTool does - replaces it outright, never
// calling the base - and records what dirtyRect_ held while it ran.
class PresentOverridingRootView : public newui::RootView {
public:
    using newui::RootView::RootView;
    using newui::RootView::repaintNow;
    using newui::RootView::repaint;
    using newui::RootView::dirtyRect;

    int presentCalls = 0;
    newui::Rect dirtyDuringPresent;

    void presentRepaintedBuffer() override {
        ++presentCalls;
        dirtyDuringPresent = dirtyRect();
    }
};

}

TEST(RootViewRepaint, ConsumesDirtyRectEvenWhenASubclassReplacesThePresentHook) {
    // Regression: dirtyRect_ used to be cleared inside RootView's own presentRepaintedBuffer(),
    // so a subclass that overrode it (PopupTool) never cleared it - its dirtyRect_ just kept
    // growing for the life of the window.
    auto* root = new PresentOverridingRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    root->repaintNow();

    EXPECT_EQ(root->presentCalls, 1);
    EXPECT_TRUE(root->dirtyRect().empty());

    root->destroy();
    delete root;
}

TEST(RootViewRepaint, PresentHookStillSeesTheRegionThatWasJustRepainted) {
    auto* root = new PresentOverridingRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");

    root->repaintNow();

    // repaintNow() marks the whole client area dirty first.
    EXPECT_EQ(root->dirtyDuringPresent, newui::Rect(0, 0, 200, 200));

    root->destroy();
    delete root;
}

TEST(RootViewRepaint, EachRepaintStartsFromAFreshDirtyRect) {
    auto* root = new PresentOverridingRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaintNow();

    root->markDirty(root, newui::Rect(10, 10, 5, 5));

    // Not the previous repaint's whole-window region, unioned in forever.
    EXPECT_EQ(root->dirtyRect(), newui::Rect(10, 10, 5, 5));

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// Repainting must be idempotent: a repaint of a narrow dirty region can't change a single pixel
// anywhere else, and the result must equal what a from-scratch full repaint produces. It wasn't -
// RootView::repaint() filled the root background only inside dirtyRect_ but redrew every child over
// the whole window, so anything outside the dirty rect (a transparent Label's anti-aliased glyph
// edges, most visibly) was re-blended onto its own previous pixels on every unrelated repaint and
// thickened a little each time.
// ---------------------------------------------------------------------------

namespace {

// Every byte of the root's whole pixel buffer (rows packed, no stride padding).
std::vector<std::uint8_t> bufferBytes(newui::RootView& root) {
    BLImageData data;
    root.getImageBuffer().get_data(&data);
    std::vector<std::uint8_t> bytes;
    for (int y = 0; y < data.size.h; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(data.pixel_data) + intptr_t(y) * data.stride;
        bytes.insert(bytes.end(), row, row + size_t(data.size.w) * 4);
    }
    return bytes;
}

// The same, for one rectangle of it.
std::vector<std::uint8_t> regionBytes(newui::RootView& root, int x, int y, int w, int h) {
    BLImageData data;
    root.getImageBuffer().get_data(&data);
    std::vector<std::uint8_t> bytes;
    for (int row = y; row < y + h; ++row) {
        const auto* p = static_cast<const std::uint8_t*>(data.pixel_data) + intptr_t(row) * data.stride + size_t(x) * 4;
        bytes.insert(bytes.end(), p, p + size_t(w) * 4);
    }
    return bytes;
}

// The largest per-channel difference between two buffers (INT_MAX if they aren't the same size).
int maxChannelDelta(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) {
        return 1 << 30;
    }
    int largest = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        const int delta = std::abs(int(a[i]) - int(b[i]));
        largest = delta > largest ? delta : largest;
    }
    return largest;
}

// Pruned and full repaints agree to within this per channel - see RootView::verifyPrunedRepaint()'s
// tolerance for why it isn't zero (a clip edge through an anti-aliased outline can be a level off).
constexpr int kPruneTolerance = 2;

bool hasMoreThanOnePixelValue(const std::vector<std::uint8_t>& bytes) {
    for (size_t i = 4; i + 3 < bytes.size(); i += 4) {
        if (std::memcmp(&bytes[i], &bytes[0], 4) != 0) {
            return true;
        }
    }
    return false;
}

// A root with one plain (transparent-by-default) Label, painted once in full. Sizing the buffer via
// setBounds() is what triggers the first paint, headlessly.
struct LabelledRoot {
    TestableRootView* root = new TestableRootView(nullptr, newui::Rect(0, 0, 10, 10), "root");
    newui::Label* label = new newui::Label();

    LabelledRoot() {
        label->setText("Hello, thickening");
        label->setBounds(newui::Rect(10, 10, 160, 24));
        root->addChild(label);
        root->setBounds(newui::Rect(0, 0, 200, 100));
    }

    ~LabelledRoot() {
        root->destroy();
        delete root;
    }

    // Repaint only a small region well away from the label.
    void repaintElsewhere(int times) {
        for (int i = 0; i < times; ++i) {
            root->markDirty(root, newui::Rect(180, 80, 10, 10));
            root->repaint();
        }
    }
};

}

TEST(RootViewRepaintIdempotence, ATransparentLabelDoesNotThickenWhenSomethingElseRepaints) {
    LabelledRoot fixture;
    ASSERT_FALSE(fixture.root->getImageBuffer().is_empty());
    const auto before = regionBytes(*fixture.root, 10, 10, 160, 24);
    ASSERT_TRUE(hasMoreThanOnePixelValue(before)) << "premise: the label's text was actually drawn";

    fixture.repaintElsewhere(5);

    EXPECT_EQ(regionBytes(*fixture.root, 10, 10, 160, 24), before)
        << "the label's pixels changed although only a region far away was repainted";
}

TEST(RootViewRepaintIdempotence, ANarrowRepaintLeavesTheWholeBufferEqualToAFullRepaint) {
    LabelledRoot fixture;
    fixture.repaintElsewhere(5);
    const auto afterNarrowRepaints = bufferBytes(*fixture.root);

    fixture.root->repaintNow();  // from scratch: whole client area

    EXPECT_EQ(bufferBytes(*fixture.root), afterNarrowRepaints);
}

TEST(RootViewRepaintIdempotence, RepaintingTheLabelsOwnRegionRepeatedlyIsAlsoStable) {
    LabelledRoot fixture;
    const auto before = regionBytes(*fixture.root, 10, 10, 160, 24);

    for (int i = 0; i < 5; ++i) {
        fixture.root->markDirty(fixture.root, newui::Rect(10, 10, 160, 24));
        fixture.root->repaint();
    }

    EXPECT_EQ(regionBytes(*fixture.root, 10, 10, 160, 24), before);
}

TEST(RootViewRepaintIdempotence, ATranslucentRootBackgroundDoesNotAccumulateAcrossRepaints) {
    // A background with alpha < 1 composites over whatever's already in the buffer - so unless the
    // buffer starts every repaint blank, each repaint of a region darkens/lightens it further.
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 10, 10), "root");
    root->style().setBackgroundColor(newui::Color(1.0f, 0.0f, 0.0f, 0.5f));
    root->setBounds(newui::Rect(0, 0, 200, 100));
    ASSERT_FALSE(root->getImageBuffer().is_empty());
    const auto before = bufferBytes(*root);

    for (int i = 0; i < 4; ++i) {
        root->markDirty(root, newui::Rect(150, 50, 40, 40));
        root->repaint();
    }

    EXPECT_EQ(bufferBytes(*root), before);

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// RootView::invalidate() shows a region of the buffer; it is not a repaint request and must not
// disturb the region a pending markDirty() repaint still owes the screen.
// ---------------------------------------------------------------------------

TEST(RootViewInvalidate, DoesNotDropARegionThatIsStillWaitingToBeRepainted) {
    // Regression: invalidate() used to clear dirtyRect_, so the deferred repaint that markDirty() had
    // scheduled then presented an empty region and the update was never shown.
    auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaint();  // start from a clean dirtyRect_
    root->markDirty(root, newui::Rect(40, 50, 20, 30));
    ASSERT_EQ(root->dirtyRect(), newui::Rect(40, 50, 20, 30));

    root->invalidate();
    EXPECT_EQ(root->dirtyRect(), newui::Rect(40, 50, 20, 30)) << "invalidate() with no argument";

    newui::Rect region(0, 0, 10, 10);
    root->invalidate(&region);
    EXPECT_EQ(root->dirtyRect(), newui::Rect(40, 50, 20, 30)) << "invalidate(rect)";

    root->invalidate(root, &region);
    EXPECT_EQ(root->dirtyRect(), newui::Rect(40, 50, 20, 30)) << "invalidate(fromView, rect)";

    root->destroy();
    delete root;
}

TEST(RootViewInvalidate, ThePendingRepaintStillPresentsItsRegionAfterAnInvalidate) {
    auto* root = new PresentOverridingRootView(nullptr, newui::Rect(0, 0, 200, 200), "root");
    root->repaint();
    root->markDirty(root, newui::Rect(40, 50, 20, 30));

    root->invalidate();
    root->repaint();  // what the scheduled idle task runs

    EXPECT_EQ(root->dirtyDuringPresent, newui::Rect(40, 50, 20, 30));

    root->destroy();
    delete root;
}

// ---------------------------------------------------------------------------
// Opt-in benchmark - DISABLED_ so it never runs in the normal suite. Run it in a *Release* build:
//   newui_tests.exe --gtest_also_run_disabled_tests --gtest_filter="RootViewRepaintBenchmark*"
// Blend2D under Debug is many times slower and says nothing about real cost.
//
// It answers "is redrawing views that don't need redrawing actually expensive?": RootView::repaint()
// redraws the whole tree every time (see its comment), so this times a repaint triggered by a tiny
// dirty region - what a hover produces - against trees of different sizes, with everything inside the
// window and with lots of views entirely outside it.
// ---------------------------------------------------------------------------

namespace {

// `onScreen` views laid out in a grid inside a 500x720 window, then `offScreen` more far below it.
// A mix of the cheap-and-common controls (Label, Button, Progress, Slider).
struct BenchTree {
    TestableRootView* root = new TestableRootView(nullptr, newui::Rect(0, 0, 10, 10), "bench");

    BenchTree(int onScreen, int offScreen) {
        const int cols = 8;
        for (int i = 0; i < onScreen + offScreen; ++i) {
            newui::SubView* view = nullptr;
            switch (i % 4) {
            case 0: { auto* l = new newui::Label(); l->setText("Label " + std::to_string(i)); view = l; break; }
            case 1: { auto* b = new newui::Button(); b->setText("Button"); view = b; break; }
            case 2: { auto* p = new newui::Progress(); p->setValue(0.4); view = p; break; }
            default: { view = new newui::Slider(); break; }
            }
            const int row = i / cols;
            const float y = i < onScreen ? float(row * 18) : 800.0f + float((i - onScreen) / cols) * 18.0f;
            view->setBounds(newui::Rect(float((i % cols) * 60), y, 58.0f, 16.0f));
            view->setVisible(true);
            root->addChild(view);
        }
        root->setBounds(newui::Rect(0, 0, 500, 720));
    }

    ~BenchTree() {
        root->destroy();
        delete root;
    }

    // Average milliseconds for a repaint driven by a small (button-sized) dirty region, in the given mode.
    double smallRepaintMs(newui::RepaintMode mode, int iterations) {
        root->setRepaintMode(mode);
        root->setVerifyRepaint(false);
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < iterations; ++i) {
            root->markDirty(root, newui::Rect(10, 10, 60, 18));
            root->repaint();
        }
        const std::chrono::duration<double, std::milli> elapsed = std::chrono::steady_clock::now() - start;
        return elapsed.count() / iterations;
    }
};

}

TEST(RootViewRepaintBenchmark, DISABLED_CostOfASmallRepaintByTreeSize) {
    std::printf("\n  %-34s %14s %14s\n", "tree", "full (ms)", "dirty (ms)");
    auto row = [](const std::string& name, BenchTree& tree, int iterations) {
        std::printf("  %-34s %14.3f %14.3f\n", name.c_str(),
            tree.smallRepaintMs(newui::RepaintMode::Full, iterations), tree.smallRepaintMs(newui::RepaintMode::Dirty, iterations));
    };
    for (int onScreen : { 0, 20, 80, 320 }) {
        BenchTree tree(onScreen, 0);
        row(std::to_string(onScreen) + " views, all on screen", tree, 200);
    }
    for (int offScreen : { 500, 2000 }) {
        BenchTree tree(80, offScreen);
        row("80 on screen + " + std::to_string(offScreen) + " far outside", tree, 100);
    }
}

// ---------------------------------------------------------------------------
// View::paintChildren() skips a child whose whole drawn extent (its bounds plus the room its focus ring/
// drop shadow need - computePrePaintBounds()) lies outside the part of its parent that can currently be
// seen. Nothing visible may change: a child that's culled was going to be clipped away entirely anyway.
// ---------------------------------------------------------------------------

namespace {

// Counts how many times it actually gets painted.
class PaintCounter : public newui::SubView {
public:
    int paints = 0;
    void paint(BLContext&) override { ++paints; }
};

// A visible PaintCounter with a plain ViewStyle (so it has a focus-ring pad, like any real control).
PaintCounter* addCounter(newui::View& parent, const newui::Rect& bounds, float elevation = 0.0f) {
    auto* view = new PaintCounter();
    auto style = std::make_unique<newui::ViewStyle>();
    style->setElevation(elevation);
    view->setStyle(std::move(style));
    view->setBounds(bounds);
    view->setVisible(true);
    parent.addChild(view);
    return view;
}

// A 200x100 root, laid out and painted once.
struct CullFixture {
    TestableRootView* root = new TestableRootView(nullptr, newui::Rect(0, 0, 10, 10), "cull");

    void show() { root->setBounds(newui::Rect(0, 0, 200, 100)); }

    ~CullFixture() {
        root->destroy();
        delete root;
    }
};

}

TEST(RootViewChildCulling, AChildEntirelyOutsideTheWindowIsNotPainted) {
    CullFixture f;
    auto* inside = addCounter(*f.root, newui::Rect(10, 10, 20, 20));
    auto* right = addCounter(*f.root, newui::Rect(300, 10, 20, 20));
    auto* below = addCounter(*f.root, newui::Rect(10, 300, 20, 20));
    auto* left = addCounter(*f.root, newui::Rect(-100, 10, 20, 20));
    auto* above = addCounter(*f.root, newui::Rect(10, -100, 20, 20));
    f.show();

    EXPECT_GE(inside->paints, 1) << "the visible child must still be painted";
    EXPECT_EQ(right->paints, 0);
    EXPECT_EQ(below->paints, 0);
    EXPECT_EQ(left->paints, 0);
    EXPECT_EQ(above->paints, 0);
}

TEST(RootViewChildCulling, AChildPartlyInsideTheWindowIsStillPainted) {
    CullFixture f;
    auto* overRight = addCounter(*f.root, newui::Rect(190, 10, 30, 20));
    auto* overBottom = addCounter(*f.root, newui::Rect(10, 90, 20, 30));
    auto* overTopLeft = addCounter(*f.root, newui::Rect(-10, -10, 30, 30));
    f.show();

    EXPECT_GE(overRight->paints, 1);
    EXPECT_GE(overBottom->paints, 1);
    EXPECT_GE(overTopLeft->paints, 1);
}

TEST(RootViewChildCulling, ARectJustPastTheEdgeIsCulledButItsFocusRingPadKeepsANearMissPainted) {
    CullFixture f;
    // Every ViewStyle reserves a few pixels for a focus ring, drawn outside the view's bounds and NOT
    // clipped to them - so a child a pixel outside the window can still put ring pixels inside it.
    auto* nearMiss = addCounter(*f.root, newui::Rect(201, 10, 20, 20));
    auto* farMiss = addCounter(*f.root, newui::Rect(230, 10, 20, 20));
    f.show();

    EXPECT_GE(nearMiss->paints, 1) << "its ring pad reaches into the window";
    EXPECT_EQ(farMiss->paints, 0);
}

TEST(RootViewChildCulling, ADropShadowThatReachesIntoTheWindowKeepsItsChildPainted) {
    CullFixture f;
    // Same position, only the elevation differs: the shadow's larger pad reaches back into the window.
    auto* flat = addCounter(*f.root, newui::Rect(212, 10, 20, 20), 0.0f);
    auto* raised = addCounter(*f.root, newui::Rect(212, 40, 20, 20), 16.0f);
    f.show();

    EXPECT_EQ(flat->paints, 0);
    EXPECT_GE(raised->paints, 1) << "a large elevation's drop shadow extends well past the child's bounds";
}

TEST(RootViewChildCulling, ScrollingTheContentChangesWhichChildrenArePainted) {
    CullFixture f;
    auto* content = addCounter(*f.root, newui::Rect(0, 0, 200, 100));
    auto* top = addCounter(*content, newui::Rect(10, 10, 20, 20));
    auto* farDown = addCounter(*content, newui::Rect(10, 500, 20, 20));
    f.show();
    ASSERT_GE(top->paints, 1);
    ASSERT_EQ(farDown->paints, 0) << "premise: not scrolled into view yet";

    content->setOrigin(newui::Point(0, 450));  // scroll: content y=450..550 is now what's shown
    top->paints = farDown->paints = 0;
    f.root->repaintNow();

    EXPECT_GE(farDown->paints, 1) << "scrolled into view";
    EXPECT_EQ(top->paints, 0) << "scrolled out of view";
}

TEST(RootViewChildCulling, AGrandchildOutsideItsParentsBoundsIsCulledEvenInsideTheWindow) {
    CullFixture f;
    auto* container = addCounter(*f.root, newui::Rect(10, 10, 100, 50));
    auto* inside = addCounter(*container, newui::Rect(5, 5, 20, 20));
    // x=150 is inside the 200-wide *window* but outside the 100-wide *container*, whose own clip hides it.
    auto* outsideParent = addCounter(*container, newui::Rect(150, 5, 20, 20));
    f.show();

    EXPECT_GE(inside->paints, 1);
    EXPECT_EQ(outsideParent->paints, 0);
}

TEST(RootViewChildCulling, ATreeWithManyOffscreenChildrenRendersExactlyLikeOneWithout) {
    // The real guarantee: culling can't change a single pixel. Same on-screen content, one tree also
    // carrying children scattered outside the window (labels, so there's real text to draw).
    auto build = [](bool withOffscreen) {
        auto* root = new TestableRootView(nullptr, newui::Rect(0, 0, 10, 10), "eq");
        auto addLabel = [root](const char* text, float x, float y) {
            auto* label = new newui::Label();
            label->setText(text);
            label->setBounds(newui::Rect(x, y, 90, 22));
            root->addChild(label);
        };
        addLabel("First", 10, 10);
        addLabel("Second", 10, 40);
        addLabel("Third", 100, 70);
        if (withOffscreen) {
            for (int i = 0; i < 20; ++i) {
                addLabel("Offscreen", 300.0f + float(i) * 10, 10);
                addLabel("Below", 10, 200.0f + float(i) * 25);
                addLabel("Left", -400.0f, float(i) * 25);
            }
        }
        root->setBounds(newui::Rect(0, 0, 200, 100));
        return root;
    };

    TestableRootView* plain = build(false);
    TestableRootView* crowded = build(true);

    EXPECT_EQ(bufferBytes(*crowded), bufferBytes(*plain));

    plain->destroy();
    delete plain;
    crowded->destroy();
    delete crowded;
}

// ---------------------------------------------------------------------------
// RepaintMode::Dirty - repaint() re-renders only dirtyRect_: blank it, paint the background and just the
// children that overlap it, everything clipped to it. The one thing that must never happen is a pruned
// repaint leaving the screen different from what a full repaint would.
// ---------------------------------------------------------------------------

namespace {

class ColorView : public newui::SubView {
public:
    BLRgba32 color = BLRgba32(0xFFFF0000);
    void paint(BLContext& ctx) override {
        ctx.set_fill_style(color);
        ctx.fill_rect(BLRect(0, 0, bounds().size().width, bounds().size().height));
    }
};

newui::SyncReturn NoopRedrawNeeded(newui::RootView&) {
    return newui::SyncReturn::Handled;
}

struct PruneFixture {
    TestableRootView* root = new TestableRootView(nullptr, newui::Rect(0, 0, 10, 10), "prune");

    PruneFixture() {
        // Independent of NEWUI_REPAINT / NEWUI_VERIFY_REPAINT in the environment: verification renders a full
        // frame after each pruned repaint, which would inflate the paint counts these tests read.
        root->setRepaintMode(newui::RepaintMode::Dirty);
        root->setVerifyRepaint(false);
    }

    ~PruneFixture() {
        root->destroy();
        delete root;
    }

    void show() { root->setBounds(newui::Rect(0, 0, 200, 100)); }

    // Every byte of the buffer as a *full* repaint leaves it; the mode is put back afterwards.
    std::vector<std::uint8_t> fullRenderBytes() {
        const newui::RepaintMode mode = root->repaintMode();
        root->setRepaintMode(newui::RepaintMode::Full);
        root->repaintNow();
        auto bytes = bufferBytes(*root);
        root->setRepaintMode(mode);
        return bytes;
    }
};

// A tree with text, a coloured view, an elevated view (drop shadow) and containers - enough variety
// for the pruned-equals-full checks below.
struct PruneTree : PruneFixture {
    newui::Label* label = new newui::Label();
    ColorView* colored = new ColorView();
    newui::SubView* content = new newui::SubView();
    ColorView* scrolled = new ColorView();

    PruneTree() {
        label->setText("Hello, pruning");
        label->setBounds(newui::Rect(10, 10, 100, 22));
        root->addChild(label);

        colored->setBounds(newui::Rect(120, 10, 40, 30));
        colored->setVisible(true);
        root->addChild(colored);

        content->setBounds(newui::Rect(10, 45, 120, 50));
        content->setVisible(true);
        root->addChild(content);
        scrolled->setBounds(newui::Rect(5, 5, 30, 20));
        scrolled->color = BLRgba32(0xFF00AA00);
        scrolled->setVisible(true);
        content->addChild(scrolled);

        show();
        root->repaintNow();  // a known-good, fully painted starting point
    }

    // Repaint what the last change invalidated, and check the result is exactly what a full repaint gives.
    void expectPrunedEqualsFull(const char* what) {
        root->repaint();
        const auto pruned = bufferBytes(*root);
        EXPECT_LE(maxChannelDelta(pruned, fullRenderBytes()), kPruneTolerance) << "after: " << what;
    }
};

}

TEST(RootViewPruning, ADirtyRepaintOnlyPaintsTheChildrenThatOverlapTheDirtyRegion) {
    PruneFixture f;
    auto* a = addCounter(*f.root, newui::Rect(10, 10, 20, 20));
    auto* b = addCounter(*f.root, newui::Rect(100, 10, 20, 20));
    auto* c = addCounter(*f.root, newui::Rect(10, 60, 20, 20));
    f.show();
    a->paints = b->paints = c->paints = 0;

    f.root->markDirty(f.root, newui::Rect(5, 5, 30, 30));  // covers only `a`
    f.root->repaint();

    EXPECT_GE(a->paints, 1);
    EXPECT_EQ(b->paints, 0);
    EXPECT_EQ(c->paints, 0);
}

TEST(RootViewPruning, FullModeStillPaintsEverythingWhateverTheDirtyRegion) {
    PruneFixture f;
    f.root->setRepaintMode(newui::RepaintMode::Full);
    auto* a = addCounter(*f.root, newui::Rect(10, 10, 20, 20));
    auto* b = addCounter(*f.root, newui::Rect(100, 10, 20, 20));
    f.show();
    a->paints = b->paints = 0;

    f.root->markDirty(f.root, newui::Rect(5, 5, 30, 30));
    f.root->repaint();

    EXPECT_GE(a->paints, 1);
    EXPECT_GE(b->paints, 1);
}

TEST(RootViewPruning, NothingIsPaintedWhenNothingIsDirty) {
    PruneFixture f;
    auto* a = addCounter(*f.root, newui::Rect(10, 10, 20, 20));
    f.show();
    f.root->repaint();  // consumes whatever was pending
    a->paints = 0;

    f.root->repaint();

    EXPECT_EQ(a->paints, 0);
}

TEST(RootViewPruning, AnOnRedrawNeededListenerForcesAFullRepaint) {
    // A listener draws straight into the buffer, unclipped - it can't be limited to a region.
    PruneFixture f;
    f.root->onRedrawNeeded += NoopRedrawNeeded;
    auto* a = addCounter(*f.root, newui::Rect(10, 10, 20, 20));
    auto* b = addCounter(*f.root, newui::Rect(100, 10, 20, 20));
    f.show();
    a->paints = b->paints = 0;

    f.root->markDirty(f.root, newui::Rect(5, 5, 30, 30));
    f.root->repaint();

    EXPECT_GE(b->paints, 1) << "with a listener the whole tree repaints";
}

TEST(RootViewPruning, PixelsOutsideTheDirtyRegionAreNeverTouchedNotEvenByFocusRingsOrShadows) {
    PruneFixture f;
    auto* raised = addCounter(*f.root, newui::Rect(90, 30, 40, 40), 16.0f);  // drop shadow reaches far past its bounds
    auto* focused = addCounter(*f.root, newui::Rect(55, 60, 30, 20));        // focus ring draws outside its bounds
    auto* label = new newui::Label();
    label->setText("Hello");
    label->setBounds(newui::Rect(10, 10, 90, 22));
    f.root->addChild(label);
    (void)raised;
    f.show();
    f.root->setFocusedSubView(focused);
    f.root->repaintNow();
    const auto before = bufferBytes(*f.root);

    // Scribble a sentinel colour everywhere OUTSIDE the region that will be repainted.
    const int regionLeft = 60, regionTop = 20, regionRight = 130, regionBottom = 80;
    {
        BLImageData data;
        f.root->getImageBuffer().get_data(&data);
        for (int y = 0; y < data.size.h; ++y) {
            auto* row = reinterpret_cast<std::uint32_t*>(static_cast<std::uint8_t*>(data.pixel_data) + intptr_t(y) * data.stride);
            for (int x = 0; x < data.size.w; ++x) {
                const bool inside = x >= regionLeft && x < regionRight && y >= regionTop && y < regionBottom;
                if (!inside) {
                    row[x] = 0xFFFF00FFu;
                }
            }
        }
    }

    f.root->markDirty(f.root, newui::Rect(float(regionLeft), float(regionTop), float(regionRight - regionLeft), float(regionBottom - regionTop)));
    f.root->repaint();

    BLImageData after;
    f.root->getImageBuffer().get_data(&after);
    int touchedOutside = 0, wrongInside = 0;
    for (int y = 0; y < after.size.h; ++y) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(after.pixel_data) + intptr_t(y) * after.stride);
        for (int x = 0; x < after.size.w; ++x) {
            const bool inside = x >= regionLeft && x < regionRight && y >= regionTop && y < regionBottom;
            if (!inside && row[x] != 0xFFFF00FFu) {
                ++touchedOutside;
            }
            if (inside) {
                const auto* was = &before[(size_t(y) * size_t(after.size.w) + size_t(x)) * 4];
                const auto* now = reinterpret_cast<const std::uint8_t*>(&row[x]);
                for (int c = 0; c < 4; ++c) {
                    if (std::abs(int(now[c]) - int(was[c])) > kPruneTolerance) {
                        ++wrongInside;
                        break;
                    }
                }
            }
        }
    }
    EXPECT_EQ(touchedOutside, 0) << "pixels outside the dirty region were modified";
    EXPECT_EQ(wrongInside, 0) << "inside the region must match a full repaint of the same state";
}

TEST(RootViewPruning, ChangingATextLabelPrunesToTheSameResultAsAFullRepaint) {
    PruneTree t;
    t.label->setText("A different, longer piece of text");
    t.expectPrunedEqualsFull("Label::setText");
}

TEST(RootViewPruning, ChangingAViewsColorAndRedrawingPrunesToTheSameResult) {
    PruneTree t;
    t.colored->color = BLRgba32(0xFF0000FF);
    t.colored->redraw();
    t.expectPrunedEqualsFull("colour change + redraw()");
}

TEST(RootViewPruning, MovingAViewWithSetBoundsPrunesToTheSameResult) {
    PruneTree t;
    t.colored->setBounds(newui::Rect(60, 55, 40, 30));
    t.expectPrunedEqualsFull("SubView::setBounds (no explicit redraw)");
}

TEST(RootViewPruning, HidingAndShowingAViewPrunesToTheSameResult) {
    PruneTree t;
    t.colored->setVisible(false);
    t.expectPrunedEqualsFull("setVisible(false)");
    t.colored->setVisible(true);
    t.expectPrunedEqualsFull("setVisible(true)");
}

TEST(RootViewPruning, ScrollingAContainerPrunesToTheSameResult) {
    PruneTree t;
    t.content->setOrigin(newui::Point(0, 10));
    t.content->redraw();
    t.expectPrunedEqualsFull("scroll origin change + redraw()");
}

TEST(RootViewPruning, ASetBoundsInvalidatesBothTheOldAndTheNewArea) {
    PruneFixture f;
    auto* child = addCounter(*f.root, newui::Rect(10, 10, 20, 20));
    f.show();
    f.root->repaint();
    ASSERT_TRUE(f.root->dirtyRect().empty()) << "premise: nothing pending";

    child->setBounds(newui::Rect(100, 50, 20, 20));

    const newui::Rect dirty = f.root->dirtyRect();
    EXPECT_LE(dirty.left(), 10.0f) << "the area it left must be repainted";
    EXPECT_LE(dirty.top(), 10.0f);
    EXPECT_GE(dirty.right(), 120.0f) << "and the area it moved to";
    EXPECT_GE(dirty.bottom(), 70.0f);
}

TEST(RootViewPruning, VerifyModeStaysQuietWhenEveryChangeWasInvalidated) {
    PruneTree t;
    t.root->setVerifyRepaint(true);

    t.label->setText("Properly invalidated");
    t.root->repaint();

    EXPECT_EQ(t.root->repaintVerifyMismatches(), 0u);
}

TEST(RootViewPruning, VerifyModeCatchesAChangeThatWasNeverInvalidated) {
    // The whole point of verification: full repaints hide a missing markDirty() (the next repaint anywhere
    // redraws everything); pruning doesn't, and this is how that shows up before it reaches a user.
    PruneTree t;
    t.root->setVerifyRepaint(true);

    t.colored->color = BLRgba32(0xFF00FFFF);  // changes how it draws, but nothing says so...
    t.root->markDirty(t.root, newui::Rect(10, 80, 20, 10));  // ...and something unrelated repaints
    t.root->repaint();

    EXPECT_GE(t.root->repaintVerifyMismatches(), 1u);
}

TEST(RootViewPruning, RepaintingARegionWhoseEdgesCutThroughTextAndControlsMatchesAFullRepaint) {
    // Clip edges landing in the middle of anti-aliased glyphs and themed control chrome. Every pruned region
    // must come out the same as a full repaint - to within the rasterizer's clip-edge rounding (a level or
    // so), never more: a bigger difference would be a real bug.
    PruneFixture f;
    auto* label = new newui::Label();
    label->setText("Hello, edge cases");
    label->setBounds(newui::Rect(10, 8, 120, 22));
    f.root->addChild(label);
    auto* button = new newui::Button();
    button->setText("Toggle me");
    button->setBounds(newui::Rect(10, 36, 110, 28));
    f.root->addChild(button);
    auto* slider = new newui::Slider();
    slider->setBounds(newui::Rect(10, 70, 150, 22));
    f.root->addChild(slider);
    f.show();
    f.root->repaintNow();
    const auto full = bufferBytes(*f.root);

    int checked = 0, failed = 0, largest = 0;
    std::string firstFailure;
    for (int x = 5; x <= 130; x += 7) {
        for (int y = 3; y <= 80; y += 9) {
            for (int w : { 13, 31, 64 }) {
                const newui::Rect region(float(x), float(y), float(w), 17.0f);
                f.root->markDirty(f.root, region);
                f.root->repaint();
                ++checked;
                const int delta = maxChannelDelta(bufferBytes(*f.root), full);
                largest = delta > largest ? delta : largest;
                if (delta > kPruneTolerance) {
                    ++failed;
                    if (firstFailure.empty()) {
                        firstFailure = "region (" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(w) + "x17), delta " + std::to_string(delta);
                    }
                }
                f.root->repaintNow();  // back to a known-good buffer for the next region
            }
        }
    }

    EXPECT_EQ(failed, 0) << failed << " of " << checked << " pruned regions differ from a full repaint by more than "
        << kPruneTolerance << " levels; first: " << firstFailure;
    EXPECT_LE(largest, kPruneTolerance);
}

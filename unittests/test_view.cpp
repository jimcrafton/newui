#include "newui/subview.h"
#include "newui/viewstyle.h"

#include <gtest/gtest.h>

namespace {

// Delegate::FunctionPtr is a plain function pointer (no capturing lambdas),
// so a shared counter across several children's onDestroyed has to be a
// free function over a namespace-scope variable - same convention
// test_property.cpp's RecordChange()/g_changeCount already use.
int g_destroyedCount = 0;

newui::SyncReturn RecordDestroyed(newui::View&) {
    ++g_destroyedCount;
    return newui::SyncReturn::Handled;
}

// Same "plain free function, not a lambda" reasoning as RecordDestroyed()
// above applies to onQueryContentSize too - a non-capturing lambda
// converts to both Delegate::Callback (std::function) and
// Delegate::FunctionPtr, which MSVC rejects as an ambiguous add() call.
newui::SyncReturn AnswerContentSize500x4000(newui::View&, newui::Size& outSize) {
    outSize = newui::Size(500, 4000);
    return newui::SyncReturn::Handled;
}

newui::SyncReturn IgnoreContentSizeQuery(newui::View&, newui::Size&) {
    return newui::SyncReturn::Ignored;
}

newui::SyncReturn AnswerContentSize10x20(newui::View&, newui::Size& outSize) {
    outSize = newui::Size(10, 20);
    return newui::SyncReturn::Handled;
}

}  // namespace

// ---------------------------------------------------------------------------
// getClientBounds() - computed live from style()/getBounds(), with no
// paintStyle()/BLContext call involved anywhere below (see View::
// getClientBounds()'s comment and ViewStyle::computeClientBounds()).
// ---------------------------------------------------------------------------

TEST(ViewGetClientBounds, DefaultStyleMatchesFullBoundsWithNoPaint) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 100, 50));

    const newui::Rect clientBounds = view->getClientBounds();

    EXPECT_EQ(clientBounds, newui::Rect(0, 0, 100, 50));

    delete view;
}

TEST(ViewGetClientBounds, ReflectsStyleImmediatelyAfterSetStyle) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 100, 50));

    auto style = std::make_unique<newui::ViewStyle>();
    style->borderWidth = 5.0f;
    view->setStyle(std::move(style));

    const newui::Rect clientBounds = view->getClientBounds();

    EXPECT_EQ(clientBounds, newui::Rect(5, 5, 90, 40));

    delete view;
}

TEST(ViewGetClientBounds, ReflectsBoundsChangeWithoutAPriorPaint) {
    auto* view = new newui::SubView();
    auto style = std::make_unique<newui::ViewStyle>();
    style->borderWidth = 2.0f;
    view->setStyle(std::move(style));

    view->setBounds(newui::Rect(0, 0, 64, 64));

    EXPECT_EQ(view->getClientBounds(), newui::Rect(2, 2, 60, 60));

    delete view;
}

// ---------------------------------------------------------------------------
// desiredSize() - explicit override (setDesiredSize()) takes precedence
// over the computed fallback (computeDesiredSize(), default: current
// bounds size) - see View::desiredSize()'s comment.
// ---------------------------------------------------------------------------

TEST(ViewDesiredSize, NoOverrideFallsBackToCurrentBoundsSize) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 80, 24));

    EXPECT_FALSE(view->hasDesiredSizeOverride());
    EXPECT_EQ(view->desiredSize(), newui::Size(80, 24));

    delete view;
}

TEST(ViewDesiredSize, SetDesiredSizeOverridesRegardlessOfCurrentBounds) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 80, 24));

    view->setDesiredSize(newui::Size(120, 40));

    EXPECT_TRUE(view->hasDesiredSizeOverride());
    EXPECT_EQ(view->desiredSize(), newui::Size(120, 40));
    // Setting a desired size never moves/resizes the view on its own -
    // something else (a Layout) has to consult it.
    EXPECT_EQ(view->bounds(), newui::Rect(0, 0, 80, 24));

    delete view;
}

TEST(ViewDesiredSize, ClearDesiredSizeRevertsToComputedFallback) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 80, 24));
    view->setDesiredSize(newui::Size(120, 40));

    view->clearDesiredSize();

    EXPECT_FALSE(view->hasDesiredSizeOverride());
    EXPECT_EQ(view->desiredSize(), newui::Size(80, 24));

    delete view;
}

// ---------------------------------------------------------------------------
// contentSize() - onQueryContentSize (syncCallFirst - first listener to
// return SyncReturn::Handled wins), falling back to bounds().size() when
// unhandled. See View::contentSize()'s own comment for what this is for
// (a scroll-owning container learning a child's true content extent
// without needing to know its concrete type - ScrollView::updateLayout()
// is the one real consumer today, see test_controls.cpp).
// ---------------------------------------------------------------------------

TEST(ViewContentSize, DefaultsToBoundsSizeWhenUnhandled) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 80, 24));

    EXPECT_EQ(view->contentSize(), newui::Size(80, 24));

    delete view;
}

TEST(ViewContentSize, ReturnsWhateverASingleHandlerReports) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 80, 24));

    view->onQueryContentSize.add(AnswerContentSize500x4000);

    EXPECT_EQ(view->contentSize(), newui::Size(500, 4000));

    delete view;
}

TEST(ViewContentSize, AListenerThatIgnoresFallsThroughToTheNextOne) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 80, 24));

    view->onQueryContentSize.add(IgnoreContentSizeQuery);
    view->onQueryContentSize.add(AnswerContentSize10x20);

    EXPECT_EQ(view->contentSize(), newui::Size(10, 20));

    delete view;
}

// ---------------------------------------------------------------------------
// cursor()/setCursor(Cursor)/cursorKind()/resolvedCursor() - RootView::
// handleMessage()'s WM_SETCURSOR case (rootview.cpp) is what actually shows
// resolvedCursor() on screen; these only cover the pure state/resolution
// logic. Cursor itself (cursor.h/cursor.cpp) is tested standalone in
// test_cursor.cpp - these only cover View's own use of it.
// ---------------------------------------------------------------------------

TEST(ViewCursor, DefaultsToArrow) {
    auto* view = new newui::SubView();

    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Arrow);
    EXPECT_EQ(view->resolvedCursor(), ::LoadCursorW(nullptr, IDC_ARROW));

    delete view;
}

TEST(ViewCursor, MutatingCursorInPlaceChangesCursorKindAndResolvedCursor) {
    auto* view = new newui::SubView();

    view->cursor().setCursorKind(newui::CursorKind::Hand);

    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Hand);
    EXPECT_EQ(view->resolvedCursor(), ::LoadCursorW(nullptr, IDC_HAND));

    delete view;
}

TEST(ViewCursor, SetCursorReplacesItWholesale) {
    auto* view = new newui::SubView();

    view->setCursor(newui::Cursor(newui::CursorKind::Hand));

    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Hand);
    EXPECT_EQ(view->resolvedCursor(), ::LoadCursorW(nullptr, IDC_HAND));

    delete view;
}

TEST(ViewCursor, SetCursorWithACustomImageResolvesToARealBuiltHandle) {
    auto* view = new newui::SubView();
    BLImage image(16, 16, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(255, 0, 0, 255));
    ctx.fill_all();
    ctx.end();

    view->setCursor(newui::Cursor(image));

    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Custom);
    EXPECT_NE(view->resolvedCursor(), nullptr);

    delete view;
}

TEST(ViewCursor, SettingCursorKindAfterCustomClearsTheCustomHandle) {
    auto* view = new newui::SubView();
    BLImage image(16, 16, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(255, 0, 0, 255));
    ctx.fill_all();
    ctx.end();
    view->setCursor(newui::Cursor(image));

    view->cursor().setCursorKind(newui::CursorKind::IBeam);

    EXPECT_EQ(view->cursorKind(), newui::CursorKind::IBeam);
    EXPECT_EQ(view->resolvedCursor(), ::LoadCursorW(nullptr, IDC_IBEAM));

    delete view;
}

// ---------------------------------------------------------------------------
// cursor().setPath() - the PNG-loading path plumbed through View via its
// Cursor member, including Cursor owning (and releasing, via its own
// destructor - see cursor.h) whatever it loads. Writes a real, tiny PNG
// to disk via BLImage::write_to_file() for each case - same "create the
// on-disk fixture the test needs, then delete it" convention
// test_bundle.cpp uses for its own resource-file tests.
// ---------------------------------------------------------------------------

namespace {

void WriteTestCursorPNG(const std::string& path, int width, int height) {
    BLImage image(width, height, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(0, 0, 255, 128));
    ctx.fill_all();
    ctx.end();
    ASSERT_EQ(image.write_to_file(path.c_str()), BL_SUCCESS);
}

}  // namespace

TEST(ViewCursor, SetPathAdoptsTheLoadedCursorAsCustom) {
    const std::string path = "view_cursor_test_small.png";
    WriteTestCursorPNG(path, 16, 16);

    auto* view = new newui::SubView();
    EXPECT_TRUE(view->cursor().setPath(path));
    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Custom);
    EXPECT_EQ(view->cursor().path(), path);
    EXPECT_NE(view->resolvedCursor(), nullptr);

    delete view;  // ~View() -> ~Cursor() releases the owned handle - crash/leak-under-a-sanitizer is the only way this could fail
    ::DeleteFileA(path.c_str());
}

TEST(ViewCursor, SetPathFailureLeavesTheExistingCursorUnchanged) {
    auto* view = new newui::SubView();
    view->cursor().setCursorKind(newui::CursorKind::Wait);

    EXPECT_FALSE(view->cursor().setPath("NoSuchCursorFile.png"));

    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Wait);
    EXPECT_EQ(view->resolvedCursor(), ::LoadCursorW(nullptr, IDC_WAIT));

    delete view;
}

TEST(ViewCursor, SetPathFailsWhenThePNGExceedsMaxSize) {
    const std::string path = "view_cursor_test_large.png";
    WriteTestCursorPNG(path, 40, 40);

    auto* view = new newui::SubView();
    EXPECT_FALSE(view->cursor().setPath(path));
    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Arrow);  // untouched default

    delete view;
    ::DeleteFileA(path.c_str());
}

TEST(ViewCursor, ReplacingASetPathCursorReleasesTheOwnedHandle) {
    const std::string path = "view_cursor_test_replace.png";
    WriteTestCursorPNG(path, 16, 16);

    auto* view = new newui::SubView();
    ASSERT_TRUE(view->cursor().setPath(path));

    // Should ::DestroyCursor() the previously-owned handle rather than
    // leaking it - no direct observable side effect here beyond "doesn't
    // crash", but the resulting state should be exactly as if setPath()
    // had never been called.
    view->cursor().setCursorKind(newui::CursorKind::Arrow);

    EXPECT_EQ(view->cursorKind(), newui::CursorKind::Arrow);
    EXPECT_EQ(view->resolvedCursor(), ::LoadCursorW(nullptr, IDC_ARROW));

    delete view;
    ::DeleteFileA(path.c_str());
}

// ---------------------------------------------------------------------------
// hitTestChildren() - pure geometry, no RootView/live window involved.
// RootView::mouseDown()/mouseMove()/etc. (rootview.cpp) are what actually
// call this to route real input to the right SubView - covered separately
// in unittests/test_rootview.cpp.
// ---------------------------------------------------------------------------

TEST(HitTestChildren, ReturnsNullptrWithNoChildren) {
    auto* view = new newui::SubView();
    view->setBounds(newui::Rect(0, 0, 100, 100));

    newui::Point outLocalPt;
    EXPECT_EQ(view->hitTestChildren(newui::Point(50, 50), outLocalPt), nullptr);

    delete view;
}

TEST(HitTestChildren, ReturnsNullptrWhenPointMissesEveryChild) {
    auto* view = new newui::SubView();
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 10, 20, 20));
    child->setVisible(true);
    view->addChild(child);

    newui::Point outLocalPt;
    EXPECT_EQ(view->hitTestChildren(newui::Point(5, 5), outLocalPt), nullptr);

    delete child;
    delete view;
}

TEST(HitTestChildren, PointOverChildReturnsItWithTranslatedLocalPoint) {
    auto* view = new newui::SubView();
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(10, 20, 50, 40));
    child->setVisible(true);
    view->addChild(child);

    newui::Point outLocalPt;
    newui::SubView* hit = view->hitTestChildren(newui::Point(15, 25), outLocalPt);

    ASSERT_EQ(hit, child);
    EXPECT_FLOAT_EQ(outLocalPt.x, 5.0f);
    EXPECT_FLOAT_EQ(outLocalPt.y, 5.0f);

    delete child;
    delete view;
}

TEST(HitTestChildren, SkipsInvisibleChildren) {
    auto* view = new newui::SubView();
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(false);
    view->addChild(child);

    newui::Point outLocalPt;
    EXPECT_EQ(view->hitTestChildren(newui::Point(10, 10), outLocalPt), nullptr);

    delete child;
    delete view;
}

TEST(HitTestChildren, OverlappingChildrenPreferTheLastAddedOne) {
    // Matches paintChildren()'s draw order - a later-added child paints
    // over an earlier one, so it should also win the hit test.
    auto* view = new newui::SubView();
    auto* back = new newui::SubView();
    back->setBounds(newui::Rect(0, 0, 50, 50));
    back->setVisible(true);
    view->addChild(back);

    auto* front = new newui::SubView();
    front->setBounds(newui::Rect(0, 0, 50, 50));
    front->setVisible(true);
    view->addChild(front);

    newui::Point outLocalPt;
    EXPECT_EQ(view->hitTestChildren(newui::Point(10, 10), outLocalPt), front);

    delete front;
    delete back;
    delete view;
}

TEST(HitTestChildren, RecursesIntoNestedChildrenAndTranslatesAllTheWayDown) {
    auto* root = new newui::SubView();
    auto* mid = new newui::SubView();
    mid->setBounds(newui::Rect(10, 10, 80, 80));
    mid->setVisible(true);
    root->addChild(mid);

    auto* leaf = new newui::SubView();
    leaf->setBounds(newui::Rect(5, 5, 20, 20));  // local to mid
    leaf->setVisible(true);
    mid->addChild(leaf);

    // root-local (23, 23) -> mid-local (13, 13) -> leaf-local (8, 8)
    newui::Point outLocalPt;
    newui::SubView* hit = root->hitTestChildren(newui::Point(23, 23), outLocalPt);

    ASSERT_EQ(hit, leaf);
    EXPECT_FLOAT_EQ(outLocalPt.x, 8.0f);
    EXPECT_FLOAT_EQ(outLocalPt.y, 8.0f);

    delete leaf;
    delete mid;
    delete root;
}

TEST(HitTestChildren, PointOverParentButOutsideNestedChildReturnsParent) {
    auto* root = new newui::SubView();
    auto* mid = new newui::SubView();
    mid->setBounds(newui::Rect(10, 10, 80, 80));
    mid->setVisible(true);
    root->addChild(mid);

    auto* leaf = new newui::SubView();
    leaf->setBounds(newui::Rect(5, 5, 20, 20));
    leaf->setVisible(true);
    mid->addChild(leaf);

    // root-local (50, 50) -> mid-local (40, 40), well outside leaf's bounds
    newui::Point outLocalPt;
    newui::SubView* hit = root->hitTestChildren(newui::Point(50, 50), outLocalPt);

    ASSERT_EQ(hit, mid);
    EXPECT_FLOAT_EQ(outLocalPt.x, 40.0f);
    EXPECT_FLOAT_EQ(outLocalPt.y, 40.0f);

    delete leaf;
    delete mid;
    delete root;
}

// ---------------------------------------------------------------------------
// destroy() - regression coverage for a real crash: View::destroy() used
// to iterate childViews_ with a live range-based for loop while each
// child->destroy() removed itself from that same vector
// (SubView::destroy()'s parent_->removeChild(this)) - iterator
// invalidation that happened to survive exactly one child (by luck) but
// crashed/corrupted with two or more. Fixed by snapshotting childViews_
// before iterating (view.cpp).
// ---------------------------------------------------------------------------

TEST(ViewDestroy, DestroysAllChildrenWithoutCorruptingIteration) {
    g_destroyedCount = 0;

    auto* container = new newui::SubView();
    auto* childA = new newui::SubView();
    auto* childB = new newui::SubView();
    auto* childC = new newui::SubView();
    childA->onDestroyed += RecordDestroyed;
    childB->onDestroyed += RecordDestroyed;
    childC->onDestroyed += RecordDestroyed;

    container->addChild(childA);
    container->addChild(childB);
    container->addChild(childC);

    container->destroy();

    EXPECT_EQ(g_destroyedCount, 3);

    delete container;
}

TEST(ViewDestroy, DestroysGrandchildrenToo) {
    g_destroyedCount = 0;

    auto* root = new newui::SubView();
    auto* childA = new newui::SubView();
    auto* childB = new newui::SubView();
    auto* grandchild = new newui::SubView();
    grandchild->onDestroyed += RecordDestroyed;

    childA->addChild(grandchild);
    root->addChild(childA);
    root->addChild(childB);

    root->destroy();

    EXPECT_EQ(g_destroyedCount, 1);

    delete root;
}

TEST(ViewDestroy, DestroysDirectRootViewChildrenWithoutCorruptingIteration) {
    // Regression test for the specific gap the fix above didn't originally
    // cover: SubView::parent_ used to only ever be set by SubView::
    // addChild() - a SubView attached directly to a RootView (the common
    // case, e.g. root.addChild(sidebar) in examples/layout1.cpp) never had
    // parent_ set at all, so SubView::destroy()'s self-removal
    // (parent_->removeChild(this)) silently never fired for it.
    // RootView::addChild() now calls setParent() too - without that,
    // View::destroy()'s front()-popping loop would never see childViews_
    // shrink and would re-process (and double-delete) the same already-
    // destroyed pointer.
    g_destroyedCount = 0;

    // No live Win32 window needed - passing a null Frame* is fine as long
    // as initialize() (which requires one) is never called; RootView's
    // destructor doesn't throw the way Frame's does (see HANDOFF.md).
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "root");

    auto* childA = new newui::SubView();
    auto* childB = new newui::SubView();
    auto* childC = new newui::SubView();
    childA->onDestroyed += RecordDestroyed;
    childB->onDestroyed += RecordDestroyed;
    childC->onDestroyed += RecordDestroyed;

    root->addChild(childA);
    root->addChild(childB);
    root->addChild(childC);

    root->destroy();

    EXPECT_EQ(g_destroyedCount, 3);

    delete root;
}

TEST(ViewPropagateRootView, SetsItOnASingleView) {
    // A real RootView, not a sentinel pointer - propagateRootView() now
    // genuinely dereferences root (to auto-assign a default name via
    // RootView::generateDefaultName() when name() is still "") - see
    // SubViewRemoveChild.PropagatesNullToWholeDetachedSubtree below for
    // the same "real RootView, frame=nullptr, never initialize()'d"
    // pattern used for the same reason.
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "root");
    auto* view = new newui::SubView();

    view->propagateRootView(root);

    EXPECT_EQ(view->rootView(), root);

    delete view;
    delete root;
}

TEST(ViewPropagateRootView, NullClearsIt) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "root");
    auto* view = new newui::SubView();
    view->propagateRootView(root);

    view->propagateRootView(nullptr);

    EXPECT_EQ(view->rootView(), nullptr);

    delete view;
    delete root;
}

TEST(SubViewAddChild, PropagatesRootViewToNewChild) {
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "root");
    auto* parent = new newui::SubView();
    auto* child = new newui::SubView();
    parent->propagateRootView(root);

    parent->addChild(child);

    EXPECT_EQ(child->rootView(), root);

    delete child;
    delete parent;
    delete root;
}

TEST(SubViewAddChild, PropagatesRootViewToPreexistingGrandchildren) {
    // Build a subtree (grandchild under child) BEFORE either has a
    // RootView - the gap this guards against: attaching just the
    // immediate child to a rooted parent used to leave already-existing
    // descendants (grandchild here) with a stale/null rootView().
    auto* grandchild = new newui::SubView();
    auto* child = new newui::SubView();
    child->addChild(grandchild);

    EXPECT_EQ(grandchild->rootView(), nullptr);  // nothing rooted yet

    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "root");
    auto* parent = new newui::SubView();
    parent->propagateRootView(root);

    parent->addChild(child);

    EXPECT_EQ(child->rootView(), root);
    EXPECT_EQ(grandchild->rootView(), root);  // propagated through

    delete grandchild;
    delete child;
    delete parent;
    delete root;
}

TEST(SubViewRemoveChild, PropagatesNullToWholeDetachedSubtree) {
    // A real RootView, not SentinelRoot() - see its doc comment above:
    // removeChild() now dereferences rootView() for real.
    auto* root = new newui::RootView(nullptr, newui::Rect(0, 0, 100, 100), "root");

    auto* grandchild = new newui::SubView();
    auto* child = new newui::SubView();
    child->addChild(grandchild);

    auto* parent = new newui::SubView();
    root->addChild(parent);
    parent->addChild(child);
    ASSERT_EQ(grandchild->rootView(), root);

    parent->removeChild(child);

    EXPECT_EQ(child->rootView(), nullptr);
    EXPECT_EQ(grandchild->rootView(), nullptr);

    delete grandchild;
    delete child;
    root->destroy();
    delete root;
}

// Rect::snappedOutwardToPixels() - see its own comment (geometry.h) for
// why View::paintChildren() needs this for every child's bounds before
// translating/clipping by them: a themed child (ThemedViewStyle::paint(),
// viewstyle.cpp) positioned at a generically-fractional coordinate (e.g.
// a Slider's thumb) otherwise leaves ctx's clip with fractional edges,
// which trips a real BL_ASSERT(is_rect_fill()) in Blend2D's
// fetchpatternpart.cpp - a real, reproduced crash this fixes.

TEST(RectSnappedOutwardToPixels, AlreadyWholePixelRectIsUnchanged) {
    newui::Rect r(10.0f, 20.0f, 30.0f, 40.0f);
    EXPECT_EQ(r.snappedOutwardToPixels(), r);
}

TEST(RectSnappedOutwardToPixels, FractionalEdgesFloorLeadingAndCeilTrailing) {
    newui::Rect r(10.25f, 20.75f, 30.5f, 40.5f);  // right = 40.75, bottom = 61.25
    newui::Rect snapped = r.snappedOutwardToPixels();

    EXPECT_FLOAT_EQ(snapped.left(), 10.0f);
    EXPECT_FLOAT_EQ(snapped.top(), 20.0f);
    EXPECT_FLOAT_EQ(snapped.right(), 41.0f);
    EXPECT_FLOAT_EQ(snapped.bottom(), 62.0f);
}

TEST(RectSnappedOutwardToPixels, NegativeOriginStillFloorsTowardNegativeInfinity) {
    newui::Rect r(-10.25f, -20.75f, 5.0f, 5.0f);
    newui::Rect snapped = r.snappedOutwardToPixels();

    EXPECT_FLOAT_EQ(snapped.left(), -11.0f);
    EXPECT_FLOAT_EQ(snapped.top(), -21.0f);
}

TEST(RectSnappedOutwardToPixels, ResultNeverShrinksTheOriginalRect) {
    // "Outward" - every edge only ever moves away from the rect's own
    // center, so nothing real gets clipped off by the snap itself.
    newui::Rect r(10.1f, 20.9f, 30.3f, 40.7f);
    newui::Rect snapped = r.snappedOutwardToPixels();

    EXPECT_LE(snapped.left(), r.left());
    EXPECT_LE(snapped.top(), r.top());
    EXPECT_GE(snapped.right(), r.right());
    EXPECT_GE(snapped.bottom(), r.bottom());
}

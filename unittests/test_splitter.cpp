#include "newui/splitter.h"
#include "newui/subview.h"
#include "newui/layout.h"

#include <memory>

#include <gtest/gtest.h>

namespace {

newui::SubView* addPlainChild(newui::Splitter* splitter) {
    auto* child = new newui::SubView();
    splitter->addChild(child);
    return child;
}

// Records how many times arrange() ran and what container size it last saw
// - enough to prove a pane's own attached Layout gets re-invoked when
// Splitter resizes that pane, without depending on any particular real
// Layout's own sizing math (FlexLayout's, in particular, has cross-axis/
// weight subtleties already covered by test_layout.cpp - irrelevant to
// what Splitter itself needs to prove here).
class SpyLayout : public newui::Layout {
public:
    int arrangeCount = 0;
    newui::Size lastContainerSize;

    void arrange(newui::View& container) override {
        ++arrangeCount;
        lastContainerSize = container.getClientBounds().size();
    }
};

}  // namespace

TEST(Splitter, DefaultsToHorizontalOrientation) {
    auto* splitter = new newui::Splitter();
    EXPECT_EQ(splitter->orientation(), newui::Orientation::Horizontal);
    delete splitter;
}

TEST(Splitter, AddingTwoChildrenArrangesThemLeftAndRightByDefault) {
    auto* splitter = new newui::Splitter();
    splitter->setBounds(newui::Rect(0, 0, 400, 200));

    auto* first = addPlainChild(splitter);
    auto* second = addPlainChild(splitter);

    EXPECT_FLOAT_EQ(first->bounds().left(), 0.0f);
    EXPECT_FLOAT_EQ(first->bounds().size().width, splitter->splitPosition());
    EXPECT_FLOAT_EQ(first->bounds().size().height, 200.0f);

    float expectedSecondX = splitter->splitPosition() + splitter->dividerThickness();
    EXPECT_FLOAT_EQ(second->bounds().left(), expectedSecondX);
    EXPECT_FLOAT_EQ(second->bounds().size().width, 400.0f - expectedSecondX);
    EXPECT_FLOAT_EQ(second->bounds().size().height, 200.0f);

    delete splitter;
}

TEST(Splitter, VerticalOrientationArrangesTopAndBottomInstead) {
    auto* splitter = new newui::Splitter(newui::Orientation::Vertical);
    splitter->setBounds(newui::Rect(0, 0, 200, 400));

    auto* first = addPlainChild(splitter);
    auto* second = addPlainChild(splitter);

    EXPECT_FLOAT_EQ(first->bounds().top(), 0.0f);
    EXPECT_FLOAT_EQ(first->bounds().size().height, splitter->splitPosition());
    EXPECT_FLOAT_EQ(first->bounds().size().width, 200.0f);

    float expectedSecondY = splitter->splitPosition() + splitter->dividerThickness();
    EXPECT_FLOAT_EQ(second->bounds().top(), expectedSecondY);
    EXPECT_FLOAT_EQ(second->bounds().size().height, 400.0f - expectedSecondY);

    delete splitter;
}

TEST(Splitter, ResizingTheSplitterReArrangesBothPanes) {
    auto* splitter = new newui::Splitter();
    splitter->setBounds(newui::Rect(0, 0, 400, 200));
    auto* first = addPlainChild(splitter);
    auto* second = addPlainChild(splitter);
    float originalSplit = splitter->splitPosition();

    splitter->setBounds(newui::Rect(0, 0, 800, 300));

    EXPECT_FLOAT_EQ(first->bounds().size().width, originalSplit);  // splitPosition() is absolute, unchanged
    EXPECT_FLOAT_EQ(first->bounds().size().height, 300.0f);
    EXPECT_FLOAT_EQ(second->bounds().size().width, 800.0f - originalSplit - splitter->dividerThickness());
    EXPECT_FLOAT_EQ(second->bounds().size().height, 300.0f);

    delete splitter;
}

TEST(Splitter, SetSplitPositionClampsToMinPaneSize) {
    auto* splitter = new newui::Splitter();
    splitter->setBounds(newui::Rect(0, 0, 400, 200));
    splitter->setMinPaneSize(40.0f);

    splitter->setSplitPosition(-100.0f);
    EXPECT_FLOAT_EQ(splitter->splitPosition(), 40.0f);

    splitter->setSplitPosition(10000.0f);
    EXPECT_FLOAT_EQ(splitter->splitPosition(), 400.0f - splitter->dividerThickness() - 40.0f);

    delete splitter;
}

TEST(Splitter, DraggingTheDividerMovesTheSplitAndResizesBothPanes) {
    auto* splitter = new newui::Splitter();
    splitter->setBounds(newui::Rect(0, 0, 400, 200));
    auto* first = addPlainChild(splitter);
    auto* second = addPlainChild(splitter);

    float dividerCenter = splitter->splitPosition() + splitter->dividerThickness() * 0.5f;
    splitter->onMouseDown(*splitter, newui::Point(dividerCenter, 100.0f), 1, 0);
    splitter->onMouseMove(*splitter, newui::Point(300.0f, 100.0f), 1, 0);
    splitter->onMouseUp(*splitter, newui::Point(300.0f, 100.0f), 1, 0);

    EXPECT_NEAR(splitter->splitPosition(), 300.0f - splitter->dividerThickness() * 0.5f, 0.01f);
    EXPECT_FLOAT_EQ(first->bounds().size().width, splitter->splitPosition());

    delete splitter;
}

TEST(Splitter, MouseDownOutsideTheDividerIsIgnoredAndStartsNoDrag) {
    auto* splitter = new newui::Splitter();
    splitter->setBounds(newui::Rect(0, 0, 400, 200));
    addPlainChild(splitter);
    addPlainChild(splitter);
    float originalSplit = splitter->splitPosition();

    newui::SyncReturn result = splitter->onMouseDown.syncCallFirst(*splitter, newui::Point(5.0f, 5.0f), 1, 0);
    EXPECT_EQ(result, newui::SyncReturn::Ignored);

    // A move afterward (no preceding successful mouseDown on the divider)
    // must not move the split - proves dragging_ never got set.
    splitter->onMouseMove(*splitter, newui::Point(300.0f, 100.0f), 1, 0);
    EXPECT_FLOAT_EQ(splitter->splitPosition(), originalSplit);

    delete splitter;
}

TEST(Splitter, ResizingAPaneCascadesIntoThatPanesOwnAttachedLayout) {
    // Proves the resize-propagation mechanism Splitter itself relies on:
    // arrangePanes() just calls setBounds() on each pane, same as any
    // other container - SubView::setBounds() already fires onSizeChanged
    // and calls updateLayout() (subview.cpp), which re-runs a pane's own
    // attached Layout automatically. No special notification from Splitter
    // to its children (or their children) is needed beyond that.
    auto* splitter = new newui::Splitter();
    splitter->setBounds(newui::Rect(0, 0, 400, 200));

    auto* firstPane = addPlainChild(splitter);
    auto spyLayout = std::make_unique<SpyLayout>();
    SpyLayout* spy = spyLayout.get();
    firstPane->setLayout(std::move(spyLayout));  // fires once immediately (setLayout()'s own updateLayout())

    int countAfterSetLayout = spy->arrangeCount;

    // Taller, not just wider - firstPane's width is splitPosition_ (an
    // absolute pixel offset, unaffected by a width-only resize unless it
    // no longer fits), but its height always tracks the Splitter's own
    // height directly, so this is guaranteed to actually change
    // firstPane's bounds (and SubView::setBounds() no-ops on an unchanged
    // rect - see its own early-out).
    splitter->setBounds(newui::Rect(0, 0, 400, 500));

    EXPECT_GT(spy->arrangeCount, countAfterSetLayout);
    EXPECT_FLOAT_EQ(spy->lastContainerSize.height, firstPane->bounds().size().height);
    EXPECT_FLOAT_EQ(firstPane->bounds().size().height, 500.0f);

    delete splitter;
}

TEST(Splitter, SetSplitPositionBeforeRealBoundsSurvivesUntilTheyArrive) {
    // Reproduces newui::ViewBuilder<Splitter>::configure(fn) calling
    // setSplitPosition() right after construction, before this Splitter is
    // attached to anything or has ever been given real bounds (bounds_
    // defaults to {0,0,0,0} - View's own default). clampSplitPosition()
    // used to treat that degenerate size as "not enough room for two real
    // panes yet" and permanently overwrite splitPosition_ with a value
    // computed from today's zero bounds - unrecoverable once real bounds
    // later arrived, since setBounds() below only ever re-clamps
    // (shrinks/floors), never grows a too-small stored value back up. Real
    // bug: every Workspace pane split (cpp_codetools) configured this way
    // silently collapsed to minPaneSize() instead of the intended split.
    auto* splitter = new newui::Splitter();
    splitter->setSplitPosition(560.0f);  // still {0,0,0,0} bounds at this point

    splitter->setBounds(newui::Rect(0, 0, 800, 200));  // real bounds finally arrive

    EXPECT_FLOAT_EQ(splitter->splitPosition(), 560.0f);

    delete splitter;
}

TEST(Splitter, DefaultsToFixedPaneFirst) {
    auto* splitter = new newui::Splitter();
    EXPECT_EQ(splitter->fixedPane(), newui::SplitterFixedPane::First);
    delete splitter;
}

TEST(Splitter, FixedPaneSecondMakesTheSecondPaneStayFixedSizeOnResize) {
    // The standard docking-IDE convention (a fixed-size dock, flexible
    // content filling the rest) needs the *opposite* of Splitter's default
    // resize behavior when the dock is pane[1] (e.g. a right-hand
    // properties panel) - real gap found via CodeToolsVsix::Workspace's
    // own centerAndRight/middle Splitters both needing this.
    auto* splitter = new newui::Splitter();
    splitter->setFixedPane(newui::SplitterFixedPane::Second);
    splitter->setSplitPosition(100.0f);  // now means pane[1]'s own fixed size
    splitter->setBounds(newui::Rect(0, 0, 400, 200));

    auto* first = addPlainChild(splitter);
    auto* second = addPlainChild(splitter);

    EXPECT_FLOAT_EQ(second->bounds().size().width, 100.0f);
    EXPECT_FLOAT_EQ(first->bounds().size().width, 400.0f - 100.0f - splitter->dividerThickness());

    // A pure resize (no drag) must keep pane[1] pinned at 100 and grow
    // pane[0] to absorb all of it - the opposite of fixedPane() == First's
    // "pane[0] absolute, pane[1] absorbs" default.
    splitter->setBounds(newui::Rect(0, 0, 800, 200));
    EXPECT_FLOAT_EQ(second->bounds().size().width, 100.0f);
    EXPECT_FLOAT_EQ(first->bounds().size().width, 800.0f - 100.0f - splitter->dividerThickness());

    delete splitter;
}

TEST(Splitter, DraggingTheDividerInFixedPaneSecondModeStillTracksTheCursor) {
    auto* splitter = new newui::Splitter();
    splitter->setFixedPane(newui::SplitterFixedPane::Second);
    splitter->setSplitPosition(100.0f);
    splitter->setBounds(newui::Rect(0, 0, 400, 200));
    auto* first = addPlainChild(splitter);
    auto* second = addPlainChild(splitter);

    // Divider sits at pane[0]'s far edge: 400 - 100 - thickness from the
    // left - same dividerRect() the mouseDown hit-test itself uses.
    float dividerNear = 400.0f - 100.0f - splitter->dividerThickness();
    float dividerCenter = dividerNear + splitter->dividerThickness() * 0.5f;

    splitter->onMouseDown(*splitter, newui::Point(dividerCenter, 100.0f), 1, 0);
    splitter->onMouseMove(*splitter, newui::Point(dividerCenter + 50.0f, 100.0f), 1, 0);  // drag right
    splitter->onMouseUp(*splitter, newui::Point(dividerCenter + 50.0f, 100.0f), 1, 0);

    // Dragging right grows pane[0] (left) and shrinks the fixed-Second
    // pane[1] (right) by the same amount - splitPosition() now means
    // pane[1]'s size, so it should have dropped by ~50.
    EXPECT_NEAR(splitter->splitPosition(), 50.0f, 0.01f);
    EXPECT_FLOAT_EQ(second->bounds().size().width, splitter->splitPosition());
    EXPECT_FLOAT_EQ(first->bounds().size().width, 400.0f - splitter->splitPosition() - splitter->dividerThickness());

    delete splitter;
}

TEST(Splitter, MouseMoveWithNoActiveDragIsIgnored) {
    auto* splitter = new newui::Splitter();
    splitter->setBounds(newui::Rect(0, 0, 400, 200));

    newui::SyncReturn result = splitter->onMouseMove.syncCallFirst(*splitter, newui::Point(300.0f, 100.0f), 0, 0);
    EXPECT_EQ(result, newui::SyncReturn::Ignored);

    delete splitter;
}

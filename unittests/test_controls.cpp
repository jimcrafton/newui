#include "newui/controls.h"

#include <gtest/gtest.h>

// ScrollBar/ScrollView's pure logic (range/value/pageSize clamping,
// region-driven click behavior) is fully headless-testable via direct
// onMouseDown()/onMouseMove()/onMouseUp() invocation, same
// simulated-event pattern test_controllers.cpp's Control tests already
// use - no live HWND/RootView/RunLoop needed for any of this. Not
// covered here: arrow/track-click auto-repeat (ScrollBar::startRepeat())
// only actually fires anything once a live, pumped RunLoop processes the
// postIdle task it queues - same "needs a real message pump" gap already
// documented for ViewController's animated present() (test_controllers.cpp)
// and ThemedViewStyle::paint() elsewhere in this suite; queuing the task
// itself is still exercised indirectly (it doesn't crash/throw) by every
// click test below.

using namespace newui;

namespace {

// ScrollBar's natural arrow-button length falls back to a fixed constant
// (kArrowFallbackSize, controls.cpp) whenever no theme is cached yet -
// always true here, headless - so a 200-tall vertical bar reserves 16px
// for each arrow, leaving a 168px track between them.
constexpr float kArrowSize = 16.0f;

}  // namespace

// ---------------------------------------------------------------------
// ScrollBar - range/value/pageSize/lineStep
// ---------------------------------------------------------------------

TEST(ScrollBar, DefaultsAreSaneRangeAndStep) {
    auto* bar = new ScrollBar();

    EXPECT_FLOAT_EQ(bar->value(), 0.0f);
    EXPECT_FLOAT_EQ(bar->minValue(), 0.0f);
    EXPECT_FLOAT_EQ(bar->maxValue(), 100.0f);
    EXPECT_FLOAT_EQ(bar->pageSize(), 10.0f);
    EXPECT_FLOAT_EQ(bar->lineStep(), 1.0f);
    EXPECT_FALSE(bar->isHorizontal());

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, SetValueClampsToEffectiveRange) {
    auto* bar = new ScrollBar();
    bar->setRange(0.0f, 100.0f);
    bar->setPageSize(20.0f);  // effective range is [0, 100-20] = [0, 80]

    bar->setValue(1000.0f);
    EXPECT_FLOAT_EQ(bar->value(), 80.0f);

    bar->setValue(-50.0f);
    EXPECT_FLOAT_EQ(bar->value(), 0.0f);

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, SetPageSizeClampsToFullRangeAndReclampsValue) {
    auto* bar = new ScrollBar();
    bar->setRange(0.0f, 50.0f);

    // Larger than the full [min,max] span - clamps to that span, not the
    // requested value.
    bar->setPageSize(1000.0f);
    EXPECT_FLOAT_EQ(bar->pageSize(), 50.0f);
    // Effective range is now [0, 50-50] = [0,0] - value has nowhere to go.
    EXPECT_FLOAT_EQ(bar->value(), 0.0f);

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, SetRangeReclampsExistingValue) {
    auto* bar = new ScrollBar();
    bar->setRange(0.0f, 100.0f);
    bar->setPageSize(10.0f);
    bar->setValue(90.0f);
    ASSERT_FLOAT_EQ(bar->value(), 90.0f);

    // New effective max is 50-10=40 - the existing value() no longer fits.
    bar->setRange(0.0f, 50.0f);
    EXPECT_FLOAT_EQ(bar->value(), 40.0f);

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, OnValueChangedFiresOnlyOnActualChange) {
    auto* bar = new ScrollBar();
    bar->setRange(0.0f, 100.0f);

    int fireCount = 0;
    bar->onValueChanged.add([&fireCount](ScrollBar&) {
        ++fireCount;
        return SyncReturn::Handled;
    });

    bar->setValue(50.0f);
    EXPECT_EQ(fireCount, 1);

    bar->setValue(50.0f);  // unchanged - no-op
    EXPECT_EQ(fireCount, 1);

    bar->setValue(60.0f);
    EXPECT_EQ(fireCount, 2);

    bar->destroy();
    delete bar;
}

// ---------------------------------------------------------------------
// ScrollBar - mouse interaction (arrow click, track-click paging, thumb
// drag) - all driven by directly invoking the same onMouseDown/onMouseMove/
// onMouseUp delegates RootView would, see this file's own top comment.
// ---------------------------------------------------------------------

TEST(ScrollBar, ClickingDownArrowStepsForwardByLineStep) {
    auto* bar = new ScrollBar();
    bar->setBounds(Rect(0, 0, 20, 200));
    bar->setRange(0.0f, 100.0f);
    bar->setPageSize(20.0f);
    bar->setLineStep(5.0f);
    bar->setValue(50.0f);

    // Down arrow occupies the bottom kArrowSize px.
    bar->onMouseDown(*bar, Point(10.0f, 195.0f), 1, 0);
    EXPECT_FLOAT_EQ(bar->value(), 55.0f);

    bar->onMouseUp(*bar, Point(10.0f, 195.0f), 1, 0);

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, ClickingUpArrowStepsBackwardByLineStep) {
    auto* bar = new ScrollBar();
    bar->setBounds(Rect(0, 0, 20, 200));
    bar->setRange(0.0f, 100.0f);
    bar->setPageSize(20.0f);
    bar->setLineStep(5.0f);
    bar->setValue(50.0f);

    // Up arrow occupies the top kArrowSize px.
    bar->onMouseDown(*bar, Point(10.0f, 5.0f), 1, 0);
    EXPECT_FLOAT_EQ(bar->value(), 45.0f);

    bar->onMouseUp(*bar, Point(10.0f, 5.0f), 1, 0);

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, ClickingTrackPagesTowardTheClickSide) {
    auto* bar = new ScrollBar();
    bar->setBounds(Rect(0, 0, 20, 200));
    bar->setRange(0.0f, 100.0f);
    bar->setPageSize(20.0f);
    // value 0 puts the thumb flush against the top of the track (just
    // below the up arrow) - clicking near the bottom of the track (still
    // above the down arrow) is unambiguously on the far side of the
    // thumb from this position.
    bar->setValue(0.0f);

    bar->onMouseDown(*bar, Point(10.0f, 170.0f), 1, 0);
    EXPECT_FLOAT_EQ(bar->value(), 20.0f);  // one pageSize() forward

    bar->onMouseUp(*bar, Point(10.0f, 170.0f), 1, 0);

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, DraggingThumbMovesValueInTheDragDirection) {
    auto* bar = new ScrollBar();
    bar->setBounds(Rect(0, 0, 20, 200));
    bar->setRange(0.0f, 100.0f);
    // A large pageSize() (80% of the full range) gives the thumb a large,
    // easy-to-hit footprint - deliberately not asserting an exact pixel-
    // derived value() (that would just re-encode ScrollBar's own mapping
    // formula into the test), only that dragging down moves value() up
    // meaningfully and dragging back up brings it back down.
    bar->setPageSize(80.0f);
    bar->setValue(0.0f);  // thumb starts flush against the track's top

    // A point near the top of the thumb's own footprint (track starts at
    // kArrowSize=16).
    bar->onMouseDown(*bar, Point(10.0f, kArrowSize + 5.0f), 1, 0);
    bar->onMouseMove(*bar, Point(10.0f, 150.0f), 1, 0);
    float draggedDownValue = bar->value();
    EXPECT_GT(draggedDownValue, 5.0f);

    bar->onMouseMove(*bar, Point(10.0f, kArrowSize + 5.0f), 1, 0);
    EXPECT_LT(bar->value(), draggedDownValue);

    bar->onMouseUp(*bar, Point(10.0f, kArrowSize + 5.0f), 1, 0);

    bar->destroy();
    delete bar;
}

TEST(ScrollBar, MouseMoveWithoutAPrecedingMouseDownIsIgnored) {
    auto* bar = new ScrollBar();
    bar->setBounds(Rect(0, 0, 20, 200));
    bar->setRange(0.0f, 100.0f);
    bar->setPageSize(20.0f);
    bar->setValue(50.0f);

    // No drag in progress - a stray move (e.g. plain hover) must not
    // change value().
    bar->onMouseMove(*bar, Point(10.0f, 150.0f), 0, 0);
    EXPECT_FLOAT_EQ(bar->value(), 50.0f);

    bar->destroy();
    delete bar;
}

// ---------------------------------------------------------------------
// ScrollView - bar visibility/sizing and origin() wiring
// ---------------------------------------------------------------------

TEST(ScrollView, BarsHiddenWhenContentFitsViewport) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));
    view->setContentSize(Size(100.0f, 100.0f));

    EXPECT_FALSE(view->vBar()->isVisible());
    EXPECT_FALSE(view->hBar()->isVisible());

    view->destroy();
    delete view;
}

TEST(ScrollView, BarsShownAndRangedWhenContentExceedsViewport) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));
    view->setContentSize(Size(500.0f, 800.0f));

    ASSERT_TRUE(view->vBar()->isVisible());
    ASSERT_TRUE(view->hBar()->isVisible());
    EXPECT_FLOAT_EQ(view->vBar()->maxValue(), 800.0f);
    EXPECT_FLOAT_EQ(view->hBar()->maxValue(), 500.0f);
    // Each bar's pageSize() is however much of that axis the viewport
    // actually shows - strictly less than the full viewport since the
    // *other* bar reserves some of it.
    EXPECT_GT(view->vBar()->pageSize(), 0.0f);
    EXPECT_LT(view->vBar()->pageSize(), 200.0f);

    view->destroy();
    delete view;
}

TEST(ScrollView, BarValueChangesUpdateContentOrigin) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));
    view->setContentSize(Size(500.0f, 800.0f));
    ASSERT_TRUE(view->vBar()->isVisible());
    ASSERT_TRUE(view->hBar()->isVisible());

    view->vBar()->setValue(123.0f);
    EXPECT_FLOAT_EQ(view->contentOrigin().y, 123.0f);

    view->hBar()->setValue(77.0f);
    EXPECT_FLOAT_EQ(view->contentOrigin().x, 77.0f);

    view->destroy();
    delete view;
}

TEST(ScrollView, AddChildDoesNotBecomeADirectChildOfScrollViewItself) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));

    auto* content = new SubView();
    content->setVisible(true);
    view->addChild(content);

    // content's parent is viewport_ (ScrollView's internal chrome), never
    // ScrollView itself - see ScrollView::addChild()'s own doc comment
    // (controls.h) for why that distinction matters (keeps vBar()/hBar()
    // pinned in place regardless of scroll position).
    EXPECT_NE(content->parent(), static_cast<View*>(view));

    view->destroy();
    delete view;
}

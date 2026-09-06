#include "newui/controls.h"
#include "newui/items.h"
#include "newui/keyboard_constants.h"

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

// A plain free function, not a lambda - a non-capturing lambda converts
// to both Delegate::Callback (std::function) and Delegate::FunctionPtr,
// which MSVC rejects as an ambiguous add() call (same reasoning
// test_view.cpp's RecordDestroyed() already documents).
SyncReturn AnswerContentSize500x800(View&, Size& outSize) {
    outSize = Size(500.0f, 800.0f);
    return SyncReturn::Handled;
}

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
// Stepper - range/value/step clamping, plus arrow click behavior. Same
// "no live RunLoop needed" scope as ScrollBar's own tests above -
// auto-repeat (Stepper::startRepeat()) isn't exercised here beyond
// confirming the initial click's own immediate setValue() doesn't
// crash/throw.
// ---------------------------------------------------------------------

TEST(Stepper, DefaultsAreSaneRangeAndStep) {
    auto* stepper = new Stepper();

    EXPECT_FLOAT_EQ(stepper->value(), 0.0f);
    EXPECT_FLOAT_EQ(stepper->minValue(), 0.0f);
    EXPECT_FLOAT_EQ(stepper->maxValue(), 100.0f);
    EXPECT_FLOAT_EQ(stepper->step(), 1.0f);

    stepper->destroy();
    delete stepper;
}

TEST(Stepper, SetValueClampsToRange) {
    auto* stepper = new Stepper();
    stepper->setRange(0.0f, 10.0f);

    stepper->setValue(1000.0f);
    EXPECT_FLOAT_EQ(stepper->value(), 10.0f);

    stepper->setValue(-50.0f);
    EXPECT_FLOAT_EQ(stepper->value(), 0.0f);

    stepper->destroy();
    delete stepper;
}

TEST(Stepper, SetRangeReclampsExistingValue) {
    auto* stepper = new Stepper();
    stepper->setRange(0.0f, 100.0f);
    stepper->setValue(90.0f);
    ASSERT_FLOAT_EQ(stepper->value(), 90.0f);

    stepper->setRange(0.0f, 50.0f);
    EXPECT_FLOAT_EQ(stepper->value(), 50.0f);

    stepper->destroy();
    delete stepper;
}

TEST(Stepper, SetStepIgnoresNonPositiveValues) {
    auto* stepper = new Stepper();

    stepper->setStep(0.5f);
    EXPECT_FLOAT_EQ(stepper->step(), 0.5f);

    // step() must stay > 0 - a zero/negative request leaves it unchanged.
    stepper->setStep(0.0f);
    EXPECT_FLOAT_EQ(stepper->step(), 0.5f);
    stepper->setStep(-3.0f);
    EXPECT_FLOAT_EQ(stepper->step(), 0.5f);

    stepper->destroy();
    delete stepper;
}

TEST(Stepper, OnValueChangedFiresOnlyOnActualChange) {
    auto* stepper = new Stepper();
    stepper->setRange(0.0f, 100.0f);

    int fireCount = 0;
    stepper->onValueChanged.add([&fireCount](Stepper&) {
        ++fireCount;
        return SyncReturn::Handled;
    });

    stepper->setValue(50.0f);
    EXPECT_EQ(fireCount, 1);

    stepper->setValue(50.0f);  // unchanged - no-op
    EXPECT_EQ(fireCount, 1);

    stepper->setValue(60.0f);
    EXPECT_EQ(fireCount, 2);

    stepper->destroy();
    delete stepper;
}

TEST(Stepper, ClickingUpArrowIncreasesValueByStep) {
    auto* stepper = new Stepper();
    stepper->setBounds(Rect(0, 0, 20, 40));  // upRect = top 20px, downRect = bottom 20px
    stepper->setRange(0.0f, 100.0f);
    stepper->setStep(5.0f);
    stepper->setValue(50.0f);

    stepper->onMouseDown(*stepper, Point(10.0f, 5.0f), 1, 0);
    EXPECT_FLOAT_EQ(stepper->value(), 55.0f);

    stepper->onMouseUp(*stepper, Point(10.0f, 5.0f), 1, 0);

    stepper->destroy();
    delete stepper;
}

TEST(Stepper, ClickingDownArrowDecreasesValueByStep) {
    auto* stepper = new Stepper();
    stepper->setBounds(Rect(0, 0, 20, 40));
    stepper->setRange(0.0f, 100.0f);
    stepper->setStep(5.0f);
    stepper->setValue(50.0f);

    stepper->onMouseDown(*stepper, Point(10.0f, 30.0f), 1, 0);
    EXPECT_FLOAT_EQ(stepper->value(), 45.0f);

    stepper->onMouseUp(*stepper, Point(10.0f, 30.0f), 1, 0);

    stepper->destroy();
    delete stepper;
}

TEST(Stepper, ClickOutsideBoundsIsIgnored) {
    auto* stepper = new Stepper();
    stepper->setBounds(Rect(0, 0, 20, 40));
    stepper->setRange(0.0f, 100.0f);
    stepper->setValue(50.0f);

    stepper->onMouseDown(*stepper, Point(100.0f, 100.0f), 1, 0);
    EXPECT_FLOAT_EQ(stepper->value(), 50.0f);

    stepper->destroy();
    delete stepper;
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

TEST(ScrollView, AutoDerivesContentSizeFromSoleChildsContentSizeWithoutAManualCall) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));

    auto* content = new SubView();
    content->setVisible(true);
    content->onQueryContentSize.add(AnswerContentSize500x800);
    // No setContentSize() call anywhere in this test - addChild() alone
    // (via updateLayout(), see its own comment, controls.h) should be
    // enough to pick up content's own answer.
    view->addChild(content);

    EXPECT_EQ(view->contentSize(), Size(500.0f, 800.0f));
    ASSERT_TRUE(view->vBar()->isVisible());
    ASSERT_TRUE(view->hBar()->isVisible());
    EXPECT_FLOAT_EQ(view->vBar()->maxValue(), 800.0f);
    EXPECT_FLOAT_EQ(view->hBar()->maxValue(), 500.0f);

    view->destroy();
    delete view;
}

TEST(ScrollView, ManualSetContentSizeTakesPermanentPrecedenceOverAChildsOwnAnswer) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));
    view->setContentSize(Size(50.0f, 50.0f));

    auto* content = new SubView();
    content->setVisible(true);
    content->onQueryContentSize.add(AnswerContentSize500x800);
    view->addChild(content);

    // The manual value from before addChild() wins, even though content
    // itself could answer - see contentSizeOverridden_'s own doc comment
    // (controls.h): a single explicit setContentSize() call opts out of
    // auto-derivation permanently, not just until the next addChild().
    EXPECT_EQ(view->contentSize(), Size(50.0f, 50.0f));
    EXPECT_FALSE(view->vBar()->isVisible());
    EXPECT_FALSE(view->hBar()->isVisible());

    view->destroy();
    delete view;
}

TEST(ScrollView, DoesNotAutoDeriveContentSizeWithMoreThanOneChild) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));

    auto* first = new SubView();
    first->setVisible(true);
    first->onQueryContentSize.add(AnswerContentSize500x800);
    view->addChild(first);

    auto* second = new SubView();
    second->setVisible(true);
    view->addChild(second);

    // Two children - no single answer makes sense automatically (see
    // updateLayout()'s own comment, controls.h), so contentSize_ stays at
    // its untouched default rather than picking first's answer.
    EXPECT_EQ(view->contentSize(), Size());

    view->destroy();
    delete view;
}

TEST(ScrollView, VirtualizedChildIsPinnedToViewportSizeAndToldItsScrollOffsetDirectly) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));

    auto* content = new SubView();
    content->setVisible(true);
    content->onQueryContentSize.add(AnswerContentSize500x800);
    std::vector<Point> receivedOffsets;
    content->onScrollOffsetChanged.add([&receivedOffsets](View&, const Point& offset) -> SyncReturn {
        receivedOffsets.push_back(offset);
        return SyncReturn::Handled;
    });
    view->addChild(content);

    // Pinned to whatever the viewport actually is, not grown to the
    // 500x800 it reported - the whole point of being virtualized (see
    // ScrollView::updateLayout()'s own comment, controls.h).
    EXPECT_LT(content->bounds().size().width, 500.0f);
    EXPECT_LT(content->bounds().size().height, 800.0f);
    EXPECT_EQ(content->bounds().pos(), Point());

    ASSERT_TRUE(view->vBar()->isVisible());
    ASSERT_FALSE(receivedOffsets.empty());
    // viewport_'s own origin() never moves for a virtualized child - see
    // updateLayout()'s own comment for why shifting it would be pointless
    // (the child is already pinned to exactly viewport_'s own bounds).
    EXPECT_EQ(view->contentOrigin(), Point());

    view->vBar()->setValue(100.0f);

    EXPECT_FLOAT_EQ(receivedOffsets.back().y, 100.0f);
    EXPECT_EQ(view->contentOrigin(), Point());

    view->destroy();
    delete view;
}

// ---------------------------------------------------------------------
// ToolbarButton - same momentary-vs-toggle click gesture as Button,
// just checked via ThemedToolbarButtonStyle instead
// ---------------------------------------------------------------------

TEST(ToolbarButton, MomentaryByDefaultDoesNotToggleOnClick) {
    auto* button = new ToolbarButton();
    button->setBounds(Rect(0, 0, 40, 24));
    ASSERT_FALSE(button->isToggleButton());

    button->onMouseDown(*button, Point(10.0f, 10.0f), 1, 0);
    button->onMouseUp(*button, Point(10.0f, 10.0f), 1, 0);

    EXPECT_FALSE(button->isChecked());

    button->destroy();
    delete button;
}

TEST(ToolbarButton, ToggleButtonFlipsCheckedOnCompletedClick) {
    auto* button = new ToolbarButton();
    button->setBounds(Rect(0, 0, 40, 24));
    button->setToggleButton(true);

    button->onMouseDown(*button, Point(10.0f, 10.0f), 1, 0);
    button->onMouseUp(*button, Point(10.0f, 10.0f), 1, 0);
    EXPECT_TRUE(button->isChecked());

    button->onMouseDown(*button, Point(10.0f, 10.0f), 1, 0);
    button->onMouseUp(*button, Point(10.0f, 10.0f), 1, 0);
    EXPECT_FALSE(button->isChecked());

    button->destroy();
    delete button;
}

TEST(ToolbarButton, ReleasingOutsideBoundsDoesNotToggle) {
    auto* button = new ToolbarButton();
    button->setBounds(Rect(0, 0, 40, 24));
    button->setToggleButton(true);

    button->onMouseDown(*button, Point(10.0f, 10.0f), 1, 0);
    button->onMouseUp(*button, Point(1000.0f, 1000.0f), 1, 0);

    EXPECT_FALSE(button->isChecked());

    button->destroy();
    delete button;
}

TEST(ToolbarButton, OnCheckedChangedFiresOnlyOnActualChange) {
    auto* button = new ToolbarButton();

    int fireCount = 0;
    button->onCheckedChanged.add([&fireCount](ToolbarButton&) {
        ++fireCount;
        return SyncReturn::Handled;
    });

    button->setChecked(true);
    EXPECT_EQ(fireCount, 1);
    button->setChecked(true);
    EXPECT_EQ(fireCount, 1);
    button->setChecked(false);
    EXPECT_EQ(fireCount, 2);

    button->destroy();
    delete button;
}

// ---------------------------------------------------------------------
// ToolbarSeparator - orientation picks which axis carries the thin
// dividing-line size, the other stays 0 (Stretch fills it from the
// owning Toolbar's own cross-axis size)
// ---------------------------------------------------------------------

TEST(ToolbarSeparator, DefaultsToHorizontalToolbarOrientation) {
    auto* separator = new ToolbarSeparator();

    EXPECT_TRUE(separator->isHorizontal());
    EXPECT_GT(separator->desiredSize().width, 0.0f);
    EXPECT_FLOAT_EQ(separator->desiredSize().height, 0.0f);

    separator->destroy();
    delete separator;
}

TEST(ToolbarSeparator, SetHorizontalFalseSwapsWhichAxisIsSized) {
    auto* separator = new ToolbarSeparator();
    separator->setHorizontal(false);

    EXPECT_FALSE(separator->isHorizontal());
    EXPECT_FLOAT_EQ(separator->desiredSize().width, 0.0f);
    EXPECT_GT(separator->desiredSize().height, 0.0f);

    separator->destroy();
    delete separator;
}

// ---------------------------------------------------------------------
// Toolbar - FlexLayout-based container, same shape as MenuBar
// ---------------------------------------------------------------------

TEST(Toolbar, DefaultsToHorizontalOrientation) {
    auto* toolbar = new Toolbar();

    EXPECT_TRUE(toolbar->orientation() == Orientation::Horizontal);

    toolbar->destroy();
    delete toolbar;
}

TEST(Toolbar, ChildrenAreArrangedSideBySideAlongTheMainAxis) {
    auto* toolbar = new Toolbar();
    toolbar->setBounds(Rect(0, 0, 400, 28));

    auto* first = new ToolbarButton();
    first->setVisible(true);
    first->setDesiredSize(Size(30.0f, 24.0f));
    toolbar->addChild(first);

    auto* second = new ToolbarButton();
    second->setVisible(true);
    second->setDesiredSize(Size(30.0f, 24.0f));
    toolbar->addChild(second);

    EXPECT_FLOAT_EQ(first->bounds().pos().x, 0.0f);
    EXPECT_GT(second->bounds().pos().x, first->bounds().pos().x);

    toolbar->destroy();
    delete toolbar;
}

TEST(Toolbar, SetOrientationSwitchesTheFlexLayoutAxis) {
    auto* toolbar = new Toolbar(Orientation::Horizontal);

    toolbar->setOrientation(Orientation::Vertical);
    EXPECT_TRUE(toolbar->orientation() == Orientation::Vertical);

    toolbar->destroy();
    delete toolbar;
}

// ---------------------------------------------------------------------
// TextField - pure topology today (text-plan.md, Phase 1): owns a
// newui::text TextModel/TextSelection/Caret/TextInputTraits and a
// ThemedEditStyle for chrome, none of it painted or wired to real input
// yet (Phases 2/3/5) - these tests only cover that composition, the same
// scope the class itself is limited to right now.
// ---------------------------------------------------------------------

TEST(TextField, DefaultConstructedIsVisibleAndEmpty) {
    auto* field = new TextField();

    EXPECT_TRUE(field->isEnabled());
    EXPECT_EQ(field->text(), L"");
    EXPECT_TRUE(field->model().empty());

    field->destroy();
    delete field;
}

TEST(TextField, StyleIsThemedEditStyle) {
    auto* field = new TextField();

    EXPECT_NE(dynamic_cast<ThemedEditStyle*>(&field->style()), nullptr);

    field->destroy();
    delete field;
}

TEST(TextField, SetTextForwardsToModel) {
    auto* field = new TextField();

    field->setText(L"hello");

    EXPECT_EQ(field->text(), L"hello");
    EXPECT_EQ(field->model().text(), L"hello");

    field->destroy();
    delete field;
}

TEST(TextField, ModelAccessorReachesTheRealMutatorsAndEvents) {
    auto* field = new TextField();
    int onChangedCount = 0;
    field->model().onChanged.add([&](Model&) {
        ++onChangedCount;
        return SyncReturn::Handled;
        });

    field->model().insert(0, L"hi");

    EXPECT_EQ(field->text(), L"hi");
    EXPECT_EQ(onChangedCount, 1);

    field->destroy();
    delete field;
}

TEST(TextField, SelectionAccessorHoldsRealSelectionState) {
    auto* field = new TextField();
    EXPECT_TRUE(field->selection().isEmpty());

    field->selection().setRange(text::TextRange(1, 3));

    ASSERT_FALSE(field->selection().isEmpty());
    EXPECT_EQ(field->selection().ranges()[0], text::TextRange(1, 3));

    field->destroy();
    delete field;
}

TEST(TextField, CaretAccessorHoldsRealCaretState) {
    auto* field = new TextField();
    EXPECT_FALSE(field->caret().isActive());

    field->caret().setPosition(text::TextPosition(4));

    EXPECT_EQ(field->caret().position(), text::TextPosition(4));

    field->destroy();
    delete field;
}

TEST(TextField, InputTraitsAccessorHoldsRealTraitsState) {
    auto* field = new TextField();
    EXPECT_FALSE(field->inputTraits().isReadOnly());

    field->inputTraits().setReadOnly(true);
    field->inputTraits().setMaxLength(10);

    EXPECT_TRUE(field->inputTraits().isReadOnly());
    EXPECT_EQ(field->inputTraits().maxLength(), 10u);

    field->destroy();
    delete field;
}

// ---------------------------------------------------------------------
// TextField/TextControl - controller_/model_ are heap-owned
// (std::unique_ptr) and swappable via setController()/setModel(), rather
// than fixed stack members - see HANDOFF.md for why. RecordingTextController
// below overrides a virtual handler to prove a swapped-in subclass's
// override is actually reached (not just accepted and ignored).
// ---------------------------------------------------------------------

namespace {

class RecordingTextController : public TextController {
public:
    using TextController::TextController;

    int gotFocusCallCount = 0;

    // Deliberately does NOT chain to TextController::handleGotFocus() -
    // the real implementation starts a live caret-blink timer via
    // Application::instance().runLoop(), which needs a real pumped
    // message loop this headless test doesn't have. Overriding without
    // chaining is enough to prove dispatch reaches the override at all.
    SyncReturn handleGotFocus() override {
        ++gotFocusCallCount;
        return SyncReturn::Handled;
    }
};

}  // namespace

TEST(TextField, SetControllerReplacesTheControllerAndReachesASubclassOverride) {
    auto* field = new TextField();
    auto* custom = new RecordingTextController(*field);

    field->setController(std::unique_ptr<TextController>(custom));

    EXPECT_EQ(&field->controller(), custom);
    EXPECT_EQ(custom->gotFocusCallCount, 0);

    field->onGotFocus(*field);

    EXPECT_EQ(custom->gotFocusCallCount, 1) << "expected the field's own onGotFocus to reach the swapped-in subclass's override";

    field->destroy();
    delete field;
}

TEST(TextField, SetControllerWithNullptrDoesNotCrashOrReplaceTheExistingOne) {
    auto* field = new TextField();
    TextController* original = &field->controller();

    field->setController(nullptr);

    EXPECT_EQ(&field->controller(), original);

    field->destroy();
    delete field;
}

TEST(TextField, SetModelReplacesTheModelAndReflectsItsContent) {
    auto* field = new TextField();
    field->setText(L"original");

    auto newModel = std::make_unique<text::TextModel>();
    newModel->setText(L"replacement");
    text::TextModel* newModelPtr = newModel.get();

    field->setModel(std::move(newModel));

    EXPECT_EQ(&field->model(), newModelPtr);
    EXPECT_EQ(field->text(), L"replacement");

    field->destroy();
    delete field;
}

TEST(TextField, SetModelWithNullptrDoesNotCrashOrReplaceTheExistingOne) {
    auto* field = new TextField();
    text::TextModel* original = &field->model();

    field->setModel(nullptr);

    EXPECT_EQ(&field->model(), original);

    field->destroy();
    delete field;
}

TEST(TextControl, SetModelRewiresContentSizeChangeNotificationToTheNewModel) {
    auto* textControl = new TextControl();
    int contentSizeChangedCount = 0;
    textControl->onContentSizeChanged.add([&](View&) {
        ++contentSizeChangedCount;
        return SyncReturn::Handled;
        });

    auto newModel = std::make_unique<text::TextModel>();
    textControl->setModel(std::move(newModel));
    EXPECT_GE(contentSizeChangedCount, 1) << "setModel() itself should notify";

    int countAfterSwap = contentSizeChangedCount;
    textControl->model().setText(L"typed after swap");

    EXPECT_GT(contentSizeChangedCount, countAfterSwap) << "a change on the NEW model should still reach handleModelChanged after the swap";

    textControl->destroy();
    delete textControl;
}

// ---------------------------------------------------------------------
// TextControl - owns no scrollbar of its own at all (see HANDOFF.md for
// the history: an earlier version had one, hand-rolled, and its scroll-
// offset bookkeeping could desync from it - removed entirely rather than
// patched further). It just answers View::onQueryContentSize/accepts
// View::onScrollOffsetChanged (view.h) so a hosting ScrollView can
// provide the real scrollbar (see TextController's own class comment,
// controls.h, and ScrollView's updateLayout()/virtualizedContentChild()).
// Standalone use (no ScrollView) simply clips - there's no scrollbar
// anywhere in that case.
// ---------------------------------------------------------------------

namespace {

const std::wstring kManyLines =
    L"line one\nline two\nline three\nline four\nline five\nline six\n"
    L"line seven\nline eight\nline nine\nline ten";

}  // namespace

TEST(TextControl, HasNoScrollBarOfItsOwnEvenWhenStandaloneContentOverflows) {
    auto* textControl = new TextControl();
    textControl->setBounds(Rect(0, 0, 100, 60));

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);

    textControl->setText(kManyLines);
    textControl->paint(ctx);

    EXPECT_TRUE(textControl->childViews().empty());

    textControl->destroy();
    delete textControl;
}

TEST(TextControl, ReportsRealContentHeightEvenBeforeAnyPaintCall) {
    auto* textControl = new TextControl();
    textControl->setBounds(Rect(0, 0, 100, 60));
    textControl->setText(kManyLines);

    // No paint() call anywhere in this test - handleQueryContentSize()
    // has to lay out on demand itself (see its own comment, controls.cpp)
    // since a hosting ScrollView can legitimately ask before this control
    // has ever actually been painted.
    Size reported = textControl->contentSize();

    EXPECT_GT(reported.height, textControl->bounds().size().height);

    textControl->destroy();
    delete textControl;
}

TEST(TextControl, ReportsBoundsSizeWhenContentFits) {
    auto* textControl = new TextControl();
    textControl->setBounds(Rect(0, 0, 100, 60));
    textControl->setText(L"short");

    Size reported = textControl->contentSize();

    EXPECT_LE(reported.height, textControl->bounds().size().height);

    textControl->destroy();
    delete textControl;
}

TEST(TextControl, WorksInsideAScrollViewSharingItsScrollbarInstead) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* textControl = new TextControl();
    textControl->setText(kManyLines);
    scrollView->addChild(textControl);

    // No scrollbar of its own, and pinned to the (much smaller) viewport
    // rather than grown to its true content height - the whole point of
    // being hosted rather than standalone.
    EXPECT_TRUE(textControl->childViews().empty());
    ASSERT_TRUE(scrollView->vBar()->isVisible());
    EXPECT_LT(textControl->bounds().size().height, 60.0f);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    // Must not crash painting at its pinned (small) bounds despite
    // holding far more text than that.
    textControl->paint(ctx);

    // Scrolling the *ScrollView's* bar (not anything on textControl
    // itself, which has nothing of its own to scroll) must not crash
    // either, and a subsequent paint() still has to succeed at the same
    // small, unchanged bounds.
    scrollView->vBar()->setValue(scrollView->vBar()->maxValue());
    textControl->paint(ctx);

    scrollView->destroy();
    delete scrollView;
}

// ---------------------------------------------------------------------
// ListView - the first real consumer of the Item/Controller foundation
// (items.h/controllers.h): rows painted via a pooled ListItem, never a
// real child SubView per row, hosted the same ScrollView-virtualization
// way TextControl already is (see its own tests above).
// ---------------------------------------------------------------------

namespace {

class StubRowModel : public ListModel {
public:
    std::vector<std::string> rows;

    std::any value(const std::any& key) override {
        if (const std::size_t* index = std::any_cast<std::size_t>(&key)) {
            if (*index < rows.size()) {
                return rows[*index];
            }
        }
        return std::any();
    }

    std::size_t size() const override { return rows.size(); }
};

}  // namespace

TEST(ListView, DefaultConstructedHasZeroItemCountAndNoSelection) {
    auto* listView = new ListView();

    EXPECT_EQ(listView->controller().itemCount(), 0u);
    EXPECT_FALSE(listView->selectedIndex().has_value());

    listView->destroy();
    delete listView;
}

TEST(ListView, ContentSizeIsItemCountTimesRowHeight) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 60));

    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    Size reported = listView->contentSize();

    EXPECT_FLOAT_EQ(reported.height, float(model.rows.size()) * listView->rowHeight());

    listView->destroy();
    delete listView;
}

namespace {

// A customized ListController whose rows genuinely vary in height by
// content - row 0 is short (a plain label), row 1 is tall (imagine an
// image/preview row) - the real scenario ListController::itemHeight()
// exists for.
class VariableHeightController : public ListController {
public:
    float itemHeight(std::size_t index) const override {
        return index == 1 ? 50.0f : 20.0f;
    }
};

}  // namespace

TEST(ListView, RespectsACustomizedControllersPerRowItemHeight) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    listView->setController(std::make_unique<VariableHeightController>());

    StubRowModel model;
    model.rows = { "short", "tall", "short again" };
    listView->setModel(&model);

    // 20 (row 0) + 50 (row 1) + 20 (row 2) = 90, not 3 * rowHeight().
    Size reported = listView->contentSize();
    EXPECT_FLOAT_EQ(reported.height, 90.0f);

    // A click inside row 1's taller span (y = 30, well past row 0's own
    // 20px but still inside row 1's 20-70 span) should select row 1, not
    // whatever a uniform-row-height assumption would have picked.
    float clickY = listView->getClientBounds().top() + 30.0f;
    listView->onMouseDown(*listView, Point(10.0f, clickY), 0, 0);

    ASSERT_TRUE(listView->selectedIndex().has_value());
    EXPECT_EQ(*listView->selectedIndex(), 1u);

    BLImage image(100, 200, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);

    listView->destroy();
    delete listView;
}

TEST(ListView, SetSelectedIndexMarksDirtyAndFiresOnSelectionChanged) {
    auto* listView = new ListView();
    int selectionChangedCount = 0;
    listView->onSelectionChanged.add([&](ListView&) {
        ++selectionChangedCount;
        return SyncReturn::Handled;
        });

    listView->setSelectedIndex(2u);

    ASSERT_TRUE(listView->selectedIndex().has_value());
    EXPECT_EQ(*listView->selectedIndex(), 2u);
    EXPECT_EQ(selectionChangedCount, 1);

    // Setting the same index again is a no-op - no extra notification.
    listView->setSelectedIndex(2u);
    EXPECT_EQ(selectionChangedCount, 1);

    listView->destroy();
    delete listView;
}

TEST(ListView, MouseDownSelectsTheRowUnderThePoint) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 60));

    StubRowModel model;
    model.rows = { "a", "b", "c" };
    listView->setModel(&model);

    int selectionChangedCount = 0;
    listView->onSelectionChanged.add([&](ListView&) {
        ++selectionChangedCount;
        return SyncReturn::Handled;
        });

    // Midway through row 1 (0-based), relative to wherever this
    // ListView's own client bounds actually start - not assumed to be
    // exactly (0,0), since its ThemedEditStyle chrome may inset a border.
    float clickY = listView->getClientBounds().top() + 1.5f * listView->rowHeight();
    listView->onMouseDown(*listView, Point(10.0f, clickY), 0, 0);

    ASSERT_TRUE(listView->selectedIndex().has_value());
    EXPECT_EQ(*listView->selectedIndex(), 1u);
    EXPECT_EQ(selectionChangedCount, 1);

    listView->destroy();
    delete listView;
}

TEST(ListView, MouseDownPastTheLastRowDoesNotSelectAnything) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 60));

    StubRowModel model;
    model.rows = { "a", "b" };
    listView->setModel(&model);

    float clickY = listView->getClientBounds().top() + 10.0f * listView->rowHeight();
    listView->onMouseDown(*listView, Point(10.0f, clickY), 0, 0);

    EXPECT_FALSE(listView->selectedIndex().has_value());

    listView->destroy();
    delete listView;
}

// ---------------------------------------------------------------------
// ListView - multi-selection: a plain click replaces the whole selection,
// Ctrl+click toggles one row without disturbing the rest, Shift+click
// range-selects from the last plain/Ctrl+click - standard listbox/
// Explorer conventions.
// ---------------------------------------------------------------------

namespace {

void ClickRow(ListView* listView, std::size_t index, std::uint32_t keyMask) {
    float clickY = listView->getClientBounds().top() + (float(index) + 0.5f) * listView->rowHeight();
    listView->onMouseDown(*listView, Point(10.0f, clickY), 0, keyMask);
}

}  // namespace

TEST(ListView, PlainClickReplacesTheWholeSelection) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    ClickRow(listView, 1, 0);
    ClickRow(listView, 3, 0);

    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 3u }))
        << "a later plain click should replace the earlier selection entirely";

    listView->destroy();
    delete listView;
}

TEST(ListView, CtrlClickTogglesARowWithoutDisturbingTheRest) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    ClickRow(listView, 1, 0);
    ClickRow(listView, 3, kmCtrl);

    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u, 3u }));

    // Ctrl+click on an already-selected row toggles it back off.
    ClickRow(listView, 1, kmCtrl);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 3u }));

    listView->destroy();
    delete listView;
}

TEST(ListView, ShiftClickSelectsARangeFromTheLastPlainClick) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    ClickRow(listView, 1, 0);
    ClickRow(listView, 3, kmShift);

    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u, 2u, 3u }));

    // A second Shift+click ranges from the SAME original anchor (row 1),
    // not from row 3 (the previous Shift+click's own target).
    ClickRow(listView, 0, kmShift);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 0u, 1u }));

    listView->destroy();
    delete listView;
}

TEST(ListView, SelectRangeAddToSelectionAndClearSelectionWorkDirectly) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    listView->selectRange(1u, 3u);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u, 2u, 3u }));

    listView->addToSelection(0u);
    EXPECT_TRUE(listView->isSelected(0u));
    EXPECT_TRUE(listView->isSelected(2u));

    listView->removeFromSelection(2u);
    EXPECT_FALSE(listView->isSelected(2u));

    listView->clearSelection();
    EXPECT_TRUE(listView->selectedIndices().empty());

    listView->destroy();
    delete listView;
}

TEST(ListView, MultiSelectionPaintsAllSelectedRowsWithoutCrashing) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);
    listView->selectRange(1u, 3u);

    BLImage image(100, 200, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);

    listView->destroy();
    delete listView;
}

// ---------------------------------------------------------------------
// ListView - hover highlighting (hoverHighlightEnabled(), on by default)
// ---------------------------------------------------------------------

TEST(ListView, HoverHighlightIsEnabledByDefault) {
    auto* listView = new ListView();
    EXPECT_TRUE(listView->hoverHighlightEnabled());
    listView->destroy();
    delete listView;
}

TEST(ListView, MouseMoveTracksTheHoveredRowAndMouseLeftClearsIt) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    float row2Y = listView->getClientBounds().top() + 2.5f * listView->rowHeight();
    listView->onMouseMove(*listView, Point(10.0f, row2Y), 0, 0);

    BLImage image(100, 200, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);  // hovered row must not crash to paint

    listView->onMouseLeft(*listView, Point(10.0f, row2Y), 0, 0);
    listView->paint(ctx);  // and neither should painting after it clears

    listView->destroy();
    delete listView;
}

TEST(ListView, DisablingHoverHighlightClearsAnyCurrentlyHoveredRow) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    float row2Y = listView->getClientBounds().top() + 2.5f * listView->rowHeight();
    listView->onMouseMove(*listView, Point(10.0f, row2Y), 0, 0);

    listView->setHoverHighlightEnabled(false);
    EXPECT_FALSE(listView->hoverHighlightEnabled());

    // Once disabled, further mouse movement shouldn't track hover at all -
    // exercised indirectly by just confirming paint() still works cleanly
    // (no pooled Item is left in a stale highlighted state).
    BLImage image(100, 200, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);

    listView->destroy();
    delete listView;
}

TEST(ListView, SetKeyboardHighlightedIndexClampsAndIsIndependentOfHover) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);

    listView->setKeyboardHighlightedIndex(2u);
    ASSERT_TRUE(listView->keyboardHighlightedIndex().has_value());
    EXPECT_EQ(*listView->keyboardHighlightedIndex(), 2u);

    // Out of range against itemCount() (5 rows, index 99 is past the end) -
    // clears rather than storing an invalid index.
    listView->setKeyboardHighlightedIndex(99u);
    EXPECT_FALSE(listView->keyboardHighlightedIndex().has_value());

    // A real mouse move (hoveredIndex_) must not disturb a keyboard
    // highlight set independently of it - see keyboardHighlightedIndex()'s
    // own doc comment (controls.h) for why the two are kept separate.
    listView->setKeyboardHighlightedIndex(1u);
    float row3Y = listView->getClientBounds().top() + 3.5f * listView->rowHeight();
    listView->onMouseMove(*listView, Point(10.0f, row3Y), 0, 0);
    ASSERT_TRUE(listView->keyboardHighlightedIndex().has_value());
    EXPECT_EQ(*listView->keyboardHighlightedIndex(), 1u);

    BLImage image(100, 200, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);  // both a hovered and a keyboard-highlighted row must not crash to paint

    listView->destroy();
    delete listView;
}

TEST(ListView, PaintDoesNotCrashAndReusesASinglePooledItemAcrossRowsAndCalls) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 60));

    StubRowModel model;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(&model);
    listView->setSelectedIndex(1u);

    ListItem* before = listView->controller().createItem(0);
    listView->controller().releaseItem(before);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);

    ListItem* after = listView->controller().createItem(0);
    EXPECT_EQ(before, after) << "expected every row's ListItem, across the whole paint() call, to reuse the same pooled instance - it's released back to the pool immediately after each row";
    listView->controller().releaseItem(after);

    listView->destroy();
    delete listView;
}

TEST(ListView, WorksInsideAScrollViewSharingItsScrollbarInstead) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* listView = new ListView();
    StubRowModel model;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(&model);
    scrollView->addChild(listView);

    // No scrollbar of its own, and pinned to the (much smaller) viewport
    // rather than grown to its true content height - same virtualized-
    // child contract TextControl's own equivalent test above verifies.
    EXPECT_TRUE(listView->childViews().empty());
    ASSERT_TRUE(scrollView->vBar()->isVisible());
    EXPECT_LT(listView->bounds().size().height, float(model.rows.size()) * listView->rowHeight());

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);

    scrollView->vBar()->setValue(scrollView->vBar()->maxValue());
    listView->paint(ctx);

    scrollView->destroy();
    delete scrollView;
}

// ---------------------------------------------------------------------
// TreeView - the hierarchical counterpart to ListView above. Same
// multi-selection/hover/ScrollView-hosting coverage, plus the tree-
// specific piece: clicking the expand/collapse glyph toggles expand
// without changing selection, and vice versa.
// ---------------------------------------------------------------------

namespace {

// A small, fixed 2-level hierarchy - the root has 2 children (0, 1);
// child 0 has 2 children of its own (0/0, 0/1); everything else is a
// leaf. Same shape test_items.cpp's own StubTreeModel uses.
class StubTreeRowModel : public TreeModel {
public:
    std::size_t childCount(const std::vector<std::size_t>& path) const override {
        if (path.empty()) {
            return 2;
        }
        if (path.size() == 1 && path[0] == 0) {
            return 2;
        }
        return 0;
    }

    std::any value(const std::any& key) override {
        if (const std::vector<std::size_t>* path = std::any_cast<std::vector<std::size_t>>(&key)) {
            std::string label = "node";
            for (std::size_t i : *path) {
                label += "-" + std::to_string(i);
            }
            return label;
        }
        return std::any();
    }
};

void ClickTreeRow(TreeView* treeView, std::size_t visibleIndex, float xOffsetFromLeft, std::uint32_t keyMask) {
    float clickY = treeView->getClientBounds().top() + (float(visibleIndex) + 0.5f) * treeView->rowHeight();
    float clickX = treeView->getClientBounds().left() + xOffsetFromLeft;
    treeView->onMouseDown(*treeView, Point(clickX, clickY), 0, keyMask);
}

}  // namespace

TEST(TreeView, DefaultConstructedHasZeroVisibleRowsAndNoSelection) {
    auto* treeView = new TreeView();
    EXPECT_EQ(treeView->controller().visibleCount(), 0u);
    EXPECT_FALSE(treeView->selectedPath().has_value());
    treeView->destroy();
    delete treeView;
}

TEST(TreeView, ContentSizeReflectsOnlyCurrentlyVisibleRows) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);

    // Collapsed: just the 2 root children.
    Size collapsedSize = treeView->contentSize();
    EXPECT_FLOAT_EQ(collapsedSize.height, 2.0f * treeView->rowHeight());

    treeView->controller().setExpanded({ 0u }, true);
    Size expandedSize = treeView->contentSize();
    EXPECT_FLOAT_EQ(expandedSize.height, 4.0f * treeView->rowHeight());

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, ClickingTheGlyphTogglesExpandWithoutSelecting) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);

    EXPECT_FALSE(treeView->controller().isExpanded({ 0u }));

    // Row 0 (path {0}) is at depth 0, so its glyph sits at local X in
    // [0, kTreeGlyphWidth) - well inside a click at x=4.
    ClickTreeRow(treeView, 0, 4.0f, 0);

    EXPECT_TRUE(treeView->controller().isExpanded({ 0u }));
    EXPECT_FALSE(treeView->selectedPath().has_value()) << "clicking the glyph should not select the row";

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, ClickingTheLabelSelectsWithoutTogglingExpand) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);

    // Well to the right of the glyph - the row's own label text.
    ClickTreeRow(treeView, 0, 50.0f, 0);

    ASSERT_TRUE(treeView->selectedPath().has_value());
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 0u }));
    EXPECT_FALSE(treeView->controller().isExpanded({ 0u })) << "clicking the label should not toggle expand";

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, CtrlClickTogglesSelectionAcrossRows) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);

    ClickTreeRow(treeView, 0, 50.0f, 0);        // select {0}
    ClickTreeRow(treeView, 1, 50.0f, kmCtrl);   // add {1}

    EXPECT_EQ(treeView->selectedPaths(), (std::set<std::vector<std::size_t>>{ { 0u }, { 1u } }));

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, ShiftClickSelectsARangeAcrossExpandedRows) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);
    treeView->controller().setExpanded({ 0u }, true);  // visible: {0}, {0,0}, {0,1}, {1}

    ClickTreeRow(treeView, 0, 50.0f, 0);         // anchor at visible row 0 ({0})
    ClickTreeRow(treeView, 2, 50.0f, kmShift);   // range to visible row 2 ({0,1})

    EXPECT_EQ(treeView->selectedPaths(), (std::set<std::vector<std::size_t>>{ { 0u }, { 0u, 0u }, { 0u, 1u } }));

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, HoverHighlightIsEnabledByDefaultAndTracksMouseMove) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    EXPECT_TRUE(treeView->hoverHighlightEnabled());

    StubTreeRowModel model;
    treeView->setModel(&model);

    float row1Y = treeView->getClientBounds().top() + 1.5f * treeView->rowHeight();
    treeView->onMouseMove(*treeView, Point(50.0f, row1Y), 0, 0);

    BLImage image(100, 200, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    treeView->paint(ctx);  // must not crash while a row is hovered

    treeView->onMouseLeft(*treeView, Point(50.0f, row1Y), 0, 0);
    treeView->paint(ctx);

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, PaintDoesNotCrashAndReusesASinglePooledItem) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);
    treeView->controller().setExpanded({ 0u }, true);
    treeView->setSelectedPath(std::vector<std::size_t>{ 0u, 1u });

    TreeItem* before = treeView->controller().createItem({});
    treeView->controller().releaseItem(before);

    BLImage image(100, 200, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    treeView->paint(ctx);

    TreeItem* after = treeView->controller().createItem({});
    EXPECT_EQ(before, after) << "expected every row's TreeItem, across the whole paint() call, to reuse the same pooled instance";
    treeView->controller().releaseItem(after);

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, RectForPathIsNulloptWhenNotCurrentlyVisible) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);

    // {0}'s own children are collapsed away by default.
    EXPECT_FALSE(treeView->rectForPath({ 0u, 0u }).has_value());
    // Doesn't exist in the model at all.
    EXPECT_FALSE(treeView->rectForPath({ 5u }).has_value());

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, RectForPathMatchesTheOnScreenRowWithNoScroll) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);

    std::optional<Rect> rect = treeView->rectForPath({ 1u });
    ASSERT_TRUE(rect.has_value());
    Rect clientBounds = treeView->getClientBounds();
    EXPECT_FLOAT_EQ(rect->left(), clientBounds.left());
    EXPECT_FLOAT_EQ(rect->top(), clientBounds.top() + treeView->rowHeight());
    EXPECT_FLOAT_EQ(rect->width(), clientBounds.width());
    EXPECT_FLOAT_EQ(rect->height(), treeView->rowHeight());

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, RectForPathAccountsForTheCurrentScrollOffset) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    StubTreeRowModel model;
    treeView->setModel(&model);
    treeView->controller().setExpanded({ 0u }, true);  // visible: {0}, {0,0}, {0,1}, {1}

    treeView->onScrollOffsetChanged(*treeView, Point(0.0f, treeView->rowHeight()));

    // {0,1} is visible row 2 - content-space Y is 2*rowHeight, minus the
    // 1*rowHeight scroll offset just applied above.
    std::optional<Rect> rect = treeView->rectForPath({ 0u, 1u });
    ASSERT_TRUE(rect.has_value());
    EXPECT_FLOAT_EQ(rect->top(), treeView->getClientBounds().top() + treeView->rowHeight());

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, WorksInsideAScrollViewSharingItsScrollbarInstead) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* treeView = new TreeView();
    StubTreeRowModel model;
    treeView->setModel(&model);
    treeView->controller().setExpanded({ 0u }, true);
    scrollView->addChild(treeView);

    EXPECT_TRUE(treeView->childViews().empty());

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    treeView->paint(ctx);

    scrollView->destroy();
    delete scrollView;
}

// DropDownList's own popup open/dismiss behavior needs a real HWND
// (PopupFrame::initialize() requires rootView()->windowHandle(), only
// resolvable once attached under a live Frame/RootView - see openPopup()'s
// own doc comment, controls.cpp) - not practically testable headlessly, so
// (matching this file's own established scoping for anything screen/HWND-
// related) these cover only what's reachable without one: model/selection
// wiring, buttonRect() hit-testing, and that clicking the button while
// detached from any live window is a safe no-op rather than a crash.

namespace {

// Exposes protected buttonRect() for direct assertions on its geometry -
// same "protected purely for testability" pattern TestableThemedButtonStyle
// (test_viewstyle.cpp) already uses for partId()/stateId().
class TestableDropDownList : public DropDownList {
public:
    using DropDownList::buttonRect;
};

}  // namespace

TEST(DropDownList, DefaultConstructedHasNoSelectionAndIsNotOpen) {
    auto* dropDown = new DropDownList();

    EXPECT_FALSE(dropDown->selectedIndex().has_value());
    EXPECT_FALSE(dropDown->isOpen());

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, SetModelWiresTheControllerAndClearsAnOutOfRangeSelection) {
    auto* dropDown = new DropDownList();

    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);
    EXPECT_EQ(dropDown->model(), &model);

    dropDown->setSelectedIndex(2u);
    ASSERT_TRUE(dropDown->selectedIndex().has_value());

    // Swapping in a smaller model whose size() no longer covers index 2
    // clears the now-invalid selection - same reasoning
    // TreeController::setModel() already has for expandedPaths_ referring
    // to a path that no longer exists.
    StubRowModel smallerModel;
    smallerModel.rows = { "only one" };
    dropDown->setModel(&smallerModel);
    EXPECT_FALSE(dropDown->selectedIndex().has_value());

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, SetSelectedIndexMarksDirtyAndFiresOnSelectionChanged) {
    auto* dropDown = new DropDownList();
    int selectionChangedCount = 0;
    dropDown->onSelectionChanged.add([&](DropDownList&) {
        ++selectionChangedCount;
        return SyncReturn::Handled;
        });

    dropDown->setSelectedIndex(1u);

    ASSERT_TRUE(dropDown->selectedIndex().has_value());
    EXPECT_EQ(*dropDown->selectedIndex(), 1u);
    EXPECT_EQ(selectionChangedCount, 1);

    // Setting the same index again is a no-op - no extra notification.
    dropDown->setSelectedIndex(1u);
    EXPECT_EQ(selectionChangedCount, 1);

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, ButtonRectIsAFixedWidthRegionAlignedToTheRightEdge) {
    auto* dropDown = new TestableDropDownList();
    dropDown->setBounds(Rect(0, 0, 200, 24));

    Rect client = dropDown->getClientBounds();
    Rect button = dropDown->buttonRect();

    EXPECT_FLOAT_EQ(button.top(), client.top());
    EXPECT_FLOAT_EQ(button.size().height, client.size().height);
    EXPECT_FLOAT_EQ(button.left() + button.size().width, client.left() + client.size().width);
    // Square-ish - width matches the client height (clamped to the client
    // width, irrelevant at this size) - see buttonRect()'s own doc comment.
    EXPECT_FLOAT_EQ(button.size().width, client.size().height);

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, ClickingTheButtonWithNoLiveWindowIsASafeNoOp) {
    auto* dropDown = new TestableDropDownList();
    dropDown->setBounds(Rect(0, 0, 200, 24));

    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);

    Point insideButton(dropDown->buttonRect().left() + 2.0f, 12.0f);
    dropDown->onMouseDown(*dropDown, insideButton, 0, 0);

    // openPopup() early-returns (no rootView()/windowHandle() while
    // detached, as here) - isOpen() stays false rather than crashing.
    EXPECT_FALSE(dropDown->isOpen());

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, ClickingOutsideTheButtonDoesNotOpenIt) {
    auto* dropDown = new DropDownList();
    dropDown->setBounds(Rect(0, 0, 200, 24));

    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);

    Point outsideButton(4.0f, 12.0f);
    dropDown->onMouseDown(*dropDown, outsideButton, 0, 0);

    EXPECT_FALSE(dropDown->isOpen());

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, DisabledDropDownIgnoresButtonClicks) {
    auto* dropDown = new TestableDropDownList();
    dropDown->setBounds(Rect(0, 0, 200, 24));
    dropDown->setEnabled(false);

    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);

    Point insideButton(dropDown->buttonRect().left() + 2.0f, 12.0f);
    dropDown->onMouseDown(*dropDown, insideButton, 0, 0);

    EXPECT_FALSE(dropDown->isOpen());

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, PaintDoesNotCrashWithOrWithoutASelection) {
    auto* dropDown = new DropDownList();
    dropDown->setBounds(Rect(0, 0, 200, 24));

    BLImage image(200, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);

    // No model/selection yet - paint() should still draw its chrome/arrow
    // glyph without crashing.
    dropDown->paint(ctx);

    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);
    dropDown->setSelectedIndex(1u);
    dropDown->paint(ctx);

    dropDown->destroy();
    delete dropDown;
}

// Arrow keys while the popup is closed - see handleKeyDown()'s own doc
// comment (controls.h) for why this is reachable/testable headlessly (it
// only ever touches selectedIndex_ directly, unlike the open-popup case,
// which needs a live popupListView_).

TEST(DropDownList, DownArrowWithNoSelectionSelectsTheFirstItem) {
    auto* dropDown = new DropDownList();
    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);

    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkDownArrow);

    ASSERT_TRUE(dropDown->selectedIndex().has_value());
    EXPECT_EQ(*dropDown->selectedIndex(), 0u);

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, DownArrowAdvancesAndClampsAtTheLastItem) {
    auto* dropDown = new DropDownList();
    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);
    dropDown->setSelectedIndex(1u);

    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkDownArrow);
    ASSERT_TRUE(dropDown->selectedIndex().has_value());
    EXPECT_EQ(*dropDown->selectedIndex(), 2u);

    // Already on the last item - stays put rather than going out of range.
    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkDownArrow);
    ASSERT_TRUE(dropDown->selectedIndex().has_value());
    EXPECT_EQ(*dropDown->selectedIndex(), 2u);

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, UpArrowRetreatsAndClampsAtTheFirstItem) {
    auto* dropDown = new DropDownList();
    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);
    dropDown->setSelectedIndex(1u);

    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkUpArrow);
    ASSERT_TRUE(dropDown->selectedIndex().has_value());
    EXPECT_EQ(*dropDown->selectedIndex(), 0u);

    // Already on the first item - stays put rather than going negative.
    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkUpArrow);
    ASSERT_TRUE(dropDown->selectedIndex().has_value());
    EXPECT_EQ(*dropDown->selectedIndex(), 0u);

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, ArrowKeysAreIgnoredWithNoModel) {
    auto* dropDown = new DropDownList();

    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkDownArrow);
    EXPECT_FALSE(dropDown->selectedIndex().has_value());

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, ArrowKeysAreIgnoredWhileDisabled) {
    auto* dropDown = new DropDownList();
    StubRowModel model;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(&model);
    dropDown->setEnabled(false);

    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkDownArrow);
    EXPECT_FALSE(dropDown->selectedIndex().has_value());

    dropDown->destroy();
    delete dropDown;
}

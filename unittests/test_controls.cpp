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

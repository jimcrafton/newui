#include "newui/controls.h"
#include "newui/items.h"
#include "newui/keyboard_constants.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <typeinfo>
#include <vector>

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
// GroupBox - a bordered frame with a caption straddling the top border,
// drawn by paint() itself on top of style()'s own chrome (ThemedGroupBoxStyle
// by default) - same "chrome vs. content" split as Button's own text.
// ---------------------------------------------------------------------

TEST(GroupBox, DefaultConstructedIsVisibleWithThemedGroupBoxStyle) {
    auto* box = new GroupBox();

    EXPECT_TRUE(box->isVisible());
    EXPECT_TRUE(box->text().empty());
    EXPECT_NE(dynamic_cast<ThemedGroupBoxStyle*>(&box->style()), nullptr);

    box->destroy();
    delete box;
}

TEST(GroupBox, DefaultConstructedHasAResolvableFontForItsOwnCaption) {
    // A real, live-caught bug this guards against: GroupBox::Groupbox()
    // originally never called setFont() on its own style, the same gap
    // Button::Button()'s own comment documents - paint() (controls.cpp)
    // silently skips drawing the caption with an unresolved font, so the
    // caption never appeared at all despite text()/textColor_ both being
    // set correctly. Caught live via a temporary debug print, not by any
    // existing test - this one exists so it can't regress silently again.
    auto* box = new GroupBox();

    BLFont* blFont = box->style().font().blFont();
    ASSERT_NE(blFont, nullptr);
    EXPECT_TRUE(blFont->is_valid());

    box->destroy();
    delete box;
}

TEST(GroupBox, SetTextChangesTheStoredValue) {
    auto* box = new GroupBox();

    box->setText("Options");
    EXPECT_EQ(box->text(), "Options");

    box->destroy();
    delete box;
}

TEST(GroupBox, SetEnabledSyncsTheThemedStylesOwnEnabledFlag) {
    auto* box = new GroupBox();
    auto& groupBoxStyle = dynamic_cast<ThemedGroupBoxStyle&>(box->style());
    ASSERT_TRUE(groupBoxStyle.enabled);

    box->setEnabled(false);
    EXPECT_FALSE(groupBoxStyle.enabled);

    box->setEnabled(true);
    EXPECT_TRUE(groupBoxStyle.enabled);

    box->destroy();
    delete box;
}

TEST(GroupBox, PaintDoesNotCrashWithOrWithoutACaption) {
    auto* box = new GroupBox();
    box->setBounds(Rect(0, 0, 200, 100));

    BLImage image(200, 100, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    // No caption yet - paint()'s own early-return (text_.empty()) is
    // what this exercises.
    EXPECT_NO_THROW(box->paint(ctx));

    box->setText("Options");
    EXPECT_NO_THROW(box->paint(ctx));

    box->setEnabled(false);
    EXPECT_NO_THROW(box->paint(ctx));

    ctx.end();
    box->destroy();
    delete box;
}

TEST(GroupBox, PaintToleratesAZeroSize) {
    auto* box = new GroupBox();
    box->setText("Options");
    box->setBounds(Rect(0, 0, 0, 0));

    BLImage image(4, 4, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(box->paint(ctx));
    ctx.end();

    box->destroy();
    delete box;
}

TEST(GroupBox, SwappingToFluentGroupBoxStyleStaysSafe) {
    auto* box = new GroupBox();
    box->setBounds(Rect(0, 0, 200, 100));
    box->setText("Options");
    box->setStyle(std::make_unique<FluentGroupBoxStyle>());

    EXPECT_NO_THROW(box->setEnabled(false));
    EXPECT_NO_THROW(box->setEnabled(true));

    BLImage image(200, 100, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(box->paint(ctx));
    ctx.end();

    box->destroy();
    delete box;
}

TEST(GroupBox, SwappingToAnIncompatibleViewStyleFailsLoudNotSilently) {
    auto* box = new GroupBox();
    box->setStyle(std::make_unique<ButtonStyle>());

    EXPECT_THROW(box->setEnabled(false), std::bad_cast);

    box->destroy();
    delete box;
}

TEST(FluentGroupBoxStyle, ComputeClientBoundsIsUnclippedNoNativeChromeToDeflateFor) {
    FluentGroupBoxStyle style;
    Size size(200.0f, 100.0f);

    Rect clientBounds = style.computeClientBounds(size);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 200.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 100.0f);
}

TEST(FluentGroupBoxStyle, PaintDoesNotThrowEnabledOrDisabled) {
    BLImage image(200, 100, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool enabled : { true, false }) {
        FluentGroupBoxStyle style;
        style.enabled = enabled;
        Rect clientBounds;
        EXPECT_NO_THROW(style.paint(ctx, Size(200.0f, 100.0f), false, clientBounds));
    }

    ctx.end();
}

// ---------------------------------------------------------------------
// Slider/Progress - style()/thumb()/fill() no longer cache a typed
// ThemedTrackbarTrackStyle*/ThemedTrackbarThumbStyle*/
// ThemedProgressBarTrackStyle*/ThemedProgressBarFillStyle* member -
// each derives it live via dynamic_cast on every use, same "style() can
// be swapped out from under this Control" reasoning Button/Toggle/
// ToolbarButton already went through (see the comment block above
// Button.SwappingToADifferentThemedButtonStyleSubclassStaysSafe).
// ---------------------------------------------------------------------

TEST(Slider, SwappingToFluentTrackAndThumbStyleStaysSafe) {
    auto* slider = new Slider();
    slider->setBounds(Rect(0, 0, 200, 20));
    slider->setStyle(std::make_unique<FluentTrackbarTrackStyle>());
    slider->thumb()->setStyle(std::make_unique<FluentTrackbarThumbStyle>());

    EXPECT_NO_THROW(slider->setValue(50.0f));
    EXPECT_NO_THROW(slider->setHorizontal(false));
    EXPECT_NO_THROW(slider->setEnabled(false));
    EXPECT_NO_THROW(slider->setEnabled(true));

    BLImage image(200, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(slider->paint(ctx));
    ctx.end();

    slider->destroy();
    delete slider;
}

TEST(Slider, SwappingToAnIncompatibleTrackStyleFailsLoudNotSilently) {
    auto* slider = new Slider();
    slider->setBounds(Rect(0, 0, 200, 20));
    // A real ViewStyle, but not a ThemedTrackbarTrackStyle at all - no
    // .horizontal field, same shape as Button's own equivalent test.
    slider->setStyle(std::make_unique<ButtonStyle>());

    EXPECT_THROW(slider->setHorizontal(false), std::bad_cast);

    slider->destroy();
    delete slider;
}

TEST(Progress, SwappingToFluentTrackAndFillStyleStaysSafe) {
    auto* progress = new Progress();
    progress->setBounds(Rect(0, 0, 200, 12));
    progress->setStyle(std::make_unique<FluentProgressBarTrackStyle>());
    progress->fill()->setStyle(std::make_unique<FluentProgressBarFillStyle>());

    EXPECT_NO_THROW(progress->setValue(0.5f));
    EXPECT_NO_THROW(progress->setHorizontal(false));
    EXPECT_NO_THROW(progress->setFillState(ThemedProgressBarFillStyle::FillState::Error));
    EXPECT_EQ(progress->fillState(), ThemedProgressBarFillStyle::FillState::Error);

    BLImage image(200, 12, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(progress->paint(ctx));
    ctx.end();

    progress->destroy();
    delete progress;
}

TEST(Progress, FillStateAccessorsThrowWhenFillStyleIsIncompatible) {
    auto* progress = new Progress();
    progress->fill()->setStyle(std::make_unique<ButtonStyle>());

    EXPECT_THROW(progress->fillState(), std::runtime_error);
    EXPECT_THROW(progress->setFillState(ThemedProgressBarFillStyle::FillState::Paused), std::runtime_error);

    progress->destroy();
    delete progress;
}

TEST(FluentTrackbarTrackStyle, ComputeClientBoundsIsUnclippedNoNativeChromeToDeflateFor) {
    FluentTrackbarTrackStyle style;
    Size size(200.0f, 20.0f);

    Rect clientBounds = style.computeClientBounds(size);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 200.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 20.0f);
}

TEST(FluentTrackbarTrackStyle, PaintDoesNotThrowForEitherOrientation) {
    BLImage image(200, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool horizontal : { true, false }) {
        FluentTrackbarTrackStyle style;
        style.horizontal = horizontal;
        Rect clientBounds;
        EXPECT_NO_THROW(style.paint(ctx, Size(200.0f, 20.0f), false, clientBounds));
    }

    ctx.end();
}

TEST(FluentTrackbarThumbStyle, PaintDoesNotThrowAcrossEveryState) {
    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool enabled : { true, false }) {
        for (bool pressed : { true, false }) {
            for (bool highlighted : { true, false }) {
                FluentTrackbarThumbStyle style;
                style.enabled = enabled;
                style.pressed = pressed;
                Rect clientBounds;
                EXPECT_NO_THROW(style.paint(ctx, Size(20.0f, 20.0f), highlighted, clientBounds));
            }
        }
    }

    ctx.end();
}

TEST(FluentTrackbarThumbStyle, PaintToleratesAZeroOrSubPixelSize) {
    BLImage image(4, 4, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    FluentTrackbarThumbStyle style;
    Rect clientBounds;
    EXPECT_NO_THROW(style.paint(ctx, Size(1.0f, 1.0f), false, clientBounds));
    EXPECT_NO_THROW(style.paint(ctx, Size(0.0f, 0.0f), false, clientBounds));

    ctx.end();
}

TEST(FluentProgressBarTrackStyle, PaintDoesNotThrowForEitherOrientation) {
    BLImage image(200, 12, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool horizontal : { true, false }) {
        FluentProgressBarTrackStyle style;
        style.horizontal = horizontal;
        Rect clientBounds;
        EXPECT_NO_THROW(style.paint(ctx, Size(200.0f, 12.0f), false, clientBounds));
    }

    ctx.end();
}

TEST(FluentProgressBarFillStyle, PaintDoesNotThrowAcrossEveryFillState) {
    BLImage image(200, 12, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (auto state : { ThemedProgressBarFillStyle::FillState::Normal,
            ThemedProgressBarFillStyle::FillState::Error,
            ThemedProgressBarFillStyle::FillState::Paused }) {
        FluentProgressBarFillStyle style;
        style.state = state;
        Rect clientBounds;
        // Also covers a fill narrower than it is tall (a low value()) -
        // radius = shortSide/2 has to stay sane even when shortSide is
        // the *width*, not just the more common height case.
        EXPECT_NO_THROW(style.paint(ctx, Size(5.0f, 12.0f), false, clientBounds));
    }

    ctx.end();
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

TEST(ScrollView, AcceptsFocusByDefault) {
    // Lets keyboard users Tab into a scrollable region that hosts no
    // focusable content of its own (e.g. read-only text) and page/arrow
    // through it - see UIInputManager::moveFocus() (uiinputmanager.h).
    auto* view = new ScrollView();
    EXPECT_TRUE(view->acceptsFocus());
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

TEST(ScrollView, ReorderChildRedirectsIntoTheViewportLikeAddChildDoes) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));

    auto* a = new SubView();
    a->setVisible(true);
    auto* b = new SubView();
    b->setVisible(true);
    view->addChild(a);
    view->addChild(b);

    // Real content lives in viewport_, not view's own childViews() - see
    // AddChildDoesNotBecomeADirectChildOfScrollViewItself above - so a
    // naive base-class reorderChild() searching view's own childViews()
    // would silently no-op. Confirmed correct by checking the parent's
    // (viewport_'s) own child order changed.
    View* viewport = a->parent();
    ASSERT_EQ(viewport->childViews()[0], a);
    ASSERT_EQ(viewport->childViews()[1], b);

    view->reorderChild(b, 0);

    EXPECT_EQ(viewport->childViews()[0], b);
    EXPECT_EQ(viewport->childViews()[1], a);

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

// Real, live-reported bug: expanding a TreeView row (or any other in-place content growth -
// more text typed, a different font, ...) grows the content child's own contentSize() answer,
// but nothing told this ScrollView so until some unrelated event (a resize) happened to call
// updateLayout() again - handleContentChildContentSizeChanged() (controls.h) was declared and
// documented but never actually implemented or subscribed in addChild(), so onContentSizeChanged
// firing on a content child was a complete no-op as far as the ScrollView hosting it was
// concerned.
TEST(ScrollView, ContentChildAnnouncingAGrownContentSizeUpdatesBarsWithoutAnExternalTrigger) {
    auto* view = new ScrollView();
    view->setBounds(Rect(0, 0, 200, 200));

    auto* content = new SubView();
    content->setVisible(true);
    Size reportedSize(100.0f, 100.0f);
    content->onQueryContentSize.add([&reportedSize](View&, Size& outSize) -> SyncReturn {
        outSize = reportedSize;
        return SyncReturn::Handled;
    });
    view->addChild(content);

    ASSERT_FALSE(view->vBar()->isVisible()) << "100x100 content fits a 200x200 viewport";

    // content itself now answers a taller size and announces it via onContentSizeChanged -
    // this ScrollView is never resized, and setContentSize()/addChild() are never called again.
    reportedSize = Size(100.0f, 800.0f);
    content->onContentSizeChanged(*content);

    EXPECT_EQ(view->contentSize(), Size(100.0f, 800.0f));
    EXPECT_TRUE(view->vBar()->isVisible());

    view->destroy();
    delete view;
}

// ---------------------------------------------------------------------
// Button - disabled-state visuals (same gap ToolbarButton had - see
// feedback_paint_state_tests_dont_prove_visual_correctness memory)
// ---------------------------------------------------------------------

TEST(Button, SetEnabledSyncsTheThemedStylesOwnEnabledFlag) {
    auto* button = new Button();
    auto* style = dynamic_cast<ThemedButtonStyle*>(&button->style());
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->enabled);

    button->setEnabled(false);
    EXPECT_FALSE(style->enabled);

    button->setEnabled(true);
    EXPECT_TRUE(style->enabled);

    button->destroy();
    delete button;
}

// Real pixel-level check, not just internal state - see ToolbarButton's
// own identical test above for why a style-field-only assertion isn't
// enough (the native BUTTON chrome here does visibly change for
// PBS_DISABLED, but the text drawn on top of it needs its own dimming
// too, and that's what this actually proves).
TEST(Button, DisabledButtonRendersVisiblyDifferentPixelsThanEnabled) {
    auto renderButton = [](bool enabled) {
        auto* button = new Button();
        button->setBounds(Rect(0, 0, 80, 24));
        button->setText("Click Me");
        button->setEnabled(enabled);

        BLImage image(80, 24, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.clear_all();
        button->paint(ctx);
        ctx.end();

        BLImageData data;
        image.get_data(&data);
        std::vector<uint8_t> pixels(
            static_cast<const uint8_t*>(data.pixel_data),
            static_cast<const uint8_t*>(data.pixel_data) + data.stride * 24);

        button->destroy();
        delete button;
        return pixels;
    };

    EXPECT_NE(renderButton(true), renderButton(false));
}

// Real, live-reported crash: ViewStyle::font()/setFont() (now a real, Properties-panel-editable
// property, not just a plain field - see FontPropertyEditor, cpp_codetools) lets a user pick any
// font name and independently tick "bold" - a combination Font::blFont() has no guarantee will
// resolve. The exact combination reported live: "Trebuchet MS Bold" is itself a real, separately
// installed face name (not "Trebuchet MS" + a synthesized bold variant), so picking it as the
// font *name* and also ticking "bold" makes blFont() look up "Trebuchet MS Bold Bold", which
// doesn't exist. Button::paint() used to treat "didn't resolve" as a "shouldn't happen" invariant
// and threw, crashing the whole app the moment this now-reachable combination came up - it must
// degrade gracefully (skip drawing the text) instead, same as LabelStyle::paint() already does
// for its own now-editable font.
TEST(Button, PaintWithAnUnresolvedFontDoesNotThrowAndSkipsDrawingText) {
    // Skips (rather than failing) if this machine has no such face installed at all - the point
    // is the "name alone resolves, but name+bold together doesn't" combination, not this exact
    // family.
    const std::vector<SystemFontInfo>& fonts = FontManager::listFonts();
    bool hasTrebuchetBold = std::any_of(fonts.begin(), fonts.end(),
        [](const SystemFontInfo& info) { return info.name == "Trebuchet MS Bold"; });
    if (!hasTrebuchetBold) {
        GTEST_SKIP() << "this machine has no 'Trebuchet MS Bold' font face installed";
    }

    auto* button = new Button();
    button->setBounds(Rect(0, 0, 80, 24));
    button->setText("Click Me");

    auto* style = dynamic_cast<ThemedButtonStyle*>(&button->style());
    ASSERT_NE(style, nullptr);
    Font font = style->font();
    font.setName("Trebuchet MS Bold");
    font.setBold(true);
    style->setFont(font);
    ASSERT_EQ(font.blFont(), nullptr) << "test assumption: 'Trebuchet MS Bold Bold' must not resolve";

    BLImage image(80, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(button->paint(ctx));
    ctx.end();

    button->destroy();
    delete button;
}

// ---------------------------------------------------------------------
// Button/ToolbarButton no longer cache a typed ThemedButtonStyle*/
// ThemedToolbarButtonStyle* member - each derives it live via
// dynamic_cast<...&>(style()) on every use instead (Button::
// updatePressedVisual()'s own comment, controls.cpp, has the full
// reasoning). These prove the actual safety property that refactor
// exists for: setStyle() (public on View) can be called on an already-
// constructed Button at any time, and it must never dangle or silently
// misbehave against the wrong layout - it must keep working (a
// compatible ThemedButtonStyle subclass) or fail loud (std::bad_cast on
// an incompatible one), never corrupt silently.
// ---------------------------------------------------------------------

TEST(Button, SwappingToADifferentThemedButtonStyleSubclassStaysSafe) {
    auto* button = new Button();
    button->setBounds(Rect(0, 0, 80, 24));
    button->setText("Click Me");

    button->setStyle(std::make_unique<FluentButtonStyle>());

    // Both read style() live (dynamic_cast) - neither should dangle into
    // the ThemedButtonStyle the constructor originally installed, now
    // long destroyed.
    EXPECT_NO_THROW(button->setEnabled(false));
    EXPECT_NO_THROW(button->setEnabled(true));

    BLImage image(80, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(button->paint(ctx));
    ctx.end();

    button->destroy();
    delete button;
}

TEST(Button, SwappingToAnIncompatibleViewStyleFailsLoudNotSilently) {
    auto* button = new Button();
    button->setBounds(Rect(0, 0, 80, 24));

    // ButtonStyle (viewstyle.h) - a real ViewStyle, but not a
    // ThemedButtonStyle at all, so it has no .pressed/.enabled fields.
    // setStyle() itself (View::, view.h) accepts any ViewStyle
    // subclass - nothing at compile time stops this.
    button->setStyle(std::make_unique<ButtonStyle>());

    EXPECT_THROW(button->setEnabled(false), std::bad_cast);

    button->destroy();
    delete button;
}

TEST(FluentButtonStyle, ComputeClientBoundsIsUnclippedNoNativeChromeToDeflateFor) {
    FluentButtonStyle style;
    Size size(100.0f, 30.0f);

    Rect clientBounds = style.computeClientBounds(size);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 100.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 30.0f);
}

TEST(FluentButtonStyle, PaintDoesNotThrowAcrossEveryState) {
    BLImage image(80, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool enabled : { true, false }) {
        for (bool pressed : { true, false }) {
            for (bool highlighted : { true, false }) {
                FluentButtonStyle style;
                style.enabled = enabled;
                style.pressed = pressed;
                Rect clientBounds;
                EXPECT_NO_THROW(style.paint(ctx, Size(80.0f, 24.0f), highlighted, clientBounds));
            }
        }
    }

    ctx.end();
}

TEST(Toggle, SwappingToFluentCheckBoxOrRadioStyleStaysSafe) {
    for (bool radioStyle : { false, true }) {
        auto* toggle = new Toggle();
        toggle->setBounds(Rect(0, 0, 20, 20));
        toggle->setRadioStyle(radioStyle);

        // rebuildStyle() (Toggle::) always installs the native
        // ThemedCheckBoxStyle/ThemedRadioButtonStyle pair itself - this
        // swaps in the Fluent subclass by hand, same opt-in shape as
        // Button/FluentButtonStyle above.
        if (radioStyle) {
            toggle->setStyle(std::make_unique<FluentRadioButtonStyle>());
        } else {
            toggle->setStyle(std::make_unique<FluentCheckBoxStyle>());
        }

        EXPECT_NO_THROW(toggle->setChecked(true));
        EXPECT_NO_THROW(toggle->setEnabled(false));
        EXPECT_NO_THROW(toggle->setEnabled(true));

        BLImage image(20, 20, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.clear_all();
        EXPECT_NO_THROW(toggle->paint(ctx));
        ctx.end();

        toggle->destroy();
        delete toggle;
    }
}

TEST(FluentCheckBoxStyle, ComputeClientBoundsIsUnclippedNoNativeChromeToDeflateFor) {
    FluentCheckBoxStyle style;
    Size size(20.0f, 20.0f);

    Rect clientBounds = style.computeClientBounds(size);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 20.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 20.0f);
}

TEST(FluentCheckBoxStyle, PaintDoesNotThrowAcrossEveryState) {
    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool enabled : { true, false }) {
        for (bool checked : { true, false }) {
            for (bool pressed : { true, false }) {
                for (bool highlighted : { true, false }) {
                    FluentCheckBoxStyle style;
                    style.enabled = enabled;
                    style.checked = checked;
                    style.pressed = pressed;
                    Rect clientBounds;
                    EXPECT_NO_THROW(style.paint(ctx, Size(20.0f, 20.0f), highlighted, clientBounds));
                }
            }
        }
    }

    ctx.end();
}

TEST(FluentRadioButtonStyle, PaintDoesNotThrowAcrossEveryState) {
    BLImage image(20, 20, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool enabled : { true, false }) {
        for (bool checked : { true, false }) {
            for (bool pressed : { true, false }) {
                for (bool highlighted : { true, false }) {
                    FluentRadioButtonStyle style;
                    style.enabled = enabled;
                    style.checked = checked;
                    style.pressed = pressed;
                    Rect clientBounds;
                    EXPECT_NO_THROW(style.paint(ctx, Size(20.0f, 20.0f), highlighted, clientBounds));
                }
            }
        }
    }

    ctx.end();
}

TEST(FluentRadioButtonStyle, PaintToleratesAZeroOrSubPixelSize) {
    // outerR = minDim*0.5f - 1.0f can go negative/zero for a tiny size -
    // the early-return guard (paint(), viewstyle.cpp) is what this
    // catches; without it, add_circle() with a negative radius is the
    // real thing under test here.
    BLImage image(4, 4, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    FluentRadioButtonStyle style;
    Rect clientBounds;
    EXPECT_NO_THROW(style.paint(ctx, Size(1.0f, 1.0f), false, clientBounds));
    EXPECT_NO_THROW(style.paint(ctx, Size(0.0f, 0.0f), false, clientBounds));

    ctx.end();
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

// Real bug: ThemedToolbarButtonStyle::enabled (a separate field from
// Control::isEnabled(), used only to pick the native theme part state -
// see its own stateId()) was never synced from setEnabled() at all, so a
// disabled ToolbarButton's background never actually painted as disabled.
TEST(ToolbarButton, SetEnabledSyncsTheThemedStylesOwnEnabledFlag) {
    auto* button = new ToolbarButton();
    auto* style = dynamic_cast<ThemedToolbarButtonStyle*>(&button->style());
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->enabled);

    button->setEnabled(false);
    EXPECT_FALSE(style->enabled);

    button->setEnabled(true);
    EXPECT_TRUE(style->enabled);

    button->destroy();
    delete button;
}

// Real pixel-level check, not just internal state - style->enabled alone
// (see SetEnabledSyncsTheThemedStylesOwnEnabledFlag above) doesn't prove
// anything visually differs, since a flat toolbar button shows no visible
// border/fill at rest either way; the text/icon dimming in paint() is what
// actually has to differ. Same isPixelPainted-style verify-by-rendering
// idiom test_rootviewproxy.cpp already established, extended to a full
// buffer comparison since dimming shows up as a color difference across
// many pixels, not a single painted/unpainted one.
TEST(ToolbarButton, DisabledButtonRendersVisiblyDifferentPixelsThanEnabled) {
    auto renderButton = [](bool enabled) {
        auto* button = new ToolbarButton();
        button->setBounds(Rect(0, 0, 50, 24));
        button->setText("New");
        button->setEnabled(enabled);

        BLImage image(50, 24, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.clear_all();
        button->paint(ctx);
        ctx.end();

        BLImageData data;
        image.get_data(&data);
        std::vector<uint8_t> pixels(
            static_cast<const uint8_t*>(data.pixel_data),
            static_cast<const uint8_t*>(data.pixel_data) + data.stride * 24);

        button->destroy();
        delete button;
        return pixels;
    };

    EXPECT_NE(renderButton(true), renderButton(false));
}

TEST(ToolbarButton, IconDefaultsToEmpty) {
    ToolbarButton button;
    EXPECT_TRUE(button.icon().empty());
}

TEST(ToolbarButton, AcceptsFocusByDefault) {
    ToolbarButton button;
    EXPECT_TRUE(button.acceptsFocus());
}

TEST(ToolbarButton, SetIconChangesTheStoredValue) {
    ToolbarButton button;
    button.setIcon("Images/icons/toolbar/new.svg");
    EXPECT_EQ(button.icon(), "Images/icons/toolbar/new.svg");
}

TEST(ToolbarButton, PaintWithAnIconAndTextDoesNotCrash) {
    auto* button = new ToolbarButton();
    button->setBounds(Rect(0, 0, 50, 24));
    button->setText("New");
    button->setIcon("Images/icons/toolbar/new.svg");  // resolves to nothing in this test binary - exercises the "icon failed to load" path, not a real blit

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    button->paint(ctx);

    button->destroy();
    delete button;
}

TEST(ToolbarButton, PaintWithOnlyAnIconAndNoTextDoesNotCrash) {
    auto* button = new ToolbarButton();
    button->setBounds(Rect(0, 0, 24, 24));
    button->setIcon("Images/icons/toolbar/new.svg");

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    button->paint(ctx);

    button->destroy();
    delete button;
}

TEST(ToolbarButton, SwappingToFluentToolbarButtonStyleStaysSafe) {
    auto* button = new ToolbarButton();
    button->setBounds(Rect(0, 0, 24, 24));
    button->setStyle(std::make_unique<FluentToolbarButtonStyle>());

    EXPECT_NO_THROW(button->setToggleButton(true));
    EXPECT_NO_THROW(button->setChecked(true));
    EXPECT_NO_THROW(button->setEnabled(false));
    EXPECT_NO_THROW(button->setEnabled(true));

    BLImage image(24, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(button->paint(ctx));
    ctx.end();

    button->destroy();
    delete button;
}

TEST(FluentToolbarButtonStyle, ComputeClientBoundsIsUnclippedNoNativeChromeToDeflateFor) {
    FluentToolbarButtonStyle style;
    Size size(24.0f, 24.0f);

    Rect clientBounds = style.computeClientBounds(size);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 24.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 24.0f);
}

TEST(FluentToolbarButtonStyle, FlatAtRestPaintsNothingButStillDoesNotThrow) {
    // Matches the native TS_NORMAL "no visible border/fill at rest"
    // look this style replaces (see class comment, viewstyle.h) - the
    // real thing under test is paint()'s own early-return when neither
    // baseAlpha nor overlayAlpha is > 0, not a pixel-level assertion.
    BLImage image(24, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    FluentToolbarButtonStyle style;
    Rect clientBounds;
    EXPECT_NO_THROW(style.paint(ctx, Size(24.0f, 24.0f), false, clientBounds));

    ctx.end();
}

TEST(FluentToolbarButtonStyle, PaintDoesNotThrowAcrossEveryState) {
    BLImage image(24, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool enabled : { true, false }) {
        for (bool checked : { true, false }) {
            for (bool pressed : { true, false }) {
                for (bool highlighted : { true, false }) {
                    FluentToolbarButtonStyle style;
                    style.enabled = enabled;
                    style.checked = checked;
                    style.pressed = pressed;
                    Rect clientBounds;
                    EXPECT_NO_THROW(style.paint(ctx, Size(24.0f, 24.0f), highlighted, clientBounds));
                }
            }
        }
    }

    ctx.end();
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

// ---------------------------------------------------------------------------
// Keeping ThemedEditStyle::focused (viewstyle.h) in sync with real
// onGotFocus/onLostFocus - drives the real native ETS_FOCUSED border, and
// (see ThemedEditStyle::postPaint()) is why these controls suppress
// View's generic dashed focus ring rather than drawing both. Fired directly
// on the delegate (field->onGotFocus(*field), not through a real RootView -
// same "no live HWND/RunLoop needed" pattern
// SetControllerReplacesTheControllerAndReachesASubclassOverride above
// already uses; RunLoop::current() is null in this headless test process,
// so TextController::handleGotFocus()'s own caret_.start() call is
// naturally skipped, same as that test's own comment describes.
// ---------------------------------------------------------------------------

TEST(TextField, GotAndLostFocusToggleThemedEditStyleFocused) {
    auto* field = new TextField();
    auto& editStyle = dynamic_cast<ThemedEditStyle&>(field->style());
    ASSERT_FALSE(editStyle.focused);

    field->onGotFocus(*field);
    EXPECT_TRUE(editStyle.focused);

    field->onLostFocus(*field);
    EXPECT_FALSE(editStyle.focused);

    field->destroy();
    delete field;
}

TEST(TextField, SwappingToFluentEditStyleStaysSafe) {
    auto* field = new TextField();
    field->setBounds(Rect(0, 0, 120, 24));
    field->setStyle(std::make_unique<FluentEditStyle>());

    // TextController::handleGotFocus()/handleLostFocus() (controls.cpp)
    // dynamic_cast style() to ThemedEditStyle* - FluentEditStyle still
    // satisfies that, same opt-in shape as every other Fluent* style.
    field->onGotFocus(*field);
    auto& editStyle = dynamic_cast<ThemedEditStyle&>(field->style());
    EXPECT_TRUE(editStyle.focused);
    field->onLostFocus(*field);
    EXPECT_FALSE(editStyle.focused);

    BLImage image(120, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();
    EXPECT_NO_THROW(field->paint(ctx));
    ctx.end();

    field->destroy();
    delete field;
}

TEST(FluentEditStyle, ComputeClientBoundsDeflatesForBorderAndFocusUnderline) {
    FluentEditStyle style;
    Size size(120.0f, 24.0f);

    Rect clientBounds = style.computeClientBounds(size);

    // Unlike FluentButtonStyle (no native chrome to deflate for at all),
    // this one reserves real room for its own border/underline - just
    // asserting it's strictly smaller than the full size, not pinned to
    // an exact inset, so the test doesn't need updating every time the
    // padding constants get tuned.
    EXPECT_LT(clientBounds.size().width, size.width);
    EXPECT_LT(clientBounds.size().height, size.height);
    EXPECT_GT(clientBounds.size().width, 0.0f);
    EXPECT_GT(clientBounds.size().height, 0.0f);
}

TEST(FluentEditStyle, PaintDoesNotThrowAcrossEveryState) {
    BLImage image(120, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.clear_all();

    for (bool enabled : { true, false }) {
        for (bool focused : { true, false }) {
            for (bool readOnly : { true, false }) {
                for (bool highlighted : { true, false }) {
                    FluentEditStyle style;
                    style.enabled = enabled;
                    style.focused = focused;
                    style.readOnly = readOnly;
                    Rect clientBounds;
                    EXPECT_NO_THROW(style.paint(ctx, Size(120.0f, 24.0f), highlighted, clientBounds));
                }
            }
        }
    }

    ctx.end();
}

TEST(TextControl, GotAndLostFocusToggleThemedEditStyleFocused) {
    auto* control = new TextControl();
    auto& editStyle = dynamic_cast<ThemedEditStyle&>(control->style());
    ASSERT_FALSE(editStyle.focused);

    control->onGotFocus(*control);
    EXPECT_TRUE(editStyle.focused);

    control->onLostFocus(*control);
    EXPECT_FALSE(editStyle.focused);

    control->destroy();
    delete control;
}

TEST(ListView, GotAndLostFocusToggleThemedEditStyleFocused) {
    auto* list = new ListView();
    auto& editStyle = dynamic_cast<ThemedEditStyle&>(list->style());
    ASSERT_FALSE(editStyle.focused);

    list->onGotFocus(*list);
    EXPECT_TRUE(editStyle.focused);

    list->onLostFocus(*list);
    EXPECT_FALSE(editStyle.focused);

    list->destroy();
    delete list;
}

TEST(TreeView, GotAndLostFocusToggleThemedEditStyleFocused) {
    auto* tree = new TreeView();
    auto& editStyle = dynamic_cast<ThemedEditStyle&>(tree->style());
    ASSERT_FALSE(editStyle.focused);

    tree->onGotFocus(*tree);
    EXPECT_TRUE(editStyle.focused);

    tree->onLostFocus(*tree);
    EXPECT_FALSE(editStyle.focused);

    tree->destroy();
    delete tree;
}

TEST(DropDownList, GotAndLostFocusToggleThemedEditStyleFocused) {
    auto* dropDown = new DropDownList();
    auto& editStyle = dynamic_cast<ThemedEditStyle&>(dropDown->style());
    ASSERT_FALSE(editStyle.focused);

    dropDown->onGotFocus(*dropDown);
    EXPECT_TRUE(editStyle.focused);

    dropDown->onLostFocus(*dropDown);
    EXPECT_FALSE(editStyle.focused);

    dropDown->destroy();
    delete dropDown;
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

    // No scrollbar of its own, and pinned to the viewport's own height
    // rather than grown to its true (much larger) content height - the
    // whole point of being hosted rather than standalone. No horizontal
    // bar either: TextControl's reported content width is always exactly
    // whatever width it's given (self-referential, wraps to fit), so it
    // never legitimately wants more horizontal space than its viewport -
    // a regression guard for a real bug where ScrollView::updateLayout()
    // mistook that self-referential width for "wants a horizontal bar"
    // any time a vertical bar was needed at all, silently stealing height
    // from every vertically-scrolling virtualized child.
    EXPECT_TRUE(textControl->childViews().empty());
    ASSERT_TRUE(scrollView->vBar()->isVisible());
    EXPECT_FALSE(scrollView->hBar()->isVisible());
    EXPECT_FLOAT_EQ(textControl->bounds().size().height, 60.0f);
    EXPECT_LT(textControl->bounds().size().height, textControl->contentSize().height);

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

// Real, live-reported bug, same class as ListView's own (see
// ListView.ManuallyScrollingAwayFromASelectedRowIsNotForciblyUndoneByTheNextPaint
// above): TextControl::paint() used to fire onRequestScrollIntoView() for
// the caret unconditionally every single call, so dragging the hosting
// ScrollView's own scrollbar away from the caret - which itself triggers a
// repaint - immediately snapped straight back to it on the very next
// paint(). A user could never scroll away from the caret to look at other
// text at all.
TEST(TextControl, ManuallyScrollingAwayFromTheCaretIsNotForciblyUndoneByTheNextPaint) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* textControl = new TextControl();
    textControl->setText(kManyLines);
    scrollView->addChild(textControl);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    textControl->paint(ctx);  // establishes real vBar range/pageSize, layout

    // Moves the caret to the very end of kManyLines - many lines down,
    // requiring the ScrollView to scroll to reveal it.
    textControl->caret().setPosition(text::TextPosition(kManyLines.size()));
    textControl->paint(ctx);  // paint() is what fires onRequestScrollIntoView

    ASSERT_GT(scrollView->vBar()->value(), 0.0f)
        << "test assumption: the caret at the end of many lines required scrolling down";

    // Simulates the user dragging the scrollbar thumb back to the top,
    // away from the still-blinking caret - same handleVBarValueChanged()
    // path a real drag goes through.
    scrollView->vBar()->setValue(0.0f);
    ASSERT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f);

    // A second, unrelated repaint must NOT snap back to the caret just
    // because it's still there - nothing about the caret's own position
    // changed.
    textControl->paint(ctx);

    EXPECT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f)
        << "an unrelated repaint must not re-fire onRequestScrollIntoView() for a caret that hasn't moved";

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

// Records which row indices a ListView actually asks to be painted -
// used to prove a given paint() call's own visible rows, not just the
// resulting vBar value, land at the corrected scroll offset. See
// ListView::paint()'s own comment (controls.cpp) for the bug this guards
// against: onRequestScrollIntoView() firing after (rather than before)
// scrollOffsetY_ is read for the row loop meant the *value* was already
// correct after one paint() but that same paint()'s actual pixels were
// still drawn one frame stale.
class SpyListController : public ListController {
public:
    std::vector<std::size_t> paintedIndices;

    ListItem* createItem(std::size_t index) override {
        paintedIndices.push_back(index);
        return ListController::createItem(index);
    }
};

}  // namespace

TEST(ListView, SetModelHandsOwnershipToTheControllerSoItOutlivesTheCallersPointer) {
    auto* listView = new ListView();
    auto model = std::make_unique<StringListModel>();
    model->items() = { "a", "b", "c" };
    StringListModel* raw = model.get();

    listView->setModel(std::move(model));

    EXPECT_EQ(listView->model(), raw);
    EXPECT_EQ(listView->controller().itemCount(), 3u);

    raw->addItem("d");   // a change through the model reaches the view's controller
    EXPECT_EQ(listView->controller().itemCount(), 4u);

    listView->setModel(nullptr);   // detaches and destroys it
    EXPECT_EQ(listView->model(), nullptr);
    EXPECT_EQ(listView->controller().itemCount(), 0u);

    listView->destroy();
    delete listView;
}

TEST(ListView, DefaultConstructedHasZeroItemCountAndNoSelection) {
    auto* listView = new ListView();

    EXPECT_EQ(listView->controller().itemCount(), 0u);
    EXPECT_FALSE(listView->selectedIndex().has_value());

    listView->destroy();
    delete listView;
}

TEST(StringListModelTest, ItemsChangeThroughTheModelAndFireOnChanged) {
    StringListModel model;
    int changes = 0;
    model.onChanged.add([&changes](Model&) { ++changes; return SyncReturn::Handled; });

    model.addItem("one");
    model.addItem("two");
    EXPECT_EQ(model.size(), 2u);
    EXPECT_EQ(std::any_cast<std::string>(model.valueAt(1)), "two");

    model.setValueAt(0, std::string("uno"));
    EXPECT_EQ(std::any_cast<std::string>(model.valueAt(0)), "uno");

    model.removeItem(0);
    model.removeItem(99);   // out of range: no-op, no notification
    EXPECT_EQ(model.size(), 1u);
    EXPECT_EQ(changes, 4);

    EXPECT_FALSE(model.valueAt(5).has_value());
    model.clear();
    EXPECT_TRUE(model.empty());
}

TEST(StringTreeModelTest, FlatDepthListResolvesIntoARealTreeByPath) {
    StringTreeModel model;
    model.rows() = {
        { 0, "Fruits" }, { 1, "Apple" }, { 1, "Banana" },
        { 0, "Vegetables" }, { 1, "Carrot" },
    };

    EXPECT_EQ(model.childCount({}), 2u);
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{0})), "Fruits");
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{1})), "Vegetables");

    EXPECT_EQ(model.childCount({0}), 2u);
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{0, 0})), "Apple");
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{0, 1})), "Banana");
    EXPECT_EQ(model.childCount({0, 0}), 0u);

    EXPECT_EQ(model.childCount({1}), 1u);
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{1, 0})), "Carrot");

    // Out of range at every level: no crash, no value.
    EXPECT_EQ(model.childCount({5}), 0u);
    EXPECT_FALSE(model.value(std::vector<std::size_t>{5}).has_value());
    EXPECT_FALSE(model.value(std::vector<std::size_t>{0, 5}).has_value());
}

TEST(StringTreeModelTest, ADeeperRowThanItsParentPlusOneIsAGrandchildNotSkippedEntirely) {
    // Depth jumps by more than one (2 right after a depth-0 row) - Banana is still Apple's child,
    // not Fruits', even though nothing at depth 1 separates them.
    StringTreeModel model;
    model.rows() = { { 0, "Fruits" }, { 1, "Apple" }, { 2, "Gala" }, { 2, "Fuji" } };

    EXPECT_EQ(model.childCount({}), 1u);
    EXPECT_EQ(model.childCount({0}), 1u);
    ASSERT_EQ(model.childCount({0, 0}), 2u);
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{0, 0, 0})), "Gala");
    EXPECT_EQ(std::any_cast<std::string>(model.value(std::vector<std::size_t>{0, 0, 1})), "Fuji");
}

TEST(StringTreeModelTest, AddItemAppendsARootRowRemoveLastItemDropsTheLastRowRegardlessOfDepthAndFiresOnChanged) {
    StringTreeModel model;
    int changes = 0;
    model.onChanged.add([&changes](Model&) { ++changes; return SyncReturn::Handled; });

    model.addItem("Fruits");
    model.rows().push_back(TreeRow{ 1, "Apple" });   // a child, added directly (addItem() is root-only)
    EXPECT_EQ(model.childCount({0}), 1u);
    EXPECT_EQ(changes, 1);

    model.removeLastItem();   // drops "Apple", a depth-1 row - not required to be a root row
    EXPECT_EQ(model.childCount({0}), 0u);
    EXPECT_EQ(changes, 2);

    model.removeLastItem();
    EXPECT_TRUE(model.empty());
    model.removeLastItem();   // empty: no-op, no notification
    EXPECT_EQ(changes, 3);
}

TEST(StringTreeModelTest, SetValueWritesTheTextAtAPathAndFiresOnChanged) {
    StringTreeModel model;
    model.rows() = { { 0, "Fruits" }, { 1, "Apple" } };
    int changes = 0;
    model.onChanged.add([&changes](Model&) { ++changes; return SyncReturn::Handled; });

    model.setValue(std::string("Pear"), std::vector<std::size_t>{0, 0});
    EXPECT_EQ(model.rows()[1].text, "Pear");
    EXPECT_EQ(changes, 1);

    model.setValue(std::string("nope"), std::vector<std::size_t>{9});   // out of range: rows untouched
    EXPECT_EQ(model.rows()[1].text, "Pear");
    EXPECT_EQ(changes, 2);   // still fires - same "notify regardless" contract as the base Model::setValue()
}

TEST(TreeView, SetModelHandsOwnershipToTheControllerSoItOutlivesTheCallersPointer) {
    auto* treeView = new TreeView();
    auto model = std::make_unique<StringTreeModel>();
    model->rows() = { { 0, "a" }, { 1, "b" } };
    StringTreeModel* raw = model.get();

    treeView->setModel(std::move(model));

    EXPECT_EQ(treeView->model(), raw);
    EXPECT_EQ(treeView->controller().visibleCount(), 1u);   // "a" only - "b" starts collapsed

    treeView->setModel(nullptr);   // detaches and destroys it
    EXPECT_EQ(treeView->model(), nullptr);

    treeView->destroy();
    delete treeView;
}

TEST(ListView, ContentSizeIsItemCountTimesRowHeight) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 60));

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "short", "tall", "short again" };
    listView->setModel(std::move(modelOwner));

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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    listView->setModel(std::move(modelOwner));

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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b" };
    listView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));
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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

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

// ---------------------------------------------------------------------
// ListView - keyboard navigation (handleKeyDown(), controls.cpp). Mirrors
// the plain/Ctrl/Shift click conventions above but driven by Up/Down/Home/
// End instead of a clicked row - see handleKeyDown()'s own doc comment
// (controls.h) for the full plain/Shift/Ctrl+Arrow/Ctrl+Space contract.
// ---------------------------------------------------------------------

namespace {

void PressKey(ListView* listView, std::uint32_t VKeyCode, std::uint32_t keyMask) {
    listView->onKeyDown(*listView, keyMask, 0, 1, VKeyCode);
}

}  // namespace

TEST(ListView, PlainArrowMovesAndReplacesSelection) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

    listView->setSelectedIndex(1u);
    PressKey(listView, vkDownArrow, 0);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 2u }));

    PressKey(listView, vkUpArrow, 0);
    PressKey(listView, vkUpArrow, 0);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 0u }))
        << "Up Arrow at row 0 must clamp, not wrap or go negative";

    listView->destroy();
    delete listView;
}

TEST(ListView, DownArrowClampsAtTheLastRow) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    listView->setModel(std::move(modelOwner));

    listView->setSelectedIndex(2u);
    PressKey(listView, vkDownArrow, 0);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 2u }));

    listView->destroy();
    delete listView;
}

TEST(ListView, HomeAndEndJumpToTheFirstAndLastRow) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

    listView->setSelectedIndex(2u);
    PressKey(listView, vkEnd, 0);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 4u }));

    PressKey(listView, vkHome, 0);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 0u }));

    listView->destroy();
    delete listView;
}

TEST(ListView, ShiftArrowExtendsFromTheAnchorAndKeepsExtending) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

    ClickRow(listView, 1, 0);  // sets selectionAnchor_ to row 1, like a plain click

    PressKey(listView, vkDownArrow, kmShift);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u, 2u }));

    // A second Shift+Down must extend further from row 2, not reset back
    // to ranging from the anchor to row 2 again.
    PressKey(listView, vkDownArrow, kmShift);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u, 2u, 3u }));

    listView->destroy();
    delete listView;
}

TEST(ListView, CtrlArrowMovesTheHighlightWithoutChangingSelection) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

    listView->setSelectedIndex(1u);
    PressKey(listView, vkDownArrow, kmCtrl);

    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u }))
        << "Ctrl+Arrow must not touch real selection";
    ASSERT_TRUE(listView->keyboardHighlightedIndex().has_value());
    EXPECT_EQ(*listView->keyboardHighlightedIndex(), 2u);

    listView->destroy();
    delete listView;
}

TEST(ListView, CtrlSpaceTogglesSelectionAtTheHighlightedRow) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));

    listView->setSelectedIndex(1u);
    PressKey(listView, vkDownArrow, kmCtrl);  // highlight -> row 2, selection still just {1}
    PressKey(listView, vkSpaceBar, kmCtrl);

    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u, 2u }));

    // Toggling again clears it back off.
    PressKey(listView, vkSpaceBar, kmCtrl);
    EXPECT_EQ(listView->selectedIndices(), (std::set<std::size_t>{ 1u }));

    listView->destroy();
    delete listView;
}

TEST(ListView, ArrowKeysAreANoOpWithNoRows) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 200));

    PressKey(listView, vkDownArrow, 0);  // must not crash with itemCount() == 0

    EXPECT_FALSE(listView->selectedIndex().has_value());

    listView->destroy();
    delete listView;
}

TEST(ListView, PaintDoesNotCrashAndReusesASinglePooledItemAcrossRowsAndCalls) {
    auto* listView = new ListView();
    listView->setBounds(Rect(0, 0, 100, 60));

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c", "d", "e" };
    listView->setModel(std::move(modelOwner));
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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(std::move(modelOwner));
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
// ScrollView::handleContentRequestScrollIntoView() (controls.cpp) - a
// content child's onRequestScrollIntoView (view.h, fired from paint())
// actually nudging the hosting ScrollView's own scrollbar. Confirmed
// live as a real, pre-existing gap: onRequestScrollIntoView was fired in
// five places across this codebase (TextControl's caret, ListView/
// TreeView's selection and keyboard highlight) but never subscribed to
// anywhere - arrow-key-driven selection moved correctly but never
// scrolled the newly-selected row into view once it left the viewport.
// ---------------------------------------------------------------------

TEST(ListView, SelectingARowBelowTheViewportScrollsDownToRevealIt) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* listView = new ListView();
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(std::move(modelOwner));
    scrollView->addChild(listView);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);  // establishes real vBar range/pageSize
    ASSERT_TRUE(scrollView->vBar()->isVisible());
    ASSERT_FALSE(scrollView->hBar()->isVisible())
        << "ListView content is exactly as wide as its viewport - a "
           "horizontal bar here would silently shrink viewportHeight and "
           "throw off the scroll-into-view target below";
    ASSERT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f);

    listView->setSelectedIndex(40u);
    listView->paint(ctx);  // paint() is what fires onRequestScrollIntoView

    float rowTop = listView->controller().itemOffset(40);
    float rowBottom = rowTop + listView->controller().itemHeight(40);
    float value = scrollView->vBar()->value();
    float pageSize = scrollView->vBar()->pageSize();

    EXPECT_GT(value, 0.0f) << "row 40 required scrolling down from the top";
    EXPECT_GE(rowTop, value - 0.01f);
    EXPECT_LE(rowBottom, value + pageSize + 0.01f);
    // Pinned exact value, not just the tolerant bounds above: with the
    // full 60px viewport height (no phantom horizontal bar stealing 16px
    // for a fallback thickness), row 40 (top=800, bottom=820) must land
    // scrolled so its bottom is flush with the viewport bottom.
    EXPECT_FLOAT_EQ(value, 760.0f);

    scrollView->destroy();
    delete scrollView;
}

// Real, live-reported bug: paint() used to fire onRequestScrollIntoView()
// unconditionally every single call (whenever selectedIndex() had a value
// at all, not just when it actually changed) - so dragging this
// ScrollView's own scrollbar to look at other rows, which itself triggers
// a repaint (ScrollBar::onValueChanged -> handleVBarValueChanged() ->
// child->redraw()), immediately snapped straight back to the still-
// selected row on the very next paint(). The user could never actually
// scroll a selected row *out* of view with the scrollbar at all.
TEST(ListView, ManuallyScrollingAwayFromASelectedRowIsNotForciblyUndoneByTheNextPaint) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* listView = new ListView();
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(std::move(modelOwner));
    scrollView->addChild(listView);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);

    listView->setSelectedIndex(40u);
    listView->paint(ctx);
    ASSERT_FLOAT_EQ(scrollView->vBar()->value(), 760.0f) << "test assumption: paint() scrolled to reveal row 40, same as the sibling test above";

    // Simulates the user dragging the scrollbar thumb back to the top,
    // away from the still-selected row 40 - same handleVBarValueChanged()
    // path a real drag goes through.
    scrollView->vBar()->setValue(0.0f);
    ASSERT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f);

    // A second, unrelated repaint - e.g. anything else in the window
    // invalidating, or simply the redraw() the manual scroll itself
    // triggered - must NOT snap the view back to row 40 just because it's
    // still selectedIndex(); nothing about the *selection* changed.
    listView->paint(ctx);

    EXPECT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f)
        << "an unrelated repaint must not re-fire onRequestScrollIntoView() for a selection that hasn't changed";

    scrollView->destroy();
    delete scrollView;
}

TEST(ListView, SelectingARowBelowTheViewportRepaintsItInTheSamePaintCall) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* listView = new ListView();
    auto* spyController = new SpyListController();
    listView->setController(std::unique_ptr<ListController>(spyController));
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(std::move(modelOwner));
    scrollView->addChild(listView);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);  // establishes real vBar range/pageSize
    ASSERT_TRUE(scrollView->vBar()->isVisible());

    listView->setSelectedIndex(40u);
    spyController->paintedIndices.clear();
    listView->paint(ctx);

    // Row 40 must be among the rows THIS SAME paint() call actually drew
    // - not just something a *second* paint() would eventually catch up
    // to. Before the fix, onRequestScrollIntoView() fired after the row
    // loop had already read the stale scrollOffsetY_, so this call would
    // still only draw rows 0-2 (the old, pre-scroll viewport) and row 40
    // would only appear on a subsequent paint().
    EXPECT_NE(std::find(spyController->paintedIndices.begin(), spyController->paintedIndices.end(), 40u),
        spyController->paintedIndices.end())
        << "row 40 was not painted in the same paint() call that scrolled it into view";

    scrollView->destroy();
    delete scrollView;
}

TEST(ListView, SelectingARowAboveTheViewportScrollsUpToRevealIt) {
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* listView = new ListView();
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(std::move(modelOwner));
    scrollView->addChild(listView);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);
    ASSERT_TRUE(scrollView->vBar()->isVisible());

    scrollView->vBar()->setValue(scrollView->vBar()->maxValue());
    listView->paint(ctx);
    ASSERT_GT(scrollView->vBar()->value(), 0.0f);

    listView->setSelectedIndex(0u);
    listView->paint(ctx);

    EXPECT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f) << "row 0 required scrolling back up to the top";

    scrollView->destroy();
    delete scrollView;
}

TEST(ListView, HomeAndEndKeysScrollTheHostingScrollViewToo) {
    // Same end-to-end path as SelectingARow[Below/Above]TheViewport... above,
    // but driven through real handleKeyDown() (PressKey(), same helper
    // ListView's own arrow-key test group uses) instead of calling
    // setSelectedIndex() directly - vkHome/vkEnd share the exact same
    // setSelectedIndex() tail every other non-modified arrow key does
    // (see handleKeyDown()'s own switch, controls.cpp), so this is
    // mainly a regression guard confirming that shared path really does
    // behave identically when reached via Home/End specifically.
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* listView = new ListView();
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(std::move(modelOwner));
    scrollView->addChild(listView);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);
    ASSERT_TRUE(scrollView->vBar()->isVisible());
    ASSERT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f);

    PressKey(listView, vkEnd, 0);
    listView->paint(ctx);

    EXPECT_EQ(*listView->selectedIndex(), 49u);
    float lastRowBottom = listView->controller().itemOffset(49) + listView->controller().itemHeight(49);
    EXPECT_GE(scrollView->vBar()->value() + scrollView->vBar()->pageSize(), lastRowBottom - 0.01f)
        << "End must scroll all the way to the bottom, not leave the last row still offscreen";

    PressKey(listView, vkHome, 0);
    listView->paint(ctx);

    EXPECT_EQ(*listView->selectedIndex(), 0u);
    EXPECT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f)
        << "Home must scroll all the way back to the top";

    scrollView->destroy();
    delete scrollView;
}

TEST(ListView, HomeAfterRepeatedDownArrowsScrollsAllTheWayBackToTheTop) {
    // Reproduces the exact real-app sequence reported live: Tab into the
    // ListView (real keyboard focus, not a click - PressKey() drives
    // handleKeyDown() directly, same as any real focused-View key
    // dispatch), press Down repeatedly past the viewport (each one its
    // own real Down keystroke, not a single jump to a far index the way
    // ClickRow()/setSelectedIndex() elsewhere in this file would be), then
    // Home. Down alone was confirmed working live; Home was not.
    auto* scrollView = new ScrollView();
    scrollView->setBounds(Rect(0, 0, 100, 60));

    auto* listView = new ListView();
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    for (int i = 0; i < 50; ++i) {
        model.rows.push_back("row " + std::to_string(i));
    }
    listView->setModel(std::move(modelOwner));
    scrollView->addChild(listView);

    BLImage image(100, 60, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    listView->paint(ctx);
    ASSERT_TRUE(scrollView->vBar()->isVisible());

    listView->setSelectedIndex(0u);
    for (int i = 0; i < 20; ++i) {
        PressKey(listView, vkDownArrow, 0);
        listView->paint(ctx);
    }

    ASSERT_EQ(*listView->selectedIndex(), 20u);
    ASSERT_GT(scrollView->vBar()->value(), 0.0f) << "20 Down presses must have actually scrolled";

    PressKey(listView, vkHome, 0);
    listView->paint(ctx);

    EXPECT_EQ(*listView->selectedIndex(), 0u);
    EXPECT_FLOAT_EQ(scrollView->vBar()->value(), 0.0f)
        << "Home must scroll all the way back to the top even after many prior Down presses";

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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

    ClickTreeRow(treeView, 0, 50.0f, 0);        // select {0}
    ClickTreeRow(treeView, 1, 50.0f, kmCtrl);   // add {1}

    EXPECT_EQ(treeView->selectedPaths(), (std::set<std::vector<std::size_t>>{ { 0u }, { 1u } }));

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, ShiftClickSelectsARangeAcrossExpandedRows) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));
    treeView->controller().setExpanded({ 0u }, true);  // visible: {0}, {0,0}, {0,1}, {1}

    ClickTreeRow(treeView, 0, 50.0f, 0);         // anchor at visible row 0 ({0})
    ClickTreeRow(treeView, 2, 50.0f, kmShift);   // range to visible row 2 ({0,1})

    EXPECT_EQ(treeView->selectedPaths(), (std::set<std::vector<std::size_t>>{ { 0u }, { 0u, 0u }, { 0u, 1u } }));

    treeView->destroy();
    delete treeView;
}

// ---------------------------------------------------------------------
// TreeView - keyboard navigation (handleKeyDown(), controls.cpp). Up/Down/
// Home/End/Shift/Ctrl mirror ListView's own contract over visible row
// index; Left/Right add the tree-specific collapse-or-go-to-parent /
// expand-or-go-to-first-child pair - see handleKeyDown()'s own doc comment
// (controls.h). StubTreeRowModel (above): 2 root children, the first
// ({0}) has 2 of its own ({0,0}/{0,1}), the second ({1}) is a leaf.
// ---------------------------------------------------------------------

namespace {

void PressTreeKey(TreeView* treeView, std::uint32_t VKeyCode, std::uint32_t keyMask) {
    treeView->onKeyDown(*treeView, keyMask, 0, 1, VKeyCode);
}

}  // namespace

TEST(TreeView, PlainArrowMovesAndReplacesSelectionAcrossVisibleRows) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));  // collapsed: visible {0}, {1}

    treeView->setSelectedPath(std::vector<std::size_t>{ 0u });
    PressTreeKey(treeView, vkDownArrow, 0);

    ASSERT_TRUE(treeView->selectedPath().has_value());
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 1u }));

    PressTreeKey(treeView, vkDownArrow, 0);
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 1u })) << "Down at the last visible row must clamp";

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, RightArrowExpandsFirstThenMovesToTheFirstChildOnASecondPress) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

    treeView->setSelectedPath(std::vector<std::size_t>{ 0u });

    PressTreeKey(treeView, vkRightArrow, 0);
    EXPECT_TRUE(treeView->controller().isExpanded({ 0u }));
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 0u }))
        << "the first Right Arrow only expands - it doesn't also move";

    PressTreeKey(treeView, vkRightArrow, 0);
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 0u, 0u }));

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, LeftArrowCollapsesFirstThenMovesToTheParentOnASecondPress) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));
    treeView->controller().setExpanded({ 0u }, true);

    // A leaf (path {0,0} has no children of its own) - Left goes straight
    // to the parent, no collapse step to do first.
    treeView->setSelectedPath(std::vector<std::size_t>{ 0u, 0u });
    PressTreeKey(treeView, vkLeftArrow, 0);
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 0u }));

    // Now sitting on {0}, which IS expanded (has children) - first Left
    // collapses it rather than immediately jumping to its own parent.
    PressTreeKey(treeView, vkLeftArrow, 0);
    EXPECT_FALSE(treeView->controller().isExpanded({ 0u }));
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 0u }))
        << "the first Left Arrow only collapses - it doesn't also move";

    // {0} is a top-level path (size 1) - no parent to move to.
    PressTreeKey(treeView, vkLeftArrow, 0);
    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 0u }));

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, ShiftArrowExtendsSelectionAcrossVisibleRows) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));
    treeView->controller().setExpanded({ 0u }, true);  // visible: {0}, {0,0}, {0,1}, {1}

    ClickTreeRow(treeView, 0, 50.0f, 0);  // anchor at visible row 0 ({0})

    PressTreeKey(treeView, vkDownArrow, kmShift);
    EXPECT_EQ(treeView->selectedPaths(), (std::set<std::vector<std::size_t>>{ { 0u }, { 0u, 0u } }));

    PressTreeKey(treeView, vkDownArrow, kmShift);
    EXPECT_EQ(treeView->selectedPaths(), (std::set<std::vector<std::size_t>>{ { 0u }, { 0u, 0u }, { 0u, 1u } }));

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, CtrlArrowMovesTheHighlightWithoutChangingSelection) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));  // collapsed: visible {0}, {1}

    treeView->setSelectedPath(std::vector<std::size_t>{ 0u });
    PressTreeKey(treeView, vkDownArrow, kmCtrl);

    EXPECT_EQ(*treeView->selectedPath(), (std::vector<std::size_t>{ 0u }))
        << "Ctrl+Arrow must not touch real selection";
    ASSERT_TRUE(treeView->keyboardHighlightedIndex().has_value());
    EXPECT_EQ(*treeView->keyboardHighlightedIndex(), 1u);

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, CtrlSpaceTogglesSelectionAtTheHighlightedRow) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

    treeView->setSelectedPath(std::vector<std::size_t>{ 0u });
    PressTreeKey(treeView, vkDownArrow, kmCtrl);  // highlight -> {1}, selection still just {0}
    PressTreeKey(treeView, vkSpaceBar, kmCtrl);

    EXPECT_EQ(treeView->selectedPaths(), (std::set<std::vector<std::size_t>>{ { 0u }, { 1u } }));

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, ArrowKeysAreANoOpWithNoRows) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));

    PressTreeKey(treeView, vkDownArrow, 0);  // must not crash with visibleCount() == 0

    EXPECT_FALSE(treeView->selectedPath().has_value());

    treeView->destroy();
    delete treeView;
}

TEST(TreeView, HoverHighlightIsEnabledByDefaultAndTracksMouseMove) {
    auto* treeView = new TreeView();
    treeView->setBounds(Rect(0, 0, 100, 200));
    EXPECT_TRUE(treeView->hoverHighlightEnabled());

    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));
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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));

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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));
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
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    treeView->setModel(std::move(modelOwner));
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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));
    EXPECT_EQ(dropDown->model(), &model);

    dropDown->setSelectedIndex(2u);
    ASSERT_TRUE(dropDown->selectedIndex().has_value());

    // Swapping in a smaller model whose size() no longer covers index 2
    // clears the now-invalid selection - same reasoning
    // TreeController::setModel() already has for expandedPaths_ referring
    // to a path that no longer exists.
    auto smallerModelOwner = std::make_unique<StubRowModel>();
    StubRowModel& smallerModel = *smallerModelOwner;
    smallerModel.rows = { "only one" };
    dropDown->setModel(std::move(smallerModelOwner));
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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));

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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));

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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));

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

    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));
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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));

    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkDownArrow);

    ASSERT_TRUE(dropDown->selectedIndex().has_value());
    EXPECT_EQ(*dropDown->selectedIndex(), 0u);

    dropDown->destroy();
    delete dropDown;
}

TEST(DropDownList, DownArrowAdvancesAndClampsAtTheLastItem) {
    auto* dropDown = new DropDownList();
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));
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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));
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
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b", "c" };
    dropDown->setModel(std::move(modelOwner));
    dropDown->setEnabled(false);

    dropDown->onKeyDown(*dropDown, 0, 0, 0, vkDownArrow);
    EXPECT_FALSE(dropDown->selectedIndex().has_value());

    dropDown->destroy();
    delete dropDown;
}

// ---- Label: LabelStyle::textColor manual override ----

namespace {
newui::LabelStyle& labelStyleOf(newui::Label& label) {
    return dynamic_cast<newui::LabelStyle&>(label.style());
}
}

TEST(Label, StateChangeDrivesTextColorWhenNothingOverridesIt) {
    auto* label = new newui::Label();
    newui::Color normal = labelStyleOf(*label).textColor();

    label->setEnabled(false);
    EXPECT_NE(labelStyleOf(*label).textColor(), normal);  // dimmed to DisabledText

    label->setEnabled(true);
    EXPECT_EQ(labelStyleOf(*label).textColor(), normal);

    label->destroy();
    delete label;
}

TEST(Label, ExternallySetStyleTextColorSurvivesLaterStateChanges) {
    auto* label = new newui::Label();
    newui::Color custom(1.0f, 0.0f, 0.5f, 1.0f);
    labelStyleOf(*label).setTextColor(custom);  // e.g. edited through the Properties grid

    label->setEnabled(false);
    label->setEnabled(true);
    label->setHotLink(true);

    EXPECT_EQ(labelStyleOf(*label).textColor(), custom);

    label->destroy();
    delete label;
}

TEST(Label, ClearingTheOverrideToNullResumesStateDrivenColor) {
    auto* label = new newui::Label();
    newui::Color normal = labelStyleOf(*label).textColor();
    labelStyleOf(*label).setTextColor(newui::Color(1.0f, 0.0f, 0.5f, 1.0f));

    labelStyleOf(*label).setTextColor(newui::Color::null());
    label->setEnabled(false);
    label->setEnabled(true);

    EXPECT_EQ(labelStyleOf(*label).textColor(), normal);

    label->destroy();
    delete label;
}

TEST(Label, ExplicitSetTextColorAlwaysWinsOverAnOverride) {
    auto* label = new newui::Label();
    labelStyleOf(*label).setTextColor(newui::Color(1.0f, 0.0f, 0.5f, 1.0f));

    label->setTextColor(BLRgba32(0xFF112233u));

    EXPECT_EQ(labelStyleOf(*label).textColor(), newui::Color(BLRgba32(0xFF112233u)));

    label->destroy();
    delete label;
}

// ---- Shared controllers (ListView / TreeView / DropDownList) ----
// One controller can back several views over the same data; a view that dies must not leave the
// controller holding a dangling onDataChanged subscriber.

TEST(ListView, ASharedControllerOutlivingAViewLeavesNoDanglingSubscriber) {
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a", "b" };
    auto controller = std::make_shared<ListController>();
    controller->setModel(std::move(modelOwner));

    auto* first = new ListView();
    first->setController(controller);
    auto* second = new ListView();
    second->setController(controller);
    EXPECT_EQ(first->sharedController(), second->sharedController());

    first->destroy();
    delete first;
    model.onChanged(model);  // controller fires onDataChanged; only `second` may still hear it

    second->destroy();
    delete second;
    model.onChanged(model);  // and nobody at all now
    SUCCEED();
}

TEST(ListView, SwappingControllersUnsubscribesFromTheOldOne) {
    auto modelOwner = std::make_unique<StubRowModel>();
    StubRowModel& model = *modelOwner;
    model.rows = { "a" };
    auto oldController = std::make_shared<ListController>();
    oldController->setModel(std::move(modelOwner));

    auto* view = new ListView();
    view->setController(oldController);
    view->setController(std::make_shared<ListController>());

    view->destroy();
    delete view;
    model.onChanged(model);  // oldController must not call into the deleted view
    SUCCEED();
}

TEST(TreeView, ASharedControllerOutlivingAViewLeavesNoDanglingSubscriber) {
    auto modelOwner = std::make_unique<StubTreeRowModel>();
    StubTreeRowModel& model = *modelOwner;
    auto controller = std::make_shared<TreeController>();
    controller->setModel(std::move(modelOwner));

    auto* view = new TreeView();
    view->setController(controller);
    EXPECT_EQ(view->sharedController(), controller);

    view->destroy();
    delete view;
    model.onChanged(model);
    SUCCEED();
}

TEST(DropDownList, SetControllerSharesItAndClampsAnOutOfRangeSelection) {
    auto threeRowsOwner = std::make_unique<StubRowModel>();
    StubRowModel& threeRows = *threeRowsOwner;
    threeRows.rows = { "a", "b", "c" };
    auto oneRowOwner = std::make_unique<StubRowModel>();
    StubRowModel& oneRow = *oneRowOwner;
    oneRow.rows = { "only" };

    auto* dropDown = new DropDownList();
    dropDown->setModel(std::move(threeRowsOwner));
    dropDown->setSelectedIndex(2);

    auto controller = std::make_shared<ListController>();
    controller->setModel(std::move(oneRowOwner));
    dropDown->setController(controller);

    EXPECT_EQ(dropDown->sharedController(), controller);
    EXPECT_EQ(&dropDown->controller(), controller.get());
    EXPECT_EQ(dropDown->model(), &oneRow);
    EXPECT_FALSE(dropDown->selectedIndex().has_value());  // index 2 no longer exists

    dropDown->destroy();
    delete dropDown;
}

TEST(Slider, ItsThumbIsAnInternalNonSelectablePart) {
    auto* slider = new Slider();
    ASSERT_NE(slider->thumb(), nullptr);
    EXPECT_TRUE(slider->thumb()->isInternal());
    EXPECT_FALSE(slider->thumb()->isSelectableAtDesignTime());
    EXPECT_FALSE(slider->isInternal());
    EXPECT_TRUE(slider->isSelectableAtDesignTime());
    slider->destroy();
    delete slider;
}

TEST(Progress, ItsFillIsAnInternalNonSelectablePart) {
    auto* progress = new Progress();
    ASSERT_NE(progress->fill(), nullptr);
    EXPECT_TRUE(progress->fill()->isInternal());
    EXPECT_FALSE(progress->fill()->isSelectableAtDesignTime());
    progress->destroy();
    delete progress;
}

TEST(Slider, ItsTickMarksViewIsAnInternalNonSelectablePart) {
    auto* slider = new Slider();
    slider->setShowTicks(true);
    ASSERT_NE(slider->ticks(), nullptr);
    EXPECT_TRUE(slider->ticks()->isInternal());
    EXPECT_FALSE(slider->ticks()->isSelectableAtDesignTime());
    slider->destroy();
    delete slider;
}

TEST(ScrollView, ItsScrollBarsAreInternalNonSelectableButItsViewportIsNot) {
    auto* scroll = new ScrollView();
    ASSERT_NE(scroll->vBar(), nullptr);
    ASSERT_NE(scroll->hBar(), nullptr);
    EXPECT_TRUE(scroll->vBar()->isInternal());
    EXPECT_TRUE(scroll->hBar()->isInternal());
    EXPECT_FALSE(scroll->vBar()->isSelectableAtDesignTime());
    EXPECT_FALSE(scroll->hBar()->isSelectableAtDesignTime());
    EXPECT_FALSE(scroll->isInternal());
    scroll->destroy();
    delete scroll;
}

namespace {
    // Pixels in a 300x40 render of textControl that are clearly red (text drawn in the red run).
    std::size_t redTextPixels(TextControl& textControl) {
        BLImage image(300, 40, BL_FORMAT_PRGB32);
        {
            BLContext ctx(image);
            ctx.clear_all();
            textControl.paint(ctx);
            ctx.end();
        }
        BLImageData data{};
        image.get_data(&data);
        std::size_t count = 0;
        for (int y = 0; y < 40; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride);
            for (int x = 0; x < 300; ++x) {
                const std::uint32_t px = row[x];
                const std::uint32_t r = (px >> 16) & 0xFF, g = (px >> 8) & 0xFF, b = px & 0xFF;
                count += (r > 120 && g < 60 && b < 60) ? 1 : 0;
            }
        }
        return count;
    }
}

// Color runs (e.g. syntax highlighting) recolor ranges of the text; plain black text has no red.
TEST(TextControl, ColorRunsRecolorTheirRangeOfTheText) {
    TextControl textControl;
    textControl.setBounds(Rect(0, 0, 300, 40));
    textControl.setText(L"WWWWWW plain");
    textControl.setTextColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
    ASSERT_EQ(redTextPixels(textControl), 0u);

    textControl.setColorRuns({ text::TextColorRun{ 0, 6, Color(1.0f, 0.0f, 0.0f, 1.0f) } });

    EXPECT_GT(redTextPixels(textControl), 20u);
}

TEST(TextControl, ColorRunsPastTheEndOfTheTextAreIgnored) {
    TextControl textControl;
    textControl.setBounds(Rect(0, 0, 300, 40));
    textControl.setText(L"abc");
    textControl.setColorRuns({ text::TextColorRun{ 2, 50, Color(1.0f, 0.0f, 0.0f, 1.0f) },
                               text::TextColorRun{ 10, 5, Color(0.0f, 1.0f, 0.0f, 1.0f) } });

    EXPECT_GT(redTextPixels(textControl), 0u) << "the in-range part is clamped, not dropped";
}

namespace {
    // Pixels of a 300x40 render of textControl that are clearly the given primary color.
    std::size_t colorPixels(TextControl& textControl, int channel) {
        BLImage image(300, 40, BL_FORMAT_PRGB32);
        {
            BLContext ctx(image);
            ctx.clear_all();
            textControl.paint(ctx);
            ctx.end();
        }
        BLImageData data{};
        image.get_data(&data);
        std::size_t count = 0;
        for (int y = 0; y < 40; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride);
            for (int x = 0; x < 300; ++x) {
                const std::uint32_t px = row[x];
                const int v[3] = { int((px >> 16) & 0xFF), int((px >> 8) & 0xFF), int(px & 0xFF) };
                const int a = int((px >> 24) & 0xFF);
                bool dominant = a > 100 && v[channel] > 150;
                for (int other = 0; other < 3; ++other) {
                    dominant = dominant && (other == channel || v[other] < 90);
                }
                count += dominant ? 1 : 0;
            }
        }
        return count;
    }

    TextControl& decoratedText(TextControl& textControl) {
        textControl.setBounds(Rect(0, 0, 300, 40));
        textControl.setText(L"alpha beta gamma");
        textControl.setTextColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
        return textControl;
    }
}

TEST(TextControlDecorations, NoneMeansNothingColoredIsDrawn) {
    TextControl textControl;
    decoratedText(textControl);
    for (int channel = 0; channel < 3; ++channel) {
        EXPECT_EQ(colorPixels(textControl, channel), 0u) << channel;
    }
}

TEST(TextControlDecorations, EachKindDrawsInItsColor) {
    using Kind = text::TextDecorationKind;
    const struct { Kind kind; int channel; } cases[] = {
        { Kind::Squiggle, 0 }, { Kind::Underline, 0 }, { Kind::Box, 2 }, { Kind::RoundBox, 2 }, { Kind::Background, 1 },
    };
    for (const auto& testCase : cases) {
        TextControl textControl;
        decoratedText(textControl);
        text::TextDecoration decoration;
        decoration.start = 6;   // "beta"
        decoration.length = 4;
        decoration.kind = testCase.kind;
        decoration.color = Color(testCase.channel == 0 ? 1.0f : 0.0f, testCase.channel == 1 ? 1.0f : 0.0f,
            testCase.channel == 2 ? 1.0f : 0.0f, 1.0f);
        textControl.setDecorations({ decoration });
        EXPECT_GT(colorPixels(textControl, testCase.channel), 5u) << "kind " << int(testCase.kind);
    }
}

TEST(TextControlDecorations, ARangePastTheEndIsClampedNotDropped) {
    TextControl textControl;
    decoratedText(textControl);
    text::TextDecoration decoration;
    decoration.start = 11;   // "gamma" and far beyond
    decoration.length = 500;
    decoration.kind = text::TextDecorationKind::Squiggle;
    decoration.color = Color(1.0f, 0.0f, 0.0f, 1.0f);
    textControl.setDecorations({ decoration });
    EXPECT_GT(colorPixels(textControl, 0), 5u);
}

namespace {
    struct InkScan {
        int rightmostInk = -1;      // rightmost column with dark text pixels
        std::size_t ink = 0;        // dark pixels
        std::vector<std::uint32_t> pixels;
    };

    InkScan scanInk(TextControl& textControl, int width, int height) {
        BLImage image(width, height, BL_FORMAT_PRGB32);
        {
            BLContext ctx(image);
            ctx.fill_all(BLRgba32(0xFFFFFFFF));
            textControl.paint(ctx);
            ctx.end();
        }
        BLImageData data{};
        image.get_data(&data);
        InkScan scan;
        for (int y = 0; y < height; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride);
            for (int x = 0; x < width; ++x) {
                scan.pixels.push_back(row[x]);
                const bool dark = ((row[x] >> 8) & 0xFF) < 128;
                if (dark) {
                    scan.rightmostInk = x > scan.rightmostInk ? x : scan.rightmostInk;
                    ++scan.ink;
                }
            }
        }
        return scan;
    }

    TextControl& styledText(TextControl& textControl, const std::wstring& text) {
        textControl.setBounds(Rect(0, 0, 400, 40));
        textControl.setFont(Font("Segoe UI", 16.0f));
        textControl.setText(text);
        textControl.setTextColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
        return textControl;
    }
}

TEST(TextControlFontRuns, BoldWidensTheTextAndMovesTheCaretPositionsAfterIt) {
    TextControl plain;
    styledText(plain, L"mmmmmm x");
    plain.controller().ensureLayoutUpToDate();
    Point plainX;
    float height = 0.0f;
    plain.controller().layoutEngine().hitTestPosition(text::TextPosition(7), plainX, height);

    TextControl bold;
    styledText(bold, L"mmmmmm x");
    text::TextFontRun run;
    run.start = 0;
    run.length = 6;
    run.bold = true;
    bold.setFontRuns({ run });
    bold.controller().ensureLayoutUpToDate();
    Point boldX;
    bold.controller().layoutEngine().hitTestPosition(text::TextPosition(7), boldX, height);

    EXPECT_GT(boldX.x, plainX.x + 1.0f);
}

// The point of one shared layout: what's drawn is what's hit-tested.
TEST(TextControlFontRuns, TheDrawnBoldTextEndsWhereItsHitTestRectEnds) {
    TextControl textControl;
    styledText(textControl, L"mmmmmmmm");
    text::TextFontRun run;
    run.start = 0;
    run.length = 8;
    run.bold = true;
    textControl.setFontRuns({ run });

    const InkScan scan = scanInk(textControl, 400, 40);
    const std::vector<Rect> rects = textControl.controller().layoutEngine().hitTestRange(text::TextRange(0, 8));
    ASSERT_FALSE(rects.empty());
    ASSERT_GE(scan.rightmostInk, 0);
    EXPECT_LE(scan.rightmostInk, rects.back().right() + 1.0f);
    EXPECT_GE(scan.rightmostInk, rects.back().right() - 4.0f);
}

TEST(TextControlFontRuns, UnderlineAddsInkBelowTheText) {
    TextControl plain;
    styledText(plain, L"abc abc abc");
    TextControl underlined;
    styledText(underlined, L"abc abc abc");
    text::TextFontRun run;
    run.start = 0;
    run.length = 11;
    run.underline = true;
    underlined.setFontRuns({ run });

    // A line under ~80px of text.
    EXPECT_GT(scanInk(underlined, 400, 40).ink, scanInk(plain, 400, 40).ink + 40);
}

TEST(TextControlFontRuns, ItalicChangesTheDrawnGlyphs) {
    TextControl plain;
    styledText(plain, L"lllllll");
    TextControl italic;
    styledText(italic, L"lllllll");
    text::TextFontRun run;
    run.start = 0;
    run.length = 7;
    run.italic = true;
    italic.setFontRuns({ run });

    EXPECT_NE(scanInk(italic, 400, 40).pixels, scanInk(plain, 400, 40).pixels);
}

#include "newui/reflectionio.h"
#include "newui/textstyle.h"

namespace {
    std::shared_ptr<TextStyleSheet> sampleSheet(const Color& keywordColor) {
        auto sheet = std::make_shared<TextStyleSheet>();
        auto* keyword = new TextStyle("keyword");
        keyword->setBold(true);
        keyword->setColor(keywordColor);
        sheet->addStyle(keyword);
        auto* comment = new TextStyle("comment");
        comment->setItalic(true);
        comment->setBackgroundColor(Color(1.0f, 1.0f, 0.0f, 1.0f));
        sheet->addStyle(comment);
        auto* error = new TextStyle("error");
        error->setDecoration(text::TextDecorationKind::Squiggle);
        error->setDecorationColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
        sheet->addStyle(error);
        return sheet;
    }
}

TEST(TextStyles, ExpandTurnsEachStyleIntoRunsAndDecorations) {
    auto sheet = sampleSheet(Color(0.0f, 0.0f, 1.0f, 1.0f));
    text::ExpandedTextStyles out = text::expandTextStyles(*sheet, {
        { 0, 4, "keyword" }, { 5, 6, "comment" }, { 12, 3, "error" }, { 16, 2, "noSuchStyle" } });

    ASSERT_EQ(out.colorRuns.size(), 1u) << "only keyword sets a color";
    EXPECT_EQ(out.colorRuns[0].start, 0u);
    EXPECT_EQ(out.colorRuns[0].color, Color(0.0f, 0.0f, 1.0f, 1.0f));

    ASSERT_EQ(out.fontRuns.size(), 2u);
    EXPECT_TRUE(out.fontRuns[0].bold);
    EXPECT_TRUE(out.fontRuns[1].italic);

    ASSERT_EQ(out.decorations.size(), 2u);
    EXPECT_EQ(out.decorations[0].kind, text::TextDecorationKind::Background);
    EXPECT_EQ(out.decorations[0].start, 5u);
    EXPECT_EQ(out.decorations[1].kind, text::TextDecorationKind::Squiggle);
    EXPECT_EQ(out.decorations[1].start, 12u);
}

TEST(TextStyles, ATextControlStylesItsRangesAndRestylesWhenTheSheetChanges) {
    TextControl textControl;
    textControl.setBounds(Rect(0, 0, 300, 40));
    textControl.setText(L"auto x = 1; // note");
    textControl.setStyledRanges({ { 0, 4, "keyword" }, { 12, 7, "comment" } });
    EXPECT_TRUE(textControl.colorRuns().empty()) << "nothing to resolve names against yet";

    textControl.setStyleSheet(sampleSheet(Color(0.0f, 0.0f, 1.0f, 1.0f)));
    ASSERT_EQ(textControl.colorRuns().size(), 1u);
    EXPECT_EQ(textControl.fontRuns().size(), 2u);
    EXPECT_EQ(textControl.decorations().size(), 1u);

    textControl.setStyleSheet(sampleSheet(Color(0.0f, 0.5f, 0.0f, 1.0f)));   // a different theme
    ASSERT_EQ(textControl.colorRuns().size(), 1u);
    EXPECT_EQ(textControl.colorRuns()[0].color, Color(0.0f, 0.5f, 0.0f, 1.0f));
}

TEST(TextStyles, AStyleFontSizeChangesTheLayout) {
    auto sheet = std::make_shared<TextStyleSheet>();
    auto* big = new TextStyle("big");
    big->setFontSize(36.0f);
    sheet->addStyle(big);

    TextControl plain;
    plain.setBounds(Rect(0, 0, 400, 80));
    plain.setText(L"small BIG");
    plain.controller().ensureLayoutUpToDate();
    const std::vector<Rect> plainRects = plain.controller().layoutEngine().hitTestRange(text::TextRange(6, 3));

    TextControl styled;
    styled.setBounds(Rect(0, 0, 400, 80));
    styled.setText(L"small BIG");
    styled.setStyleSheet(sheet);
    styled.setStyledRanges({ { 6, 3, "big" } });
    styled.controller().ensureLayoutUpToDate();
    const std::vector<Rect> bigRects = styled.controller().layoutEngine().hitTestRange(text::TextRange(6, 3));

    ASSERT_FALSE(plainRects.empty());
    ASSERT_FALSE(bigRects.empty());
    EXPECT_GT(bigRects[0].width(), plainRects[0].width() * 1.5f);
}

TEST(TextStyles, AStyleSheetSurvivesSaveAndLoad) {
    auto sheet = sampleSheet(Color(0.0f, 0.0f, 1.0f, 1.0f));
    sheet->setName("lightTheme");
    reflection::ObjectWriter writer;
    writer.write(sheet.get());

    reflection::ObjectReader reader;
    ASSERT_FALSE(json5::from_string(json5::to_string(writer.doc), reader.doc));
    std::unique_ptr<TextStyleSheet> loaded(reader.readNew<TextStyleSheet>());
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->name(), "lightTheme");
    ASSERT_EQ(loaded->styles().size(), 3u);
    TextStyle* keyword = loaded->style("keyword");
    ASSERT_NE(keyword, nullptr);
    EXPECT_TRUE(keyword->isBold());
    EXPECT_EQ(keyword->color(), Color(0.0f, 0.0f, 1.0f, 1.0f));
    TextStyle* error = loaded->style("error");
    ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->decoration(), text::TextDecorationKind::Squiggle);
    EXPECT_TRUE(loaded->style("comment")->isItalic());
}

namespace {
    std::vector<std::uint32_t> renderField(TextField& field) {
        BLImage image(200, 30, BL_FORMAT_PRGB32);
        {
            BLContext ctx(image);
            ctx.fill_all(BLRgba32(0xFFFFFFFF));
            field.paint(ctx);
            ctx.end();
        }
        BLImageData data{};
        image.get_data(&data);
        std::vector<std::uint32_t> pixels;
        for (int y = 0; y < 30; ++y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(static_cast<const std::uint8_t*>(data.pixel_data) + y * data.stride);
            pixels.insert(pixels.end(), row, row + 200);
        }
        return pixels;
    }

    std::size_t inkIn(const std::vector<std::uint32_t>& pixels) {
        std::size_t count = 0;
        for (std::uint32_t px : pixels) {
            count += ((px >> 8) & 0xFF) < 220 ? 1 : 0;
        }
        return count;
    }
}

TEST(TextFieldPlaceholder, ShowsWhileEmptyAndIsNeverPartOfTheText) {
    TextField field;
    field.setBounds(Rect(0, 0, 200, 30));
    field.controller().caret().stop();
    const std::size_t blank = inkIn(renderField(field));

    field.setPlaceholder("Search");
    const std::vector<std::uint32_t> withPlaceholder = renderField(field);

    EXPECT_GT(inkIn(withPlaceholder), blank + 20) << "the placeholder is drawn";
    EXPECT_TRUE(field.text().empty());
}

TEST(TextFieldPlaceholder, TextReplacesItAndClearingBringsItBack) {
    TextField field;
    field.setBounds(Rect(0, 0, 200, 30));
    field.setPlaceholder("Search");
    const std::vector<std::uint32_t> empty = renderField(field);

    field.setText(L"abc");
    EXPECT_NE(renderField(field), empty);
    EXPECT_EQ(field.text(), L"abc");

    field.setText(L"");
    EXPECT_EQ(renderField(field), empty);
}

TEST(TextFieldPlaceholder, IsAReflectedProperty) {
    const reflection::Class* cls = reflection::classinfo(typeid(TextField));
    ASSERT_NE(cls, nullptr);
    std::vector<const reflection::Property*> properties;
    cls->allProperties(properties);
    bool found = false;
    for (const reflection::Property* property : properties) {
        found = found || property->name() == "placeholder";
    }
    EXPECT_TRUE(found);
}

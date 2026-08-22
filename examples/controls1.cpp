// A tour of newui::Control-derived widgets (controls.h) - starts with
// Progress, the first fleshed-out concrete Control (TextControl is still
// an empty stub; TextField has real text-editing state - see text.h -
// but no painting/input wired up yet). Six rows, each a small horizontal
// FlexLayout pairing a text label (LabelStyle) with one Progress bar
// demonstrating a different facet:
//   1. A fixed 25% horizontal bar - the baseline.
//   2. A fixed 75% horizontal bar.
//   3. Full (100%), fillState = Error - the red PBFS_ERROR look.
//   4. 60%, fillState = Paused - the yellow PBFS_PAUSED look.
//   5. A vertical bar at 70% - fills from the bottom up (see
//      Progress::updateFillBounds()'s doc comment in controls.cpp).
//   6. A live bar whose value() counts up and wraps, driven by a plain
//      RunLoop::postIdle() task registered on the app's own run loop
//      before Application::run() starts pumping - the same integration
//      point AnimationManager::addToRunLoop() uses in animation1.cpp's
//      Demo 6, just a plain lambda here instead of a real Animation:
//      Progress::value_ is private (nothing outside the class could bind
//      PropertyManager::registerProperty() to it directly), and calling
//      the public setValue() repeatedly is the natural fit for a single
//      scalar like this anyway.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <iostream>
#include <memory>
#include <string>

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    return newui::SyncReturn::Handled;
}

newui::SubView* MakeLabel(const std::string& text) {
    auto* label = new newui::SubView();
    label->setVisible(true);
    auto style = std::make_unique<newui::LabelStyle>();
    style->text = text;
    // UIColorManager::colorFor(), not a hardcoded literal - matches
    // root's own backgroundFill below (see main()), which follows the
    // same reasoning: this toolkit's native controls (Progress/Button)
    // already darken themselves to track Windows' OS-level Light/Dark
    // mode setting (ThemedViewStyle::paint(), viewstyle.cpp), so this
    // demo's own app-drawn colors need to track the same setting via
    // UIColorManager::colorFor() or they drift out of sync with it -
    // concretely, hardcoded black text landing on an unexpectedly
    // dark-inverted native control face.
    style->textColor = newui::UIColorManager::colorFor(newui::UIColorRole::WindowText).toBLRgba32();
    // Without an opaque fill of its own, this label's glyphs would get
    // alpha-blended directly onto whatever was already there on every
    // repaint (root's own background fill is scoped to dirtyRect_, but
    // paintChildren() still redraws every child unconditionally
    // regardless of whether its own area was actually included - see
    // RootView::repaint()'s comment), which isn't idempotent for
    // translucent AA glyph edges - repeated re-blending darkens/thickens
    // them instead of reproducing the same pixels. An opaque
    // backgroundFill re-establishes a clean backdrop before each redraw,
    // same as every other control in this demo already has via its own
    // track/fill chrome.
    style->setBackgroundColor( newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground) );
    label->setStyle(std::move(style));
    label->setDesiredSize(newui::Size(150.0f, 24.0f));
    return label;
}

// One row: a fixed-width label on the left, one Progress bar filling the
// rest. Returns the Progress bar itself so the caller can keep
// configuring it (setValue()/setFillState()/... - see main()).
newui::Progress* AddProgressRow(newui::View* parent, const std::string& labelText) {
    auto* row = new newui::SubView();
    row->setVisible(true);
    row->setDesiredSize(newui::Size(0.0f, 24.0f));
    auto rowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    rowLayout->setSpacing(12.0f);
    row->setLayout(std::move(rowLayout));
    parent->addChild(row);

    row->addChild(MakeLabel(labelText));

    auto* progress = new newui::Progress();
    progress->setVisible(true);
    progress->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    row->addChild(progress);

    return progress;
}

int main() {
    std::cout << "newui " << newui::version() << " - controls example\n";
    std::cout << "Progress: fixed values, Error/Paused fill states, a vertical bar,\n";
    std::cout << "and a live bar counting up and wrapping via RunLoop idle time.\n";

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("controls1");
    app.setFrame(&frame);

    frame.setTitle("Controls Example");
    // Tall enough for every row below (Progress x4, a weighted vertical
    // bar, buttons, the new toolbar, labels, toggles, sliders, and a
    // 100px image) to actually fit within RootView's own client area
    // (Frame::setBounds() sizes the *outer* window - see
    // Frame::updateViewBounds(), which derives RootView's real bounds
    // from GetClientRect() instead) - too short here left the lower rows
    // laid out past the visible client height with nothing to scroll to
    // them, which is why the toolbar (added partway down the stack)
    // wasn't visible before this was widened.
    frame.setBounds(newui::Rect(10, 10, 520, 720));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.getView();
    // UIColorManager::colorFor(), not a hardcoded literal - see
    // MakeLabel()'s own comment above for why: this toolkit's native
    // controls already track Windows' OS-level Light/Dark mode setting on
    // their own, so this demo's own app-drawn background needs to track
    // the same setting or the two drift out of sync (confirmed live: a
    // hardcoded white background here, with the OS in Dark mode, produced
    // an unreadable near-black-on-near-black Button - its native chrome
    // correctly went dark to match the OS, its text color didn't).
    root.style().setBackgroundColor( newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground) );

    /*
    root.onMouseMove += [](newui::View&, const newui::Point& pt, std::uint32_t, std::uint32_t) -> newui::SyncReturn { 
        
        printf("onMouseMove %d, %d\n", (int)pt.x, (int)pt.y);

        return newui::SyncReturn::Handled; 
        };
        */


    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(10.0f);
    rootLayout->setPadding(16.0f);
    root.setLayout(std::move(rootLayout));

    // Toolbar - a horizontal FlexLayout strip (Toolbar, controls.h) of
    // ToolbarButtons plus a ToolbarSeparator between the two groups,
    // docked at the very top like a real app's toolbar (root's own
    // FlexLayout just stacks children in addChild() order, so "first
    // child added" is what makes this the top row - no special docking
    // concept needed). "New"/"Open" are momentary (fire onClick() once
    // per completed click, same touchUpInside semantics as clickButton
    // below); "Bold"/"Italic" are toggle buttons (setToggleButton(true))
    // that stay visually pressed while isChecked() - the toolbar-
    // formatting-button use case ThemedToolbarButtonStyle's own class
    // comment (viewstyle.h) describes.
    auto* toolbar = new newui::Toolbar();
    root.addChild(toolbar);

    auto* newButton = new newui::ToolbarButton();
    newButton->setText("New");
    newButton->setDesiredSize(newui::Size(50.0f, 24.0f));
    newButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [](newui::Control&) -> newui::SyncReturn {
            printf("Toolbar: New clicked\n");
            return newui::SyncReturn::Handled;
        }));
    toolbar->addChild(newButton);

    auto* openButton = new newui::ToolbarButton();
    openButton->setText("Open");
    openButton->setDesiredSize(newui::Size(50.0f, 24.0f));
    openButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [](newui::Control&) -> newui::SyncReturn {
            printf("Toolbar: Open clicked\n");
            return newui::SyncReturn::Handled;
        }));
    toolbar->addChild(openButton);

    toolbar->addChild(new newui::ToolbarSeparator());

    auto* boldButton = new newui::ToolbarButton();
    boldButton->setText("Bold");
    boldButton->setToggleButton(true);
    boldButton->setDesiredSize(newui::Size(50.0f, 24.0f));
    boldButton->onCheckedChanged.add(std::function<newui::SyncReturn(newui::ToolbarButton&)>(
        [](newui::ToolbarButton& sender) -> newui::SyncReturn {
            printf("Toolbar: Bold is now %s\n", sender.isChecked() ? "ON" : "off");
            return newui::SyncReturn::Handled;
        }));
    toolbar->addChild(boldButton);

    auto* italicButton = new newui::ToolbarButton();
    italicButton->setText("Italic");
    italicButton->setToggleButton(true);
    italicButton->setDesiredSize(newui::Size(50.0f, 24.0f));
    italicButton->onCheckedChanged.add(std::function<newui::SyncReturn(newui::ToolbarButton&)>(
        [](newui::ToolbarButton& sender) -> newui::SyncReturn {
            printf("Toolbar: Italic is now %s\n", sender.isChecked() ? "ON" : "off");
            return newui::SyncReturn::Handled;
        }));
    toolbar->addChild(italicButton);

    AddProgressRow(&root, "25%")->setValue(0.25f);
    AddProgressRow(&root, "75%")->setValue(0.75f);

    auto* errorBar = AddProgressRow(&root, "Error state");
    errorBar->setValue(1.0f);
    errorBar->setFillState(newui::ThemedProgressBarFillStyle::FillState::Error);

    auto* pausedBar = AddProgressRow(&root, "Paused state");
    pausedBar->setValue(0.6f);
    pausedBar->setFillState(newui::ThemedProgressBarFillStyle::FillState::Paused);

    // Vertical bar - its own row stacks a label above a tall, narrow
    // Progress instead of pairing them side by side like the rows above.
    auto* verticalRow = new newui::SubView();
    verticalRow->setVisible(true);
    verticalRow->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    auto verticalRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    verticalRowLayout->setSpacing(6.0f);
    verticalRow->setLayout(std::move(verticalRowLayout));
    root.addChild(verticalRow);

    verticalRow->addChild(MakeLabel("Vertical, 70%"));

    auto* verticalBar = new newui::Progress();
    verticalBar->setVisible(true);
    verticalBar->setHorizontal(false);
    verticalBar->setValue(0.7f);
    verticalBar->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    verticalRow->addChild(verticalBar);

    // Live bar - counts up over a couple of seconds then wraps back to 0,
    // driven by a plain RunLoop::postIdle() task rather than a real
    // Animation (see this file's header comment for why). Hand-throttled
    // to roughly 30 steps/second via std::chrono - postIdle() itself runs
    // once per idle pass with no built-in rate limiting (unlike
    // AnimationManager, which paces itself against frameRate()), so an
    // unthrottled increment here would blow past 100% almost instantly.
    // Every step calls Progress::setValue(), which now correctly
    // style().markDirty()s - and RootView::markDirty() always repaints
    // the *entire* window buffer (there's no dirty-rect scoping in this
    // paint architecture; see RootView::notifyRedrawNeeded()), so this
    // rate directly sets how often the whole demo window gets
    // re-rasterized. 30fps is plenty smooth for a progress bar and roughly
    // halves that repaint cost versus 60fps.


    
    auto* liveBar = AddProgressRow(&root, "Live (counts up)");

    /*
    app.runLoop().postIdle([liveBar, lastTick = std::chrono::steady_clock::now()]() mutable {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastTick).count() < 33) {
            return false;
        }
        lastTick = now;

        float next = liveBar->value() + 0.01f;
        liveBar->setValue(next >= 1.0f ? 0.0f : next);
        return false;  // never "done" - keeps running for the app's life
    });
    */

    // Buttons - one momentary (fires onClick() on each full down-then-
    // up-inside gesture, the same UIControl-style touchUpInside semantics
    // Progress's own Control base already provides) and one toggle
    // (stays visually pressed once isChecked(), flips back off on the
    // next click) - see Button::isToggleButton()'s doc comment.
    auto* buttonRow = new newui::SubView();
    buttonRow->setVisible(true);
    buttonRow->setDesiredSize(newui::Size(0.0f, 28.0f));
    auto buttonRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    buttonRowLayout->setSpacing(12.0f);
    buttonRow->setLayout(std::move(buttonRowLayout));
    root.addChild(buttonRow);

    auto* clickButton = new newui::Button();
    clickButton->setText("Click Me");
    clickButton->setDesiredSize(newui::Size(100.0f, 28.0f));
    clickButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [](newui::Control&) -> newui::SyncReturn {
            static int clickCount = 0;
            ++clickCount;
            printf("Click Me pressed (%d times)\n", clickCount);
            return newui::SyncReturn::Handled;
        }));
    buttonRow->addChild(clickButton);

    auto* toggleButton = new newui::Button();
    toggleButton->setText("Toggle");
    toggleButton->setToggleButton(true);
    toggleButton->setDesiredSize(newui::Size(100.0f, 28.0f));
    toggleButton->onCheckedChanged.add(std::function<newui::SyncReturn(newui::Button&)>(
        [](newui::Button& sender) -> newui::SyncReturn {
            printf("Toggle button is now %s\n", sender.isChecked() ? "ON" : "off");
            return newui::SyncReturn::Handled;
        }));
    buttonRow->addChild(toggleButton);

    // Labels - both hot-linked (Label::setHotLink(true), recolors on
    // hover and fires onLinkClicked() on a completed click), one enabled
    // and one disabled, side by side so both looks are visible at once
    // (disabled always shows UIColorRole::DisabledText and never
    // recolors on hover - see Label::updateTextColor()'s doc comment).
    auto* labelRow = new newui::SubView();
    labelRow->setVisible(true);
    labelRow->setDesiredSize(newui::Size(0.0f, 24.0f));
    auto labelRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    labelRowLayout->setSpacing(12.0f);
    labelRow->setLayout(std::move(labelRowLayout));
    root.addChild(labelRow);

    auto* enabledLink = new newui::Label();
    enabledLink->setText("Visit our website");
    enabledLink->setHotLink(true);
    enabledLink->setDesiredSize(newui::Size(150.0f, 24.0f));
    enabledLink->onLinkClicked.add(std::function<newui::SyncReturn(newui::Label&)>(
        [](newui::Label&) -> newui::SyncReturn {
            printf("Link clicked\n");
            return newui::SyncReturn::Handled;
        }));
    labelRow->addChild(enabledLink);

    auto* disabledLink = new newui::Label();
    disabledLink->setText("Unavailable link");
    disabledLink->setHotLink(true);
    disabledLink->setEnabled(false);
    disabledLink->setDesiredSize(newui::Size(150.0f, 24.0f));
    labelRow->addChild(disabledLink);

    // Toggles - one checkbox-style (isRadioStyle() false, the default:
    // each click flips isChecked()), one radio-style (isRadioStyle()
    // true: a click always selects, never deselects - see Toggle's own
    // class comment). Toggle draws only the indicator glyph, no text of
    // its own, so each is paired with a sibling Label for a caption, same
    // as every other row.
    auto* toggleRow = new newui::SubView();
    toggleRow->setVisible(true);
    toggleRow->setDesiredSize(newui::Size(0.0f, 24.0f));
    auto toggleRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    toggleRowLayout->setSpacing(6.0f);
    toggleRow->setLayout(std::move(toggleRowLayout));
    root.addChild(toggleRow);

    auto* checkToggle = new newui::Toggle();
    checkToggle->setDesiredSize(newui::Size(16.0f, 16.0f));
    checkToggle->onCheckedChanged.add(std::function<newui::SyncReturn(newui::Toggle&)>(
        [](newui::Toggle& sender) -> newui::SyncReturn {
            printf("Checkbox toggle is now %s\n", sender.isChecked() ? "checked" : "unchecked");
            return newui::SyncReturn::Handled;
        }));
    toggleRow->addChild(checkToggle);

    auto* checkToggleLabel = new newui::Label();
    checkToggleLabel->setText("Check style");
    checkToggleLabel->setDesiredSize(newui::Size(110.0f, 24.0f));
    toggleRow->addChild(checkToggleLabel);

    auto* radioToggle = new newui::Toggle();
    radioToggle->setRadioStyle(true);
    radioToggle->setDesiredSize(newui::Size(16.0f, 16.0f));
    radioToggle->onCheckedChanged.add(std::function<newui::SyncReturn(newui::Toggle&)>(
        [](newui::Toggle& sender) -> newui::SyncReturn {
            printf("Radio toggle is now %s\n", sender.isChecked() ? "checked" : "unchecked");
            return newui::SyncReturn::Handled;
        }));
    toggleRow->addChild(radioToggle);

    auto* radioToggleLabel = new newui::Label();
    radioToggleLabel->setText("Radio style");
    radioToggleLabel->setDesiredSize(newui::Size(110.0f, 24.0f));
    toggleRow->addChild(radioToggleLabel);

    // Sliders - one isInteger(true), one plain float - each paired with
    // its own Label showing the live value (Slider itself draws no text
    // of its own, see its class comment), refreshed from onValueChanged()
    // the same way a caller is expected to.
    auto* intSliderRow = new newui::SubView();
    intSliderRow->setVisible(true);
    intSliderRow->setDesiredSize(newui::Size(0.0f, 32.0f));
    auto intSliderRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    intSliderRowLayout->setSpacing(12.0f);
    intSliderRow->setLayout(std::move(intSliderRowLayout));
    root.addChild(intSliderRow);

    auto* intValueLabel = new newui::Label();
    intValueLabel->setText("Int: 50");
    intValueLabel->setDesiredSize(newui::Size(80.0f, 24.0f));
    intSliderRow->addChild(intValueLabel);

    auto* intSlider = new newui::Slider();
    intSlider->setInteger(true);
    intSlider->setRange(0.0f, 100.0f);
    intSlider->setValue(50.0f);
    // Ties the drawn tick marks to an actual snap increment instead of
    // just the tickCount default - each mark now lands exactly on a
    // reachable value (see Slider::updateTickCount(), controls.cpp).
    intSlider->setStep(10.0f);
    intSlider->setShowTicks(true);
    // Taller than the plain float Slider below - showTicks(true) reserves
    // part of this Slider's own height for the tick strip (see its class
    // comment), so the track/thumb need more room than the default 24px
    // to still look comfortable.
    intSlider->setDesiredSize(newui::Size(0.0f, 32.0f));
    intSlider->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    intSlider->onValueChanged.add(std::function<newui::SyncReturn(newui::Slider&)>(
        [intValueLabel](newui::Slider& sender) -> newui::SyncReturn {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Int: %d", static_cast<int>(sender.value()));
            intValueLabel->setText(buf);
            return newui::SyncReturn::Handled;
        }));
    intSliderRow->addChild(intSlider);

    auto* floatSliderRow = new newui::SubView();
    floatSliderRow->setVisible(true);
    floatSliderRow->setDesiredSize(newui::Size(0.0f, 24.0f));
    auto floatSliderRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    floatSliderRowLayout->setSpacing(12.0f);
    floatSliderRow->setLayout(std::move(floatSliderRowLayout));
    root.addChild(floatSliderRow);

    auto* floatValueLabel = new newui::Label();
    floatValueLabel->setText("Float: 5.00");
    floatValueLabel->setDesiredSize(newui::Size(80.0f, 24.0f));
    floatSliderRow->addChild(floatValueLabel);

    auto* floatSlider = new newui::Slider();
    floatSlider->setRange(0.0f, 10.0f);
    floatSlider->setValue(5.0f);
    floatSlider->setDesiredSize(newui::Size(0.0f, 24.0f));
    floatSlider->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    floatSlider->onValueChanged.add(std::function<newui::SyncReturn(newui::Slider&)>(
        [floatValueLabel](newui::Slider& sender) -> newui::SyncReturn {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Float: %.2f", sender.value());
            floatValueLabel->setText(buf);
            return newui::SyncReturn::Handled;
        }));
    floatSliderRow->addChild(floatSlider);

    // Stepper - a pair of up/down arrows and nothing else (draws no text
    // of its own, see its class comment), paired with a Label the same
    // way the Sliders above are. Not layoutParams(1.0f)'d like the
    // Sliders - a Stepper is a fixed-width control, not one meant to
    // stretch to fill its row.
    auto* stepperRow = new newui::SubView();
    stepperRow->setVisible(true);
    stepperRow->setDesiredSize(newui::Size(0.0f, 32.0f));
    auto stepperRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    stepperRowLayout->setSpacing(12.0f);
    stepperRow->setLayout(std::move(stepperRowLayout));
    root.addChild(stepperRow);

    auto* stepperValueLabel = new newui::Label();
    stepperValueLabel->setText("Stepper: 5");
    stepperValueLabel->setDesiredSize(newui::Size(80.0f, 24.0f));
    stepperRow->addChild(stepperValueLabel);

    auto* stepper = new newui::Stepper();
    stepper->setRange(0.0f, 10.0f);
    stepper->setValue(5.0f);
    stepper->setStep(1.0f);
    stepper->setDesiredSize(newui::Size(20.0f, 32.0f));
    stepper->onValueChanged.add(std::function<newui::SyncReturn(newui::Stepper&)>(
        [stepperValueLabel](newui::Stepper& sender) -> newui::SyncReturn {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Stepper: %d", static_cast<int>(sender.value()));
            stepperValueLabel->setText(buf);
            return newui::SyncReturn::Handled;
        }));
    stepperRow->addChild(stepper);


    // TextField - real, fully interactive single-line text editing (see
    // text-plan.md at the repo root for the phase-by-phase history):
    // click/drag/double/triple-click selection, Shift+Arrow, typing,
    // Backspace/Delete, all wired through newui::TextController.
    auto* textFieldRow = new newui::SubView();
    textFieldRow->setVisible(true);
    textFieldRow->setDesiredSize(newui::Size(0.0f, 32.0f));
    auto textFieldRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    textFieldRowLayout->setSpacing(12.0f);
    textFieldRow->setLayout(std::move(textFieldRowLayout));
    root.addChild(textFieldRow);

    auto* textFieldLabel = new newui::Label();
    textFieldLabel->setText("TextField:");
    textFieldLabel->setDesiredSize(newui::Size(80.0f, 24.0f));
    textFieldRow->addChild(textFieldLabel);

    auto* textField = new newui::TextField();
    textField->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    textField->setText(L"Hello, DirectWrite!");
    textFieldRow->addChild(textField);

    // Highlights "DirectWrite" (offsets 7-18 of "Hello, DirectWrite!") and
    // pre-positions the caret at the very end of the text - just initial
    // state, not shown yet: caret_'s blink doesn't start until this
    // TextField actually gains real focus (a click - see
    // TextController::handleGotFocus(), controls.cpp), same as any real
    // text field. Don't call caret().start() manually here - forcing it
    // active regardless of real focus makes this control (and any other
    // one that does the same) show a blinking caret even while some
    // *other* control actually has focus, which is exactly what
    // confirmed live when both this and textControl (below) did it.
    textField->selection().setRange(newui::text::TextRange(7, 11));
    textField->caret().setPosition(newui::text::TextPosition(textField->text().size()));

    // TextControl - the multi-line counterpart to TextField above, both
    // thin View-integration shims around one shared newui::TextController
    // (controls.h). Word-wraps for free (DirectWrite's own default
    // DWRITE_WORD_WRAPPING_WRAP - TextLayoutEngine/TextRenderer never
    // had to change for this), plus Up/Down line navigation and Enter
    // inserting a real newline - both gated on TextController::
    // isMultiline(), set true by this class's own constructor.
    //
    // TextControl owns no scrollbar of its own (see its own class
    // comment, controls.h) - hosted directly, content taller than its
    // own bounds just clips. Scrolling only happens once it's hosted
    // inside a real ScrollView, as here: textControlScrollView is what
    // FlexLayout actually grows to fill the row; textControl is added to
    // *it*, not to textControlRow directly, and ScrollView detects it as
    // a virtualized child (it answers onQueryContentSize, view.h) -
    // provides the real scrollbar, and pins textControl's own bounds to
    // its viewport instead of growing them to the full document height.
    auto* textControlRow = new newui::SubView();
    textControlRow->setVisible(true);
    textControlRow->setDesiredSize(newui::Size(0.0f, 100.0f));
    auto textControlRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    textControlRowLayout->setSpacing(12.0f);
    textControlRow->setLayout(std::move(textControlRowLayout));
    root.addChild(textControlRow);

    auto* textControlLabel = new newui::Label();
    textControlLabel->setText("TextControl:");
    textControlLabel->setDesiredSize(newui::Size(80.0f, 24.0f));
    textControlRow->addChild(textControlLabel);

    auto* textControlScrollView = new newui::ScrollView();
    textControlScrollView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    textControlRow->addChild(textControlScrollView);

    auto* textControl = new newui::TextControl();
    textControl->setText(
        L"This is a multi-line TextControl.\n"
        L"It word-wraps automatically, and Enter starts a new paragraph.\n"
        L"Up/Down move between visual lines.\n"
        L"This text is deliberately long enough to overflow the box's own height, "
        L"so a vertical scrollbar should appear automatically on the right edge, "
        L"provided by the ScrollView this TextControl is hosted inside - try "
        L"scrolling with the mouse wheel or dragging the scrollbar's thumb.\n"
        L"One more paragraph, just to be sure there's plenty to scroll through.");
    textControlScrollView->addChild(textControl);

    /*
    auto* imageRow = new newui::SubView();
    imageRow->setVisible(true);
    imageRow->setDesiredSize(newui::Size(0.0f, 100.0f));
    auto imageRowRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    imageRowRowLayout->setSpacing(12.0f);
    imageRow->setLayout(std::move(imageRowRowLayout));
    
    root.addChild(imageRow);

    auto* imgCtrl = new newui::Image();
    imgCtrl->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    imgCtrl->setImagePath("C:\\Users\\jim\\Pictures\\Screenshots\\Screenshot 2026-08-14 175923.png");

    imageRow->addChild(imgCtrl);
    */
    

    app.run();

    return 0;
}

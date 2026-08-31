#include "newui/controls.h"
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/color.h"
#include "newui/items.h"
#include "newui/keyboard_constants.h"
#include "newui/popupframe.h"
#include "newui/runloop.h"
#include "newui/uicolormanager.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <cwctype>
#include <stdexcept>

namespace newui {

    Control::Control() {
        onMouseDown.add(this, &Control::handleTrackingMouseDown);
        onMouseUp.add(this, &Control::handleTrackingMouseUp);
    }

    SyncReturn Control::handleTrackingMouseDown(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }
        tracking_ = true;
        return SyncReturn::Handled;
    }

    SyncReturn Control::handleTrackingMouseUp(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        bool wasTracking = tracking_;
        tracking_ = false;
        if (!wasTracking || !isEnabled()) {
            return SyncReturn::Ignored;
        }

        // pt arrives in this Control's own local space (see
        // RootView::mouseUp()'s capturedSubView_ handling) - a
        // zero-origin rect matching this Control's own size is what
        // "still inside" means here, mirroring UIControl's isTouchInside
        // at release time.
        Rect localBounds(Point(0.0f, 0.0f), bounds().size());
        if (localBounds.contains(pt)) {
            onClick(*this);
        }
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // Progress
    // -----------------------------------------------------------------

    Progress::Progress() {
        setVisible(true);

        auto trackStyle = std::make_unique<ThemedProgressBarTrackStyle>();
        trackStyle_ = trackStyle.get();
        setStyle(std::move(trackStyle));

        fill_ = new SubView();
        fill_->setVisible(true);
        auto fillStyle = std::make_unique<ThemedProgressBarFillStyle>();
        fillStyle_ = fillStyle.get();
        fill_->setStyle(std::move(fillStyle));
        addChild(fill_);

        onSizeChanged.add(this, &Progress::handleSizeChanged);
    }

    void Progress::setValue(float newValue) {
        newValue = newValue < 0.0f ? 0.0f : (newValue > 1.0f ? 1.0f : newValue);
        if (value_ == newValue) {
            return;
        }
        value_ = newValue;
        updateFillBounds();
        onValueChanged(*this);
    }

    void Progress::setHorizontal(bool value) {
        if (horizontal_ == value) {
            return;
        }
        horizontal_ = value;
        trackStyle_->horizontal = value;
        fillStyle_->horizontal = value;
        updateFillBounds();
    }

    void Progress::updateFillBounds() {
        Rect client = getClientBounds();

        if (horizontal_) {
            float fillWidth = client.size().width * value_;
            fill_->setBounds(Rect(client.left(), client.top(), fillWidth, client.size().height));
        } else {
            // A vertical progress bar fills from the bottom up, matching
            // a real Win32 vertical progress control (and the intuitive
            // "thermometer" reading).
            float fillHeight = client.size().height * value_;
            fill_->setBounds(Rect(client.left(), client.bottom() - fillHeight, client.size().width, fillHeight));
        }

        // setBounds() alone changes fill_'s geometry but never schedules a
        // repaint - this codebase's paint buffer is only re-rendered on an
        // explicit markDirty() (see ViewStyle::markDirty()'s doc comment:
        // "invalidate() alone would just re-blit the existing pixel
        // buffer - markDirty() is what re-runs paint() into it first" -
        // same pattern example1.cpp's own animated-property callback
        // already follows). Without this, value() changes correctly in
        // memory but the screen only ever catches up as an accidental
        // side effect of something *else* triggering a repaint (e.g.
        // RootView::updateHoveredSubView() marking things dirty on mouse
        // move) - exactly the "only moves when the mouse is over the
        // window" symptom this fixes.
        style().markDirty();
    }

    SyncReturn Progress::handleSizeChanged(View& /*sender*/, const Size& /*size*/) {
        updateFillBounds();
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // Button
    // -----------------------------------------------------------------

    Button::Button() {
        setVisible(true);

        auto buttonStyle = std::make_unique<ThemedButtonStyle>();
        buttonStyle_ = buttonStyle.get();
        buttonStyle_->font = FontManager::getSystemFont(SystemUIFont::Message);
        setStyle(std::move(buttonStyle));

        // UIColorManager::colorFor(), not Color::fromSystemColor() - the
        // latter reads GetSysColor(), the legacy Windows 9x/2000-era
        // "color scheme" concept, which does *not* track the modern
        // Light/Dark mode setting at all (see UIColorManager's own class
        // comment, uicolormanager.h). ThemedButtonStyle's native chrome
        // (ThemedViewStyle::paint(), viewstyle.cpp) *does* darken itself
        // to match UIColorManager::isDarkMode() - using a dark-mode-aware
        // text color here is what keeps this legible against that,
        // instead of e.g. black text landing on a now-dark-inverted
        // button face.
        textColor_ = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();

        // Separate subscriptions from Control's own private click-tracking
        // (already wired in Control::Control()) - these only manage the
        // visual pressed look and, for a toggle Button, flipping
        // isChecked() once a click is confirmed.
        onMouseDown.add(this, &Button::handlePressStart);
        onMouseUp.add(this, &Button::handlePressEnd);
        onClick.add(this, &Button::handleClicked);
    }

    void Button::setText(const std::string& text) {
        if (text_ == text) {
            return;
        }
        text_ = text;
        style().markDirty();
    }

    void Button::setTextColor(BLRgba32 color) {
        textColor_ = color;
        style().markDirty();
    }

    void Button::setChecked(bool value) {
        if (checked_ == value) {
            return;
        }
        checked_ = value;
        updatePressedVisual();
        onCheckedChanged(*this);
    }

    void Button::updatePressedVisual() {
        bool wantPressed = pressing_ || (isToggleButton_ && checked_);
        if (buttonStyle_->pressed == wantPressed) {
            return;
        }
        buttonStyle_->pressed = wantPressed;
        style().markDirty();
    }

    SyncReturn Button::handlePressStart(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }
        pressing_ = true;
        updatePressedVisual();
        return SyncReturn::Handled;
    }

    SyncReturn Button::handlePressEnd(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        pressing_ = false;
        updatePressedVisual();
        return SyncReturn::Handled;
    }

    SyncReturn Button::handleClicked(Control& /*sender*/) {
        if (isToggleButton_) {
            setChecked(!checked_);
        }
        return SyncReturn::Handled;
    }

    void Button::paint(BLContext& ctx) {
        if (text_.empty() || textColor_.is_null()) {
            return;
        }

        BLFont* blFont = buttonStyle_->font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            throw std::runtime_error("Button::paint: font not resolved to a valid BLFont");
        }

        Rect clientBounds = getClientBounds();
        if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
            return;
        }

        BLGlyphBuffer glyphBuffer;
        glyphBuffer.set_utf8_text(text_.c_str(), text_.size());
        blFont->shape(glyphBuffer);

        BLTextMetrics textMetrics;
        blFont->get_text_metrics(glyphBuffer, textMetrics);

        const BLFontMetrics& fontMetrics = blFont->metrics();
        double textWidth = textMetrics.advance.x;
        double textHeight = fontMetrics.ascent + fontMetrics.descent;

        double x = clientBounds.left() + (clientBounds.size().width - textWidth) * 0.5;
        double y = clientBounds.top() + (clientBounds.size().height - textHeight) * 0.5 + fontMetrics.ascent;

        ctx.save();
        ctx.set_comp_op( toBLCompOp( buttonStyle_->compositingOp));
        ctx.set_fill_style(textColor_);
        ctx.set_fill_alpha(buttonStyle_->opacity);
        ctx.fill_utf8_text(BLPoint(x, y), *blFont, text_.c_str(), text_.size());
        ctx.restore();
    }

    // -----------------------------------------------------------------
    // Toggle
    // -----------------------------------------------------------------

    Toggle::Toggle() {
        setVisible(true);

        rebuildStyle();

        onMouseDown.add(this, &Toggle::handlePressStart);
        onMouseUp.add(this, &Toggle::handlePressEnd);
        onClick.add(this, &Toggle::handleClicked);
        onStateChanged.add(this, &Toggle::handleStateChanged);
    }

    void Toggle::setRadioStyle(bool value) {
        if (radioStyle_ == value) {
            return;
        }
        radioStyle_ = value;
        rebuildStyle();
    }

    void Toggle::setChecked(bool value) {
        if (checked_ == value) {
            return;
        }
        checked_ = value;
        updateStyleFields();
        onCheckedChanged(*this);
    }

    void Toggle::rebuildStyle() {
        if (radioStyle_) {
            auto radioButtonStyle = std::make_unique<ThemedRadioButtonStyle>();
            radioButtonStyle_ = radioButtonStyle.get();
            checkBoxStyle_ = nullptr;
            setStyle(std::move(radioButtonStyle));
        } else {
            auto checkBoxStyle = std::make_unique<ThemedCheckBoxStyle>();
            checkBoxStyle_ = checkBoxStyle.get();
            radioButtonStyle_ = nullptr;
            setStyle(std::move(checkBoxStyle));
        }
        updateStyleFields();
    }

    void Toggle::updateStyleFields() {
        if (radioButtonStyle_ != nullptr) {
            radioButtonStyle_->checked = checked_;
            radioButtonStyle_->pressed = pressing_;
            radioButtonStyle_->enabled = isEnabled();
        } else {
            checkBoxStyle_->checked = checked_;
            checkBoxStyle_->pressed = pressing_;
            checkBoxStyle_->enabled = isEnabled();
        }
        style().markDirty();
    }

    SyncReturn Toggle::handlePressStart(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }
        pressing_ = true;
        updateStyleFields();
        return SyncReturn::Handled;
    }

    SyncReturn Toggle::handlePressEnd(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        pressing_ = false;
        updateStyleFields();
        return SyncReturn::Handled;
    }

    SyncReturn Toggle::handleClicked(Control& /*sender*/) {
        // Radio semantics: a click always selects, never deselects - see
        // this class's own doc comment (controls.h) for why.
        setChecked(radioStyle_ ? true : !checked_);
        return SyncReturn::Handled;
    }

    SyncReturn Toggle::handleStateChanged(Control& /*sender*/) {
        updateStyleFields();
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // Label
    // -----------------------------------------------------------------

    Label::Label() {
        setVisible(true);

        auto labelStyle = std::make_unique<LabelStyle>();
        labelStyle_ = labelStyle.get();
        setStyle(std::move(labelStyle));

        textColor_ = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
        linkColor_ = UIColorManager::colorFor(UIColorRole::LinkText).toBLRgba32();
        hoveredLinkColor_ = UIColorManager::colorFor(UIColorRole::LinkHoverText).toBLRgba32();

        onMouseEntered.add(this, &Label::handleMouseEntered);
        onMouseLeft.add(this, &Label::handleMouseLeft);
        onStateChanged.add(this, &Label::handleStateChanged);
        onClick.add(this, &Label::handleClicked);

        updateTextColor();
    }

    void Label::setText(const std::string& text) {
        if (text_ == text) {
            return;
        }
        text_ = text;
        labelStyle_->text = text;
        style().markDirty();
    }

    void Label::setHotLink(bool value) {
        if (hotLink_ == value) {
            return;
        }
        hotLink_ = value;
        labelStyle_->font.setUnderlined(hotLink_);
        // Only while enabled - matches updateTextColor()'s own "disabled
        // never invites a hyperlink click" rule, so a disabled hot-link
        // Label doesn't show a hand cursor for a click that won't do
        // anything anyway.
        cursor().setCursorKind(hotLink_ && isEnabled() ? CursorKind::Hand : CursorKind::Arrow);
        updateTextColor();
    }

    void Label::setTextColor(BLRgba32 color) {
        textColor_ = color;
        updateTextColor();
    }

    void Label::setLinkColor(BLRgba32 color) {
        linkColor_ = color;
        updateTextColor();
    }

    void Label::setHoveredLinkColor(BLRgba32 color) {
        hoveredLinkColor_ = color;
        updateTextColor();
    }

    void Label::updateTextColor() {
        if (!isEnabled()) {
            labelStyle_->textColor = UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32();
        } else if (hotLink_) {
            labelStyle_->textColor = hovering_ ? hoveredLinkColor_ : linkColor_;
        } else {
            labelStyle_->textColor = textColor_;
        }
        style().markDirty();
    }

    SyncReturn Label::handleMouseEntered(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        hovering_ = true;
        updateTextColor();
        return SyncReturn::Handled;
    }

    SyncReturn Label::handleMouseLeft(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        hovering_ = false;
        updateTextColor();
        return SyncReturn::Handled;
    }

    SyncReturn Label::handleStateChanged(Control& /*sender*/) {
        cursor().setCursorKind(hotLink_ && isEnabled() ? CursorKind::Hand : CursorKind::Arrow);
        updateTextColor();
        return SyncReturn::Handled;
    }

    SyncReturn Label::handleClicked(Control& /*sender*/) {
        if (hotLink_) {
            onLinkClicked(*this);
        }
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // Slider
    // -----------------------------------------------------------------

    Slider::Slider() {
        setVisible(true);

        auto trackStyle = std::make_unique<ThemedTrackbarTrackStyle>();
        trackStyle_ = trackStyle.get();
        setStyle(std::move(trackStyle));

        thumb_ = new SubView();
        thumb_->setVisible(true);
        auto thumbStyle = std::make_unique<ThemedTrackbarThumbStyle>();
        thumbStyle_ = thumbStyle.get();
        thumb_->setStyle(std::move(thumbStyle));
        addChild(thumb_);

        onSizeChanged.add(this, &Slider::handleSizeChanged);
        onStateChanged.add(this, &Slider::handleStateChanged);

        // Both this Slider's own mouse delegates and thumb_'s - see
        // handleDragStart()'s own doc comment (controls.h) for why both
        // are needed.
        onMouseDown.add(this, &Slider::handleDragStart);
        onMouseMove.add(this, &Slider::handleDragMove);
        onMouseUp.add(this, &Slider::handleDragEnd);
        thumb_->onMouseDown.add(this, &Slider::handleDragStart);
        thumb_->onMouseMove.add(this, &Slider::handleDragMove);
        thumb_->onMouseUp.add(this, &Slider::handleDragEnd);

        updateThumbBounds();
    }

    void Slider::setValue(float value) {
        if (step_ > 0.0f) {
            // Relative to min_, not zero - see step()'s own doc comment
            // (controls.h) for why.
            value = min_ + std::round((value - min_) / step_) * step_;
        }
        if (integer_) {
            value = std::round(value);
        }
        value = value < min_ ? min_ : (value > max_ ? max_ : value);
        if (value_ == value) {
            return;
        }
        value_ = value;
        updateThumbBounds();
        onValueChanged(*this);
    }

    void Slider::updateTickCount() {
        if (ticksStyle_ == nullptr) {
            return;
        }
        int count = kDefaultTickIntervals;
        if (step_ > 0.0f && max_ > min_) {
            count = static_cast<int>((max_ - min_) / step_ + 0.5f);
            if (count < 1) {
                count = 1;
            } else if (count > kMaxTickIntervals) {
                count = kMaxTickIntervals;
            }
        }
        ticksStyle_->tickCount = count;
        style().markDirty();
    }

    void Slider::setStep(float value) {
        // Negative doesn't make sense as a step size - treat it the same
        // as "no stepping" rather than silently misbehaving.
        step_ = value > 0.0f ? value : 0.0f;
        setValue(value_);
        updateTickCount();
    }

    void Slider::setRange(float minValue, float maxValue) {
        min_ = minValue;
        max_ = maxValue;
        // Re-clamp (and re-round, if isInteger()) the existing value into
        // the new range - goes through setValue() rather than touching
        // value_ directly so onValueChanged() still fires if this
        // actually moves it.
        setValue(value_);
        // updateThumbBounds() again, unconditionally: the thumb's
        // fractional position depends on min_/max_ too, not just
        // value_ - if value_ itself is still valid for the new range,
        // setValue() above correctly no-ops (no spurious
        // onValueChanged()) and therefore never repositions the thumb,
        // even though its correct fraction-of-the-track just changed
        // anyway (e.g. value 50 within [0,100] is 50% along the track;
        // the same value 50 within a new [0,200] range needs to land at
        // 25% instead, with no change to value_ itself to trigger it).
        updateThumbBounds();
        updateTickCount();
    }

    void Slider::setInteger(bool value) {
        if (integer_ == value) {
            return;
        }
        integer_ = value;
        setValue(value_);
    }

    void Slider::setHorizontal(bool value) {
        if (horizontal_ == value) {
            return;
        }
        horizontal_ = value;
        trackStyle_->horizontal = value;
        thumbStyle_->horizontal = value;
        if (ticksStyle_ != nullptr) {
            ticksStyle_->horizontal = value;
        }
        updateThumbBounds();
        updateTicksBounds();
    }

    void Slider::setShowTicks(bool value) {
        if (showTicks_ == value) {
            return;
        }
        showTicks_ = value;

        if (ticks_ == nullptr) {
            ticks_ = new SubView();
            auto ticksStyle = std::make_unique<ThemedTrackbarTicksStyle>();
            ticksStyle_ = ticksStyle.get();
            ticksStyle_->horizontal = horizontal_;
            ticks_->setStyle(std::move(ticksStyle));
            addChild(ticks_);
            updateTickCount();
        }
        ticks_->setVisible(showTicks_);

        // The track/thumb's own usable area (trackRect()) depends on
        // showTicks_ too - both need repositioning, not just the ticks
        // strip itself.
        updateThumbBounds();
        updateTicksBounds();
    }

    Size Slider::resolvedThumbSize() const {
        // Neutralize pressed/enabled for the query - stateId() folds both
        // into the state it asks the theme for, and we want the stable
        // "resting" size to use as this Slider's own layout size, not
        // whatever the thumb's current interaction state happens to
        // report (see the doc comment on the declaration, controls.h).
        // Not cached - GetThemePartSize() is cheap, and caching turned
        // out to be actively wrong: it locked in the fallback forever if
        // this ran even once before this Slider's first real paint() (the
        // theme isn't open yet at that point), leaving an untouched
        // Slider stuck at the wrong size even after theming became
        // available - confirmed live (a never-dragged Slider kept
        // rendering the fallback-sized thumb while a dragged one, which
        // happened to re-run this after painting, picked up the real,
        // very different theme size).
        bool wasPressed = thumbStyle_->pressed;
        bool wasEnabled = thumbStyle_->enabled;
        thumbStyle_->pressed = false;
        thumbStyle_->enabled = true;
        Size resolved = thumbStyle_->partSize(Size(kThumbSize, kThumbSize));
        thumbStyle_->pressed = wasPressed;
        thumbStyle_->enabled = wasEnabled;
        return resolved;
    }

    Size Slider::resolvedTicksSize() const {
        return ticksStyle_->partSize(Size(kTicksSize, kTicksSize));
    }

    Rect Slider::trackRect() const {
        Rect client = getClientBounds();
        if (!showTicks_) {
            return client;
        }
        // Reserve however thick the ticks strip's own theme part
        // actually is (see updateTicksBounds()'s own use of the same
        // query) below (horizontal) or to the right (vertical) for it -
        // see showTicks()'s own doc comment (controls.h). ticksStyle_ is
        // always non-null here (only reachable once showTicks_ is true,
        // which only ever gets set after ticksStyle_ is created - see
        // setShowTicks()).
        Size ticksSize = resolvedTicksSize();
        if (horizontal_) {
            float height = client.size().height - ticksSize.height;
            return Rect(client.left(), client.top(), client.size().width, height > 0.0f ? height : 0.0f);
        }
        float width = client.size().width - ticksSize.width;
        return Rect(client.left(), client.top(), width > 0.0f ? width : 0.0f, client.size().height);
    }

    void Slider::updateThumbBounds() {
        Rect track = trackRect();
        // Queried from the real theme part instead of a guessed fixed
        // constant (kThumbSize is still the fallback, used only until
        // the first real paint() has cached a theme - see partSize()'s
        // own doc comment, viewstyle.h). partId()/stateId() already pick
        // TKP_THUMB vs TKP_THUMBVERT based on horizontal_
        // (ThemedTrackbarThumbStyle, viewstyle.h), so width/height here
        // are already correctly axis-oriented - no swapping needed.
        Size thumbSize = resolvedThumbSize();
        float fraction = (max_ > min_) ? (value_ - min_) / (max_ - min_) : 0.0f;
        fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);

        if (horizontal_) {
            float usable = track.size().width - thumbSize.width;
            float thumbX = track.left() + fraction * (usable > 0.0f ? usable : 0.0f);
            // Centered within the track's own cross-axis extent, not
            // stretched to fill it - a real trackbar thumb has a fixed
            // natural height, typically shorter than a tall control's
            // own full client height.
            float thumbY = track.top() + (track.size().height - thumbSize.height) * 0.5f;
            thumb_->setBounds(Rect(thumbX, thumbY, thumbSize.width, thumbSize.height));
        } else {
            // max at top - matches Progress::updateFillBounds()'s own
            // "fills from the bottom up" vertical convention.
            float usable = track.size().height - thumbSize.height;
            float thumbY = track.top() + (1.0f - fraction) * (usable > 0.0f ? usable : 0.0f);
            float thumbX = track.left() + (track.size().width - thumbSize.width) * 0.5f;
            thumb_->setBounds(Rect(thumbX, thumbY, thumbSize.width, thumbSize.height));
        }

        style().markDirty();
    }

    void Slider::updateTicksBounds() {
        if (ticks_ == nullptr) {
            return;
        }
        Rect client = getClientBounds();
        Rect track = trackRect();
        Size ticksSize = resolvedTicksSize();
        if (horizontal_) {
            ticks_->setBounds(Rect(client.left(), track.bottom(), client.size().width, ticksSize.height));
        } else {
            ticks_->setBounds(Rect(track.right(), client.top(), ticksSize.width, client.size().height));
        }
        style().markDirty();
    }

    Point Slider::toLocalSpace(View& sender, const Point& pt) const {
        if (&sender == thumb_) {
            return pt + thumb_->bounds().pos();
        }
        return pt;
    }

    void Slider::updateValueFromLocalPoint(const Point& localPt) {
        Rect track = trackRect();
        // Same queried size updateThumbBounds() positions the thumb
        // with - keeps the half-thumb offset below consistent with the
        // thumb's actual on-screen width, so a click doesn't map to a
        // value that's slightly off from where the thumb itself would
        // then be drawn.
        Size thumbSize = resolvedThumbSize();
        float fraction;
        if (horizontal_) {
            float usable = track.size().width - thumbSize.width;
            fraction = usable > 0.0f ? (localPt.x - track.left() - thumbSize.width * 0.5f) / usable : 0.0f;
        } else {
            float usable = track.size().height - thumbSize.height;
            fraction = usable > 0.0f ? 1.0f - (localPt.y - track.top() - thumbSize.height * 0.5f) / usable : 0.0f;
        }
        fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        setValue(min_ + fraction * (max_ - min_));
    }

    SyncReturn Slider::handleSizeChanged(View& /*sender*/, const Size& /*size*/) {
        // trackRect() (which both of these ultimately depend on) shrinks
        // to reserve room for the ticks strip when showTicks_ is true, so
        // both need recomputing on every resize - not just the thumb.
        // Missing this left ticks_ stuck at whatever (likely zero) bounds
        // it had from construction time, before the Slider had ever been
        // laid out to a real size - invisible not because of any paint()
        // wrong theme size, but because it was never actually visible in
        // in the first place (reported live: "no tick marks displaying").
        updateThumbBounds();
        updateTicksBounds();
        return SyncReturn::Handled;
    }

    SyncReturn Slider::handleDragStart(View& sender, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }
        dragging_ = true;
        thumbStyle_->pressed = true;
        style().markDirty();
        updateValueFromLocalPoint(toLocalSpace(sender, pt));
        return SyncReturn::Handled;
    }

    SyncReturn Slider::handleDragMove(View& sender, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!dragging_) {
            return SyncReturn::Ignored;
        }
        updateValueFromLocalPoint(toLocalSpace(sender, pt));
        return SyncReturn::Handled;
    }

    SyncReturn Slider::handleDragEnd(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        dragging_ = false;
        thumbStyle_->pressed = false;
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn Slider::handleStateChanged(Control& /*sender*/) {
        thumbStyle_->enabled = isEnabled();
        if (!isEnabled()) {
            dragging_ = false;
            thumbStyle_->pressed = false;
        }
        style().markDirty();
        return SyncReturn::Handled;
    }

    ScrollBar::ScrollBar() {
        setVisible(true);

        auto trackStyle = std::make_unique<ThemedScrollbarTrackStyle>();
        trackStyle->horizontal = horizontal_;
        trackStyle_ = trackStyle.get();
        setStyle(std::move(trackStyle));  // setStyle() sets trackStyle_'s view() to this

        upArrowStyle_ = std::make_unique<ThemedScrollbarArrowStyle>();
        upArrowStyle_->direction = horizontal_ ? ThemedScrollbarArrowStyle::Direction::Left
                                                : ThemedScrollbarArrowStyle::Direction::Up;
        upArrowStyle_->setView(this);

        downArrowStyle_ = std::make_unique<ThemedScrollbarArrowStyle>();
        downArrowStyle_->direction = horizontal_ ? ThemedScrollbarArrowStyle::Direction::Right
                                                  : ThemedScrollbarArrowStyle::Direction::Down;
        downArrowStyle_->setView(this);

        thumbStyle_ = std::make_unique<ThemedScrollbarThumbStyle>();
        thumbStyle_->horizontal = horizontal_;
        thumbStyle_->setView(this);

        onSizeChanged.add(this, &ScrollBar::handleSizeChanged);
        onStateChanged.add(this, &ScrollBar::handleStateChanged);

        onMouseDown.add(this, &ScrollBar::handleMouseDown);
        onMouseMove.add(this, &ScrollBar::handleMouseMove);
        onMouseUp.add(this, &ScrollBar::handleMouseUp);

        updateChildBounds();
    }

    ScrollBar::~ScrollBar() {
        // See startRepeat()'s own doc comment (controls.h) - a repeat
        // task still queued/running past this point becomes a safe no-op
        // instead of touching a dangling this.
        *aliveFlag_ = false;
    }

    void ScrollBar::setValue(float value) {
        float effMax = std::max(min_, max_ - pageSize_);
        value = value < min_ ? min_ : (value > effMax ? effMax : value);
        if (value_ == value) {
            return;
        }
        value_ = value;
        updateChildBounds();
        onValueChanged(*this);
    }

    void ScrollBar::setRange(float minValue, float maxValue) {
        min_ = minValue;
        max_ = maxValue;
        setPageSize(pageSize_);  // re-clamps pageSize_ into the new range, then value_/child rects via the calls below it makes
        setValue(value_);
        // Unconditional, same reasoning as Slider::setRange(): value_'s
        // fraction-of-track depends on min_/max_ too, even when setValue()
        // above correctly no-ops because value_ itself is still in range.
        updateChildBounds();
    }

    void ScrollBar::setPageSize(float pageSize) {
        float maxPage = max_ - min_;
        pageSize = pageSize > 0.0f ? pageSize : 0.0f;
        if (maxPage > 0.0f && pageSize > maxPage) {
            pageSize = maxPage;
        }
        pageSize_ = pageSize;
        setValue(value_);  // effective max (max_-pageSize_) may have moved
        updateChildBounds();  // thumb length depends on pageSize_ too
    }

    void ScrollBar::setLineStep(float lineStep) {
        lineStep_ = lineStep > 0.0f ? lineStep : 0.0f;
    }

    void ScrollBar::setHorizontal(bool value) {
        if (horizontal_ == value) {
            return;
        }
        horizontal_ = value;
        trackStyle_->horizontal = value;
        thumbStyle_->horizontal = value;
        upArrowStyle_->direction = value ? ThemedScrollbarArrowStyle::Direction::Left
                                          : ThemedScrollbarArrowStyle::Direction::Up;
        downArrowStyle_->direction = value ? ThemedScrollbarArrowStyle::Direction::Right
                                            : ThemedScrollbarArrowStyle::Direction::Down;
        updateChildBounds();
    }

    void ScrollBar::paint(BLContext& ctx) {
        // isHighlighted() (View's own, driven by RootView's ordinary
        // hover tracking - "is the cursor anywhere over this ScrollBar")
        // passed to every sub-part uniformly - a real scrollbar
        // highlights as a unit, thumb and both arrows together, not per
        // region. See this class's own doc comment for why that's a
        // single bool here instead of the finer-grained regionAt() used
        // for hit-testing below.
        //
        // dragging_/repeating_ also keep it lit even once the cursor
        // strays outside these bounds mid-gesture - isHighlighted() alone
        // would flip false the instant hitTestChildren() (hover-driven,
        // rootview.cpp) no longer finds this ScrollBar under the cursor,
        // even though mouse capture keeps routing the drag/repeat here
        // regardless of where the cursor actually is - the interaction
        // (and the scroll position it's still changing) is very much
        // still live, so the highlight shouldn't drop out from under it.
        bool hot = isHighlighted() || dragging_ || repeating_;
        Rect unused;

        ctx.save();
        ctx.translate(upArrowRect_.left(), upArrowRect_.top());
        upArrowStyle_->paint(ctx, upArrowRect_.size(), hot, unused);
        ctx.restore();

        ctx.save();
        ctx.translate(downArrowRect_.left(), downArrowRect_.top());
        downArrowStyle_->paint(ctx, downArrowRect_.size(), hot, unused);
        ctx.restore();

        ctx.save();
        ctx.translate(thumbRect_.left(), thumbRect_.top());
        thumbStyle_->paint(ctx, thumbRect_.size(), hot, unused);
        ctx.restore();
    }

    ScrollBar::Region ScrollBar::regionAt(const Point& localPt) const {
        if (upArrowRect_.contains(localPt)) {
            return Region::UpArrow;
        }
        if (downArrowRect_.contains(localPt)) {
            return Region::DownArrow;
        }
        if (thumbRect_.contains(localPt)) {
            return Region::Thumb;
        }
        if (!localRect().contains(localPt)) {
            return Region::None;
        }
        if (horizontal_) {
            return localPt.x < thumbRect_.left() ? Region::TrackBefore : Region::TrackAfter;
        }
        return localPt.y < thumbRect_.top() ? Region::TrackBefore : Region::TrackAfter;
    }

    Rect ScrollBar::localRect() const {
        return Rect(0.0f, 0.0f, bounds().size().width, bounds().size().height);
    }

    Size ScrollBar::resolvedArrowSize() const {
        // Neutralize pressed/enabled for the query, same reasoning as
        // Slider::resolvedThumbSize() - a stable layout size regardless
        // of live interaction state. Only upArrowStyle_ is queried - both
        // arrows share the same SBP_ARROWBTN part, just a different
        // direction/state, and a visual style's natural size for that
        // part doesn't vary by which of the four directions is asked.
        bool wasPressed = upArrowStyle_->pressed;
        bool wasEnabled = upArrowStyle_->enabled;
        upArrowStyle_->pressed = false;
        upArrowStyle_->enabled = true;
        Size resolved = upArrowStyle_->partSize(Size(kArrowFallbackSize, kArrowFallbackSize));
        upArrowStyle_->pressed = wasPressed;
        upArrowStyle_->enabled = wasEnabled;
        return resolved;
    }

    float ScrollBar::resolvedThumbLength(float trackLength) const {
        float range = max_ - min_;
        if (range <= 0.0f || trackLength <= 0.0f) {
            return trackLength > 0.0f ? trackLength : 0.0f;
        }
        float proportional = trackLength * (pageSize_ / range);
        if (proportional < kMinThumbLength) {
            proportional = kMinThumbLength;
        }
        if (proportional > trackLength) {
            proportional = trackLength;
        }
        return proportional;
    }

    Rect ScrollBar::trackRect() const {
        Rect client = localRect();
        Size arrowSize = resolvedArrowSize();
        if (horizontal_) {
            float width = client.size().width - 2.0f * arrowSize.width;
            return Rect(client.left() + arrowSize.width, client.top(),
                width > 0.0f ? width : 0.0f, client.size().height);
        }
        float height = client.size().height - 2.0f * arrowSize.height;
        return Rect(client.left(), client.top() + arrowSize.height,
            client.size().width, height > 0.0f ? height : 0.0f);
    }

    void ScrollBar::updateChildBounds() {
        Rect client = localRect();
        Size arrowSize = resolvedArrowSize();
        Rect track = trackRect();

        // Position fraction is over the *effective* range
        // [min_, max_-pageSize_], not the full [min_, max_] -
        // resolvedThumbLength() already reserves the track length its
        // own proportional footprint needs, so mapping value_ through the
        // effective range is what actually lands the thumb flush against
        // the far end of the track when value_ is at its maximum - see
        // updateValueFromLocalPoint()'s own comment for the matching
        // inverse mapping.
        float effMax = std::max(min_, max_ - pageSize_);
        float denom = effMax - min_;
        float fraction = denom > 0.0f ? (value_ - min_) / denom : 0.0f;
        fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);

        if (horizontal_) {
            upArrowRect_ = Rect(client.left(), client.top(), arrowSize.width, client.size().height);
            downArrowRect_ = Rect(client.right() - arrowSize.width, client.top(),
                arrowSize.width, client.size().height);

            float trackLen = track.size().width;
            float thumbLen = resolvedThumbLength(trackLen);
            float usable = trackLen - thumbLen;
            float thumbX = track.left() + fraction * (usable > 0.0f ? usable : 0.0f);
            thumbRect_ = Rect(thumbX, track.top(), thumbLen, track.size().height);
        } else {
            upArrowRect_ = Rect(client.left(), client.top(), client.size().width, arrowSize.height);
            downArrowRect_ = Rect(client.left(), client.bottom() - arrowSize.height,
                client.size().width, arrowSize.height);

            float trackLen = track.size().height;
            float thumbLen = resolvedThumbLength(trackLen);
            float usable = trackLen - thumbLen;
            float thumbY = track.top() + fraction * (usable > 0.0f ? usable : 0.0f);
            thumbRect_ = Rect(track.left(), thumbY, track.size().width, thumbLen);
        }

        style().markDirty();
    }

    void ScrollBar::updateValueFromLocalPoint(const Point& localPt) {
        Rect track = trackRect();
        float effMax = std::max(min_, max_ - pageSize_);
        float fraction;
        if (horizontal_) {
            float trackLen = track.size().width;
            float thumbLen = resolvedThumbLength(trackLen);
            float usable = trackLen - thumbLen;
            fraction = usable > 0.0f ? (localPt.x - track.left() - thumbLen * 0.5f) / usable : 0.0f;
        } else {
            float trackLen = track.size().height;
            float thumbLen = resolvedThumbLength(trackLen);
            float usable = trackLen - thumbLen;
            fraction = usable > 0.0f ? (localPt.y - track.top() - thumbLen * 0.5f) / usable : 0.0f;
        }
        fraction = fraction < 0.0f ? 0.0f : (fraction > 1.0f ? 1.0f : fraction);
        setValue(min_ + fraction * (effMax - min_));
    }

    void ScrollBar::pageTowardLocalPoint(const Point& localPt) {
        bool forward;
        if (horizontal_) {
            float thumbCenter = thumbRect_.left() + thumbRect_.size().width * 0.5f;
            forward = localPt.x > thumbCenter;
        } else {
            float thumbCenter = thumbRect_.top() + thumbRect_.size().height * 0.5f;
            forward = localPt.y > thumbCenter;
        }
        float amount = pageSize_ > 0.0f ? pageSize_ : (max_ - min_);
        amount = forward ? amount : -amount;
        setValue(value_ + amount);
        startRepeat(amount, /*isTrackRepeat=*/true, localPt);
    }

    void ScrollBar::startRepeat(float amount, bool isTrackRepeat, const Point& trackClickPt) {
        repeating_ = true;
        trackRepeat_ = isTrackRepeat;
        repeatAmount_ = amount;
        trackClickPt_ = trackClickPt;
        repeatNextTime_ = std::chrono::steady_clock::now() + kRepeatInitialDelay;

        std::shared_ptr<bool> alive = aliveFlag_;
        RunLoop& loop = RunLoop::current();
        if (loop) {
            loop.postIdle([this, alive]() {
                if (!*alive || !repeating_) {
                    return true;  // destroyed, or the press already ended - stop
                }

                auto now = std::chrono::steady_clock::now();
                if (now < repeatNextTime_) {
                    return false;  // not time for the next tick yet - keep polling
                }
                repeatNextTime_ = now + kRepeatInterval;

                if (trackRepeat_ && thumbRect_.contains(trackClickPt_)) {
                    // The thumb has caught up to (or passed) the original
                    // click point - real Win32 track-click paging stops here
                    // on its own, without needing a release.
                    repeating_ = false;
                    return true;
                }

                setValue(value_ + repeatAmount_);
                return false;
                });
        }
    }

    void ScrollBar::stopRepeat() {
        repeating_ = false;
    }

    SyncReturn ScrollBar::handleSizeChanged(View& /*sender*/, const Size& /*size*/) {
        updateChildBounds();
        return SyncReturn::Handled;
    }

    SyncReturn ScrollBar::handleMouseDown(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }

        Region region = regionAt(pt);
        switch (region) {
            case Region::Thumb:
                dragging_ = true;
                thumbStyle_->pressed = true;
                style().markDirty();
                break;

            case Region::UpArrow:
            case Region::DownArrow: {
                bool isUp = (region == Region::UpArrow);
                ThemedScrollbarArrowStyle* arrowStyle = isUp ? upArrowStyle_.get() : downArrowStyle_.get();
                arrowStyle->pressed = true;
                style().markDirty();

                float amount = isUp ? -lineStep_ : lineStep_;
                setValue(value_ + amount);
                startRepeat(amount, /*isTrackRepeat=*/false, Point());
                break;
            }

            case Region::TrackBefore:
            case Region::TrackAfter:
                pageTowardLocalPoint(pt);
                break;

            case Region::None:
                return SyncReturn::Ignored;
        }
        return SyncReturn::Handled;
    }

    SyncReturn ScrollBar::handleMouseMove(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (dragging_ || repeating_) {
            // RootView's own hover tracking (updateHoveredSubView(),
            // rootview.cpp) already ran for this same mouse-move just
            // before this handler fires, and will have already cleared
            // isHighlighted() the instant the cursor left this
            // ScrollBar's bounds - correct that back here. A real
            // scrollbar keeps reading as hot for as long as a drag/
            // repeat it started is still live, cursor position
            // notwithstanding - mouse capture keeps routing the gesture
            // here regardless of where the cursor actually is (see
            // RootView::mouseDown()'s capturedSubView_ handling), so the
            // interaction - and the highlight showing it's still live -
            // shouldn't drop out just because the cursor briefly strayed
            // outside these bounds.
            setHighlighted(true);
        }
        if (!dragging_) {
            return SyncReturn::Ignored;
        }
        updateValueFromLocalPoint(pt);
        return SyncReturn::Handled;
    }

    SyncReturn ScrollBar::handleMouseUp(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        dragging_ = false;
        thumbStyle_->pressed = false;
        upArrowStyle_->pressed = false;
        downArrowStyle_->pressed = false;
        // Idempotent no-op if nothing was actually repeating (e.g. this
        // release ends a plain thumb drag, not an arrow/track-click page
        // repeat) - always safe to call.
        stopRepeat();
        // The gesture is over - isHighlighted() should now reflect
        // whether the release actually happened within these bounds, not
        // stay forced on (handleMouseMove()'s own override, above) until
        // some future mouse move happens to correct it.
        setHighlighted(regionAt(pt) != Region::None);
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn ScrollBar::handleStateChanged(Control& /*sender*/) {
        bool enabled = isEnabled();
        thumbStyle_->enabled = enabled;
        upArrowStyle_->enabled = enabled;
        downArrowStyle_->enabled = enabled;
        if (!enabled) {
            dragging_ = false;
            thumbStyle_->pressed = false;
            upArrowStyle_->pressed = false;
            downArrowStyle_->pressed = false;
            stopRepeat();
        }
        style().markDirty();
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // Stepper
    // -----------------------------------------------------------------

    Stepper::Stepper() {
        setVisible(true);

        upStyle_ = std::make_unique<ThemedSpinButtonStyle>();
        upStyle_->isUpButton = true;
        upStyle_->setView(this);

        downStyle_ = std::make_unique<ThemedSpinButtonStyle>();
        downStyle_->isUpButton = false;
        downStyle_->setView(this);

        onStateChanged.add(this, &Stepper::handleStateChanged);

        onMouseDown.add(this, &Stepper::handleMouseDown);
        onMouseMove.add(this, &Stepper::handleMouseMove);
        onMouseUp.add(this, &Stepper::handleMouseUp);
        onMouseLeft.add(this, &Stepper::handleMouseLeft);
        onMouseEntered.add(this, &Stepper::handleMouseEnter);
    }

    Stepper::~Stepper() {
        // See startRepeat()'s own doc comment (controls.h) - a repeat
        // task still queued/running past this point becomes a safe no-op
        // instead of touching a dangling this.
        *aliveFlag_ = false;
    }

    void Stepper::setValue(float value) {
        value = value < min_ ? min_ : (value > max_ ? max_ : value);
        if (value_ == value) {
            return;
        }
        value_ = value;
        style().markDirty();
        onValueChanged(*this);
    }

    void Stepper::setRange(float minValue, float maxValue) {
        min_ = minValue;
        max_ = maxValue;
        setValue(value_);
    }

    void Stepper::setStep(float step) {
        step_ = step > 0.0f ? step : step_;
    }

    void Stepper::paint(BLContext& ctx) {
        // Each arrow's HOT look follows hoverRegion_ independently (see
        // its own doc comment, controls.h) - not a single shared flag
        // the way ScrollBar's thumb+arrows share isHighlighted().
        Rect unused;

        Rect up = upRect();
        ctx.save();
        ctx.translate(up.left(), up.top());
        upStyle_->paint(ctx, up.size(), hoverRegion_ == Region::Up, unused);
        ctx.restore();

        Rect down = downRect();
        ctx.save();
        ctx.translate(down.left(), down.top());
        downStyle_->paint(ctx, down.size(), hoverRegion_ == Region::Down, unused);

        printf("paint upStyle_->pressed: %d, downStyle_->pressed: %d\n", (int)upStyle_->pressed, (int)downStyle_->pressed);

        ctx.restore();
    }

    Stepper::Region Stepper::regionAt(const Point& localPt) const {
        if (upRect().contains(localPt)) {
            return Region::Up;
        }
        if (downRect().contains(localPt)) {
            return Region::Down;
        }
        return Region::None;
    }

    Rect Stepper::upRect() const {
        Size size = bounds().size();
        float halfHeight = size.height * 0.5f;
        return Rect(0.0f, 0.0f, size.width, halfHeight);
    }

    Rect Stepper::downRect() const {
        Size size = bounds().size();
        float halfHeight = size.height * 0.5f;
        return Rect(0.0f, halfHeight, size.width, size.height - halfHeight);
    }

    void Stepper::startRepeat(float amount) {
        repeating_ = true;
        repeatAmount_ = amount;
        repeatNextTime_ = std::chrono::steady_clock::now() + kRepeatInitialDelay;

        std::shared_ptr<bool> alive = aliveFlag_;
        RunLoop& loop = RunLoop::current();
        
        loop.postIdle([this, alive]() {
            if (!*alive || !repeating_) {
                return true;  // destroyed, or the press already ended - stop
            }

            auto now = std::chrono::steady_clock::now();
            if (now < repeatNextTime_) {
                return false;  // not time for the next tick yet - keep polling
            }
            repeatNextTime_ = now + kRepeatInterval;

            setValue(value_ + repeatAmount_);
            return false;
        });
    }

    void Stepper::stopRepeat() {
        repeating_ = false;
    }

    SyncReturn Stepper::handleMouseDown(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }

        Region region = regionAt(pt);

        if (region == Region::None) {
            return SyncReturn::Ignored;
        }

        hoverRegion_ = region;
        bool isUp = (region == Region::Up);
        upStyle_->pressed = isUp;
        downStyle_->pressed = !isUp;

        style().markDirty();

        float amount = isUp ? step_ : -step_;
        setValue(value_ + amount);
        startRepeat(amount);
        return SyncReturn::Handled;
    }

    SyncReturn Stepper::handleMouseMove(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        Region region = regionAt(pt);

        
        if (region != hoverRegion_) {
            hoverRegion_ = region;
            style().markDirty();
        }
        return SyncReturn::Ignored;
    }

    SyncReturn Stepper::handleMouseUp(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        upStyle_->pressed = false;
        downStyle_->pressed = false;
        stopRepeat();
        hoverRegion_ = regionAt(pt);


        style().markDirty();
        return SyncReturn::Handled;
    }

    

    SyncReturn Stepper::handleMouseEnter(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (hoverRegion_ != Region::None) {

        }

        Region region = regionAt(pt);

        hoverRegion_ = region;

        style().markDirty();
        return SyncReturn::Ignored;
    }

    SyncReturn Stepper::handleMouseLeft(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (hoverRegion_ != Region::None) {
            
        }

        hoverRegion_ = Region::None;

        style().markDirty();
        return SyncReturn::Ignored;
    }

    SyncReturn Stepper::handleStateChanged(Control& /*sender*/) {
        bool enabled = isEnabled();
        upStyle_->enabled = enabled;
        downStyle_->enabled = enabled;
        if (!enabled) {
            upStyle_->pressed = false;
            downStyle_->pressed = false;
            hoverRegion_ = Region::None;
            stopRepeat();
        }
        style().markDirty();
        return SyncReturn::Handled;
    }

    ScrollView::ScrollView() {
        setVisible(true);

        viewport_ = new SubView();
        viewport_->setVisible(true);
        // SubView::addChild() explicitly (not this->addChild(), which
        // virtual-dispatches to ScrollView::addChild() below and routes
        // into viewport_ itself instead) - viewport_ is ScrollView's own
        // chrome, not user content, but still needs SubView::addChild()'s
        // real linkage (setParent()/propagateRootView()), not just
        // View::addChild()'s plain childViews_.push_back() - skipping
        // that linkage was a real bug caught live: parent()-walking code
        // (RootView::accumulatedOffset(), the mouse-wheel bubbling in
        // RootView::mouseWheel()) silently stopped dead at viewport_/
        // vBar_/hBar_ since their parent() stayed null, even though
        // painting/rootView() propagation both still worked (those walk
        // childViews_, not parent()) - masking the bug until wheel
        // scrolling was actually tried live.
        SubView::addChild(viewport_);

        vBar_ = new ScrollBar();
        vBar_->setVisible(true);
        vBar_->setHorizontal(false);
        vBar_->onValueChanged.add(this, &ScrollView::handleVBarValueChanged);
        SubView::addChild(vBar_);

        hBar_ = new ScrollBar();
        hBar_->setVisible(true);
        hBar_->setHorizontal(true);
        hBar_->onValueChanged.add(this, &ScrollView::handleHBarValueChanged);
        SubView::addChild(hBar_);

        onSizeChanged.add(this, &ScrollView::handleSizeChanged);
        onMouseWheel.add(this, &ScrollView::handleMouseWheel);
        onQueryContentSize.add(this, &ScrollView::handleQueryContentSize);

        updateLayout();
    }

    void ScrollView::addChild(SubView* child) {
        viewport_->addChild(child);
        // Picks up child's own contentSize() immediately (see
        // updateLayout()'s own comment) rather than leaving contentSize_
        // at whatever it was (typically the default Size(), showing no
        // bars) until some unrelated event - this ScrollView's own resize,
        // say - happens to trigger a recompute.
        updateLayout();
    }

    void ScrollView::removeChild(SubView* child) {
        // viewport_/vBar_/hBar_ themselves route to the real base
        // behavior (removing from *this* ScrollView's own childViews_),
        // not into viewport_ - real user content (routed into viewport_
        // by addChild() above) is the only thing that should ever be
        // redirected there. Without this check, View::destroy()'s own
        // child-teardown cascade - which calls parent_->removeChild(this)
        // polymorphically on each of *this* ScrollView's own direct
        // children, viewport_ included, via SubView::destroy() - would
        // call ScrollView::removeChild(viewport_) while destroying
        // viewport_ itself, redirecting into viewport_->removeChild(viewport_):
        // searches viewport_'s own (unrelated) child list for itself,
        // finds nothing, and leaves viewport_ never actually removed from
        // *this* ScrollView's childViews_ - so destroy()'s loop, which
        // trusts that removal to shrink the list, keeps re-visiting the
        // same already-destroyed front() entry forever - a real
        // use-after-free crash, caught live by this class's own tests
        // (ScrollView.BarsHiddenWhenContentFitsViewport et al., all
        // called destroy() and crashed before this fix).
        if (child == viewport_ || child == vBar_ || child == hBar_) {
            SubView::removeChild(child);
            return;
        }
        viewport_->removeChild(child);
        updateLayout();
    }

    void ScrollView::setContentSize(const Size& size) {
        // Unconditional, even on the early-return below - a caller that
        // explicitly calls this at all wants manual control from now on,
        // regardless of whether the value passed happens to match what
        // auto-derivation would already have produced (see
        // contentSizeOverridden_'s own doc comment, controls.h).
        contentSizeOverridden_ = true;
        if (contentSize_ == size) {
            return;
        }
        contentSize_ = size;
        updateLayout();
    }

    Point ScrollView::contentOrigin() const {
        return viewport_->origin();
    }

    SyncReturn ScrollView::handleQueryContentSize(View& /*sender*/, Size& outSize) {
        outSize = contentSize_;
        return SyncReturn::Handled;
    }

    SubView* ScrollView::virtualizedContentChild() const {
        if (viewport_->childViews().size() != 1) {
            return nullptr;
        }
        SubView* child = viewport_->childViews().front();
        Size probe;
        return child->onQueryContentSize.syncCallFirst(*child, probe).handled() ? child : nullptr;
    }

    void ScrollView::updateLayout() {
        SubView* soleChild = viewport_->childViews().size() == 1 ? viewport_->childViews().front() : nullptr;
        SubView* virtualizedChild = virtualizedContentChild();

        Rect client = getClientBounds();
        if (virtualizedChild) {
            // Pin to the full client width first - contentSize() below
            // needs a real wrap width to answer meaningfully, and
            // whatever bounds() this child happened to have before being
            // hosted here (its construction default, most likely) isn't
            // it. Narrowed to the final viewportWidth (if a vertical bar
            // ends up reserved) further down, once that's known.
            virtualizedChild->setBounds(Rect(0.0f, 0.0f, client.size().width, client.size().height));
        }

        // Re-derive contentSize_ from the sole content child's own
        // contentSize() first, if nothing's ever opted this ScrollView
        // into manual control - see contentSizeOverridden_'s own doc
        // comment (controls.h) for why this only applies with exactly one
        // child. Explicitly reset to Size() (rather than just skipping
        // the update) once that's no longer true - a second child added
        // later leaving contentSize_ frozen at whatever the first child's
        // answer used to be would be stale, misleading data (bars ranged/
        // shown for content that's no longer what's actually being
        // measured), not a reasonable "still valid" fallback.
        if (!contentSizeOverridden_) {
            contentSize_ = soleChild ? soleChild->contentSize() : Size();
        }

        // Natural thickness of each bar - queried from its own arrow part
        // the same way ScrollBar::resolvedArrowSize() does internally;
        // reuse partSize() through a throwaway-free path isn't available
        // here (that helper is private to ScrollBar), so this asks each
        // bar's own current bounds' cross-axis size once one exists, or
        // falls back to the bar's own arrow fallback constant's rough
        // equivalent for the very first layout pass before either bar has
        // ever been sized. Good enough - this only ever misjudges by a
        // couple pixels on the first frame, self-corrects immediately
        // once GetThemePartSize() is available (see ThemedViewStyle::
        // partSize()'s own "no theme cached yet" fallback, viewstyle.h).
        constexpr float kBarThicknessFallback = 16.0f;
        float vBarWidth = vBar_->bounds().size().width > 0.0f ? vBar_->bounds().size().width : kBarThicknessFallback;
        float hBarHeight = hBar_->bounds().size().height > 0.0f ? hBar_->bounds().size().height : kBarThicknessFallback;

        // Two-pass: whether one bar is needed can change whether the
        // other is (showing a vertical bar narrows the viewport, which
        // can newly make a horizontal bar necessary, and vice versa) -
        // check both against the full client first, then re-check each
        // against the space actually left after the other is reserved.
        bool needV = contentSize_.height > client.size().height;
        bool needH = contentSize_.width > client.size().width;
        if (needV && !needH && contentSize_.width > (client.size().width - vBarWidth)) {
            needH = true;
        }
        if (needH && !needV && contentSize_.height > (client.size().height - hBarHeight)) {
            needV = true;
        }

        float viewportWidth = client.size().width - (needV ? vBarWidth : 0.0f);
        float viewportHeight = client.size().height - (needH ? hBarHeight : 0.0f);
        viewportWidth = viewportWidth > 0.0f ? viewportWidth : 0.0f;
        viewportHeight = viewportHeight > 0.0f ? viewportHeight : 0.0f;

        viewport_->setBounds(Rect(client.left(), client.top(), viewportWidth, viewportHeight));

        if (virtualizedChild) {
            // Re-pin at the final (possibly narrower) width and re-derive
            // contentSize_ from it - narrowing can only ever increase
            // wrapped height, never decrease it, so this can't oscillate
            // back to "doesn't need one" (same reasoning TextController::
            // paint() used to rely on for its own, now-removed, internal
            // two-pass layout - see HANDOFF.md).
            virtualizedChild->setBounds(Rect(0.0f, 0.0f, viewportWidth, viewportHeight));
            if (!contentSizeOverridden_) {
                contentSize_ = virtualizedChild->contentSize();
            }
        }

        vBar_->setVisible(needV);
        if (needV) {
            vBar_->setBounds(Rect(client.left() + viewportWidth, client.top(), vBarWidth, viewportHeight));
            vBar_->setRange(0.0f, contentSize_.height);
            vBar_->setPageSize(viewportHeight);
        }

        hBar_->setVisible(needH);
        if (needH) {
            hBar_->setBounds(Rect(client.left(), client.top() + viewportHeight, viewportWidth, hBarHeight));
            hBar_->setRange(0.0f, contentSize_.width);
            hBar_->setPageSize(viewportWidth);
        }

        if (virtualizedChild) {
            // The child stays pinned at (0,0) within viewport_, always -
            // it's told its scroll position directly instead (see
            // onScrollOffsetChanged's own doc comment, view.h).
            viewport_->setOrigin(Point(0.0f, 0.0f));
            Point offset(needH ? hBar_->value() : 0.0f, needV ? vBar_->value() : 0.0f);
            virtualizedChild->onScrollOffsetChanged.syncCall(*virtualizedChild, offset);
        } else {
            Point origin = viewport_->origin();
            viewport_->setOrigin(Point(needH ? hBar_->value() : 0.0f, needV ? vBar_->value() : 0.0f));
            if (origin != viewport_->origin()) {
                viewport_->redraw();
            }
        }
    }

    SyncReturn ScrollView::handleSizeChanged(View& /*sender*/, const Size& /*size*/) {
        updateLayout();
        return SyncReturn::Handled;
    }

    SyncReturn ScrollView::handleVBarValueChanged(ScrollBar& /*sender*/) {
        if (SubView* child = virtualizedContentChild()) {
            child->onScrollOffsetChanged.syncCall(*child, Point(hBar_->isVisible() ? hBar_->value() : 0.0f, vBar_->value()));
            child->redraw();
            return SyncReturn::Handled;
        }
        Point origin = viewport_->origin();
        origin.y = vBar_->value();
        viewport_->setOrigin(origin);
        viewport_->redraw();
        return SyncReturn::Handled;
    }

    SyncReturn ScrollView::handleHBarValueChanged(ScrollBar& /*sender*/) {
        if (SubView* child = virtualizedContentChild()) {
            child->onScrollOffsetChanged.syncCall(*child, Point(hBar_->value(), vBar_->isVisible() ? vBar_->value() : 0.0f));
            child->redraw();
            return SyncReturn::Handled;
        }
        Point origin = viewport_->origin();
        origin.x = hBar_->value();
        viewport_->setOrigin(origin);
        viewport_->redraw();
        return SyncReturn::Handled;
    }

    SyncReturn ScrollView::handleMouseWheel(View& /*sender*/, const Point& /*pt*/, float delta) {
        if (!vBar_->isVisible()) {
            return SyncReturn::Ignored;
        }
        // delta follows WM_MOUSEWHEEL convention (WHEEL_DELTA == 120.0f
        // per notch, positive = away from the user/scroll up) - see
        // RootView's own WM_MOUSEWHEEL handling (rootview.cpp). Scrolls
        // opposite the wheel's sign (wheel up -> content moves up ->
        // vBar_'s value decreases), same direction convention every
        // desktop scroll area uses.
        float notches = delta / 120.0f;
        vBar_->setValue(vBar_->value() - notches * wheelLines_ * vBar_->lineStep());
        return SyncReturn::Handled;
    }


    Image::Image()
    {
        setVisible(true);

        auto imageStyle = std::make_unique<ImageFillStyle>();
        // Align/Center, not ImageFillStyle's (ViewStyle's) own inherited
        // Tile default - see this class's own comment (controls.h) for
        // why a dedicated image control's default should differ from the
        // base ViewStyle default every other image-filled View shares.
        imageStyle->imageFillMode = ImageFillMode::Align;
        imageStyle->imageAlignment = ImageAlignment::Center;
        setStyle(std::move(imageStyle));

        onImagePathChanged.add(this, &Image::updateImage);
    }

    void Image::setImagePath(const std::string& val)
    {
        if (imagePath_ != val) {
            imagePath_ = val;
            onImagePathChanged(*this, this->imagePath_);
        }
    }

    void Image::setImageFillMode(ImageFillMode mode)
    {
        style().imageFillMode = mode;
        style().markDirty();
    }

    void Image::setImageAlignment(ImageAlignment align)
    {
        style().imageAlignment = align;
        style().markDirty();
    }

    // Tries newPath first as a Bundle::instance().loadImage() resource
    // name (Resources/-relative - see bundle.h), then, only if that
    // doesn't resolve, as a plain filesystem path via
    // BLImage::read_from_file() - so either a bare resource name or a
    // real path on disk works through the same setImagePath() call. An
    // absolute path harmlessly fails the Bundle lookup first (Bundle::
    // resourcePath() just checks a resourcesDir()-relative concatenation
    // for existence) before falling through to the direct read.
    SyncReturn Image::updateImage(Image&, const std::string& newPath)
    {
        auto& curStyle = style();

        if (newPath.empty()) {
            return SyncReturn::Handled;
        }

        BLImage image;
        bool loaded = Bundle::instance().loadImage(newPath, image);
        if (!loaded) {
            loaded = image.read_from_file(newPath.c_str()) == BL_SUCCESS;
        }

        if (loaded) {
            curStyle.setBackgroundImage(image);
        }
        curStyle.markDirty();

        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // ToolbarButton
    // -----------------------------------------------------------------

    ToolbarButton::ToolbarButton() {
        setVisible(true);

        auto buttonStyle = std::make_unique<ThemedToolbarButtonStyle>();
        buttonStyle_ = buttonStyle.get();
        buttonStyle_->font = FontManager::getSystemFont(SystemUIFont::Message);
        setStyle(std::move(buttonStyle));

        textColor_ = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();

        onMouseDown.add(this, &ToolbarButton::handlePressStart);
        onMouseUp.add(this, &ToolbarButton::handlePressEnd);
        onClick.add(this, &ToolbarButton::handleClicked);
    }

    void ToolbarButton::setText(const std::string& text) {
        if (text_ == text) {
            return;
        }
        text_ = text;
        style().markDirty();
    }

    void ToolbarButton::setTextColor(BLRgba32 color) {
        textColor_ = color;
        style().markDirty();
    }

    void ToolbarButton::setChecked(bool value) {
        if (checked_ == value) {
            return;
        }
        checked_ = value;
        updatePressedVisual();
        onCheckedChanged(*this);
    }

    void ToolbarButton::updatePressedVisual() {
        bool wantPressed = pressing_;
        bool wantChecked = isToggleButton_ && checked_;
        if (buttonStyle_->pressed == wantPressed && buttonStyle_->checked == wantChecked) {
            return;
        }
        buttonStyle_->pressed = wantPressed;
        buttonStyle_->checked = wantChecked;
        style().markDirty();
    }

    SyncReturn ToolbarButton::handlePressStart(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }
        pressing_ = true;
        updatePressedVisual();
        return SyncReturn::Handled;
    }

    SyncReturn ToolbarButton::handlePressEnd(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        pressing_ = false;
        updatePressedVisual();
        return SyncReturn::Handled;
    }

    SyncReturn ToolbarButton::handleClicked(Control& /*sender*/) {
        if (isToggleButton_) {
            setChecked(!checked_);
        }
        return SyncReturn::Handled;
    }

    void ToolbarButton::paint(BLContext& ctx) {
        if (text_.empty() || textColor_.is_null()) {
            return;
        }

        BLFont* blFont = buttonStyle_->font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            throw std::runtime_error("ToolbarButton::paint: font not resolved to a valid BLFont");
        }

        Rect clientBounds = getClientBounds();
        if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
            return;
        }

        BLGlyphBuffer glyphBuffer;
        glyphBuffer.set_utf8_text(text_.c_str(), text_.size());
        blFont->shape(glyphBuffer);

        BLTextMetrics textMetrics;
        blFont->get_text_metrics(glyphBuffer, textMetrics);

        const BLFontMetrics& fontMetrics = blFont->metrics();
        double textWidth = textMetrics.advance.x;
        double textHeight = fontMetrics.ascent + fontMetrics.descent;

        double x = clientBounds.left() + (clientBounds.size().width - textWidth) * 0.5;
        double y = clientBounds.top() + (clientBounds.size().height - textHeight) * 0.5 + fontMetrics.ascent;

        ctx.save();
        ctx.set_comp_op( toBLCompOp( buttonStyle_->compositingOp));
        ctx.set_fill_style(textColor_);
        ctx.set_fill_alpha(buttonStyle_->opacity);
        ctx.fill_utf8_text(BLPoint(x, y), *blFont, text_.c_str(), text_.size());
        ctx.restore();
    }

    // -----------------------------------------------------------------
    // ToolbarSeparator
    // -----------------------------------------------------------------

    ToolbarSeparator::ToolbarSeparator() {
        setVisible(true);

        auto separatorStyle = std::make_unique<ThemedToolbarSeparatorStyle>();
        separatorStyle_ = separatorStyle.get();
        setStyle(std::move(separatorStyle));

        setDesiredSize(Size(9.0f, 0.0f));
    }

    void ToolbarSeparator::setHorizontal(bool value) {
        if (horizontal_ == value) {
            return;
        }
        horizontal_ = value;
        separatorStyle_->horizontal = value;
        setDesiredSize(horizontal_ ? Size(9.0f, 0.0f) : Size(0.0f, 9.0f));
        style().markDirty();
    }

    // -----------------------------------------------------------------
    // Toolbar
    // -----------------------------------------------------------------

    Toolbar::Toolbar(Orientation orientation) {
        setName("Toolbar");
        setVisible(true);
        setStyle(std::make_unique<ThemedRebarBandStyle>());
        setLayout(std::make_unique<FlexLayout>(orientation));
        setDesiredSize(orientation == Orientation::Horizontal ? Size(0.0f, 28.0f) : Size(28.0f, 0.0f));
    }

    Orientation Toolbar::orientation() const {
        return static_cast<FlexLayout*>(layout())->orientation();
    }

    void Toolbar::setOrientation(Orientation orientation) {
        static_cast<FlexLayout*>(layout())->setOrientation(orientation);
    }

    // -----------------------------------------------------------------
    // TextController
    // -----------------------------------------------------------------

    TextController::TextController(Control& owner) : owner_(owner) {
        // Same defaults/reasoning as Button's own font_/textColor_ setup
        // (see Button::Button() above): UIColorManager::colorFor(), not
        // Color::fromSystemColor(), since only the former tracks the
        // modern Light/Dark mode setting.
        font_ = FontManager::getSystemFont(SystemUIFont::Message);
        textColor_ = UIColorManager::colorFor(UIColorRole::ControlText);

        caret_.onVisibilityChanged.add(this, &TextController::handleCaretVisibilityChanged);

        // Builds the default TextModel and wires it up via setModel()
        // itself (addView()/onBeforeChar/onBeforeRangeChanged, plus
        // Controller::setModel() - see setModel()'s own doc comment) -
        // any later model() mutation (typed input, a future paste, or a
        // direct programmatic setText()/insert() call) marks owner_ dirty
        // on its own via Model::updateAllViews() from that point on;
        // handlers below never need to call owner_.style().markDirty()
        // themselves after editing model().
        setModel(std::make_unique<text::TextModel>());
    }

    TextController::~TextController() {
        // Must run before ownedModel_ (and every other member) is
        // destroyed - see this destructor's own doc comment (controls.h)
        // for the real use-after-free this avoids. Controller::setModel(nullptr)
        // clears model_ to null too, so ~Controller()'s own cleanup
        // (controllers.cpp) becomes a safe no-op once it runs afterward.
        if (Controller::model() != nullptr) {
            model().removeView(&owner_);
            Controller::setModel(nullptr);
        }
    }

    SyncReturn TextController::handleCaretVisibilityChanged(text::Caret& sender) {
        owner_.style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleGotFocus() {
        
        if (RunLoop::current()) {
            caret_.start(RunLoop::current());
        }
        
        // start()/setPosition() below deliberately don't fire
        // onVisibilityChanged themselves (see Caret's own doc comment -
        // "the caller already knows the outcome at the call site") - this
        // markDirty() is that caller's half of the contract. Confirmed
        // live as a real, easy-to-miss gap: without it, a freshly
        // (re)focused caret doesn't actually appear until whatever the
        // next blink tick happens to be, up to a full blink interval
        // later, reading as "laggy" focus/click response.
        owner_.style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleLostFocus() {
        caret_.stop();
        owner_.style().markDirty();
        return SyncReturn::Handled;
    }

    Point TextController::toLayoutSpace(const Point& localPt) const {
        Rect clientBounds = owner_.getClientBounds();
        return Point(localPt.x - clientBounds.left(), localPt.y - clientBounds.top() + scrollOffsetY_);
    }

    void TextController::ensureLayoutUpToDate() {
        Rect clientBounds = owner_.getClientBounds();
        layoutEngine_.update(model().storage(), font_, clientBounds.width(), clientBounds.height(), multiline_);
    }

    void TextController::setScrollOffsetY(float y) {
        scrollOffsetY_ = y;
        owner_.style().markDirty();
    }

    Rect TextController::caretDocumentRect() const {
        Point caretTopLeft;
        float caretHeight = 0.0f;
        layoutEngine_.hitTestPosition(caret_.position(), caretTopLeft, caretHeight);
        return Rect(caretTopLeft.x, caretTopLeft.y, 1.0f, caretHeight);
    }

    void TextController::drawSelection(BLContext& ctx) const {
        if (selection_.isEmpty()) {
            return;
        }
        // Plain BLContext fills (no intermediate D2D/WIC bitmap the way
        // text/caret rendering involves) - shifting via an ordinary
        // ctx.translate() is exactly as cheap regardless of
        // scrollOffsetY_'s value, no need for a TextRenderer-style
        // small-render-target trick here.
        ctx.save();
        ctx.translate(0.0f, -scrollOffsetY_);
        std::vector<Rect> selectionRects;
        for (const text::TextRange& range : selection_.ranges()) {
            std::vector<Rect> rangeRects = layoutEngine_.hitTestRange(range);
            selectionRects.insert(selectionRects.end(), rangeRects.begin(), rangeRects.end());
        }
        selection_.draw(ctx, selectionRects);
        ctx.restore();
    }

    void TextController::drawCaret(BLContext& ctx) const {
        if (!caret_.isVisible()) {
            return;
        }
        Point caretTopLeft;
        float caretHeight = 0.0f;
        layoutEngine_.hitTestPosition(caret_.position(), caretTopLeft, caretHeight);
        caret_.draw(ctx, Point(caretTopLeft.x, caretTopLeft.y - scrollOffsetY_), caretHeight);
    }

    void TextController::clearSelection() {
        selectionAnchor_ = text::TextPosition();
        selection_.clear();
    }

    void TextController::moveCaret(size_t newOffset, bool extendSelection, bool resetPreferredColumn) {
        if (resetPreferredColumn) {
            hasPreferredColumnX_ = false;
        }
        if (extendSelection) {
            if (!selectionAnchor_.isValid()) {
                selectionAnchor_ = caret_.position();
            }
            size_t anchorOffset = selectionAnchor_.offset();
            size_t start = (anchorOffset < newOffset) ? anchorOffset : newOffset;
            size_t length = (anchorOffset < newOffset) ? (newOffset - anchorOffset) : (anchorOffset - newOffset);
            if (length > 0) {
                selection_.setRange(text::TextRange(start, length));
            } else {
                selection_.clear();
            }
        } else {
            clearSelection();
        }
        // setPosition() already resets the blink phase to solid/visible
        // internally - markDirty() is what actually gets that (and any
        // selection_ change above) onto screen immediately rather than
        // waiting for the next blink tick (see handleGotFocus()'s own
        // comment on why this is needed).
        caret_.setPosition(text::TextPosition(newOffset));
        owner_.style().markDirty();
    }

    void TextController::moveCaretVertically(bool up, bool extendSelection) {
        Point currentTopLeft;
        float currentHeight = 0.0f;
        layoutEngine_.hitTestPosition(caret_.position(), currentTopLeft, currentHeight);
        if (currentHeight <= 0.0f) {
            // No layout built yet, or an invalid caret position - nothing
            // sensible to navigate from.
            return;
        }

        float targetX = hasPreferredColumnX_ ? preferredColumnX_ : currentTopLeft.x;
        // A point safely inside the line above/below - half a line above
        // currentTopLeft.y for up (currentTopLeft.y is already the *top*
        // of the current line), one and a half lines below for down
        // (skips past the rest of the current line into the next one).
        float targetY = up ? (currentTopLeft.y - currentHeight * 0.5f) : (currentTopLeft.y + currentHeight * 1.5f);

        text::TextPosition hitPos = layoutEngine_.hitTestPoint(Point(targetX, targetY));
        if (!hitPos.isValid()) {
            // Already on the first/last line - nothing above/below to
            // move to (also where a genuinely single-line TextField
            // always ends up, making this a harmless no-op there).
            return;
        }

        if (!hasPreferredColumnX_) {
            preferredColumnX_ = targetX;
            hasPreferredColumnX_ = true;
        }
        moveCaret(hitPos.offset(), extendSelection, false);
    }

    namespace {
        bool isTextEditingWordChar(wchar_t ch) {
            return std::iswalnum(static_cast<wint_t>(ch)) != 0 || ch == L'_';
        }
    }

    void TextController::selectWordAt(const Point& localPt) {
        text::TextPosition hitPos = layoutEngine_.hitTestPoint(toLayoutSpace(localPt));
        if (!hitPos.isValid()) {
            return;
        }

        const std::wstring& text = model().text();
        if (text.empty()) {
            clearSelection();
            caret_.setPosition(text::TextPosition(0));
            owner_.style().markDirty();
            return;
        }

        size_t offset = (hitPos.offset() < text.size()) ? hitPos.offset() : (text.size() - 1);
        if (!isTextEditingWordChar(text[offset])) {
            clearSelection();
            caret_.setPosition(hitPos);
            owner_.style().markDirty();
            return;
        }

        size_t start = offset;
        while (start > 0 && isTextEditingWordChar(text[start - 1])) {
            --start;
        }
        size_t end = offset;
        while (end < text.size() && isTextEditingWordChar(text[end])) {
            ++end;
        }

        selectionAnchor_ = text::TextPosition(start);
        selection_.setRange(text::TextRange(start, end - start));
        caret_.setPosition(text::TextPosition(end));
        owner_.style().markDirty();
    }

    void TextController::selectAll() {
        size_t length = model().length();
        if (length == 0) {
            clearSelection();
            caret_.setPosition(text::TextPosition(0));
        } else {
            selectionAnchor_ = text::TextPosition(0);
            selection_.setRange(text::TextRange(0, length));
            caret_.setPosition(text::TextPosition(length));
        }
        owner_.style().markDirty();
    }

    SyncReturn TextController::handleMouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
        text::TextPosition hitPos = layoutEngine_.hitTestPoint(toLayoutSpace(pt));
        if (!hitPos.isValid()) {
            return SyncReturn::Ignored;
        }

        // Windows has no triple-click message - handleMouseDblClick()
        // (below) already covers click 2 of a rapid sequence, so a plain
        // handleMouseDown landing within the system double-click time/
        // distance window right after that is click 3. Same timing/
        // distance Windows' own double-click detection itself uses
        // (::GetDoubleClickTime()/SM_CXDOUBLECLK/SM_CYDOUBLECLK).
        auto now = std::chrono::steady_clock::now();
        bool withinMultiClickWindow = clickCount_ > 0
            && (now - lastClickTime_) <= std::chrono::milliseconds(::GetDoubleClickTime())
            && std::abs(pt.x - lastClickPos_.x) <= (::GetSystemMetrics(SM_CXDOUBLECLK) / 2.0f)
            && std::abs(pt.y - lastClickPos_.y) <= (::GetSystemMetrics(SM_CYDOUBLECLK) / 2.0f);

        lastClickTime_ = now;
        lastClickPos_ = pt;

        if (withinMultiClickWindow && clickCount_ == 2) {
            // Reset immediately (rather than letting it climb further) so
            // a 4th rapid click starts a fresh sequence instead of
            // chaining past "select everything".
            clickCount_ = 0;
            selectAll();
            return SyncReturn::Handled;
        }

        clickCount_ = 1;
        dragging_ = true;
        moveCaret(hitPos.offset(), false);
        // moveCaret(..., false) just went through clearSelection(), which
        // resets selectionAnchor_ - re-set it now so a subsequent drag
        // (handleMouseMove(), below) has a fixed anchor to extend from.
        selectionAnchor_ = hitPos;
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleMouseMove(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
        if (!dragging_) {
            return SyncReturn::Ignored;
        }
        text::TextPosition hitPos = layoutEngine_.hitTestPoint(toLayoutSpace(pt));
        if (!hitPos.isValid()) {
            return SyncReturn::Ignored;
        }
        moveCaret(hitPos.offset(), true);
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleMouseUp(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
        bool wasDragging = dragging_;
        dragging_ = false;
        return wasDragging ? SyncReturn::Handled : SyncReturn::Ignored;
    }

    SyncReturn TextController::handleMouseDblClick(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) {
        clickCount_ = 2;
        lastClickTime_ = std::chrono::steady_clock::now();
        lastClickPos_ = pt;
        dragging_ = false;
        selectWordAt(pt);
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleKeyPress(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) {
        wchar_t ch = static_cast<wchar_t>(keyCharVal);
        // Control characters (Backspace/Tab/Enter/Escape/etc.) arrive
        // here too via WM_CHAR - handleKeyDown() (VKeyCode-driven) is
        // where those are actually handled, not this insertion path.
        if (ch < 0x20 || ch == 0x7F) {
            return SyncReturn::Ignored;
        }

        size_t insertAt;
        if (!selection_.isEmpty()) {
            text::TextRange range = selection_.ranges()[0];
            clearSelection();
            model().replace(range, std::wstring(1, ch));
            insertAt = range.start() + 1;
        } else {
            insertAt = caret_.position().isValid() ? caret_.position().offset() : model().length();
            model().insert(insertAt, std::wstring(1, ch));
            insertAt += 1;
        }
        caret_.setPosition(text::TextPosition(insertAt));
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleKeyDown(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) {
        // Left/Right/Up/Down/Home/End below all go through moveCaret()/
        // moveCaretVertically(), which extend selection_ from
        // selectionAnchor_ when extend is true (Shift held) instead of
        // just relocating the caret - see moveCaret()'s own doc comment
        // (controls.h).
        bool extend = (keyMask & kmShift) != 0;

        switch (VKeyCode) {
            case vkBackSpace: {
                if (!selection_.isEmpty()) {
                    text::TextRange range = selection_.ranges()[0];
                    clearSelection();
                    model().remove(range);
                    caret_.setPosition(text::TextPosition(range.start()));
                } else if (caret_.position().isValid() && caret_.position().offset() > 0) {
                    size_t pos = caret_.position().offset() - 1;
                    model().remove(text::TextRange(pos, 1));
                    caret_.setPosition(text::TextPosition(pos));
                }
                return SyncReturn::Handled;
            }
            case vkDelete: {
                if (!selection_.isEmpty()) {
                    text::TextRange range = selection_.ranges()[0];
                    clearSelection();
                    model().remove(range);
                    caret_.setPosition(text::TextPosition(range.start()));
                } else if (caret_.position().isValid() && caret_.position().offset() < model().length()) {
                    model().remove(text::TextRange(caret_.position().offset(), 1));
                }
                return SyncReturn::Handled;
            }
            case vkLeftArrow: {
                size_t current = caret_.position().isValid() ? caret_.position().offset() : 0;
                moveCaret(current > 0 ? current - 1 : current, extend);
                return SyncReturn::Handled;
            }
            case vkRightArrow: {
                size_t current = caret_.position().isValid() ? caret_.position().offset() : model().length();
                moveCaret(current < model().length() ? current + 1 : current, extend);
                return SyncReturn::Handled;
            }
            case vkUpArrow: {
                moveCaretVertically(true, extend);
                return SyncReturn::Handled;
            }
            case vkDownArrow: {
                moveCaretVertically(false, extend);
                return SyncReturn::Handled;
            }
            case vkHome: {
                // The start of the *current visual line*, not the whole
                // document - correct unchanged for a single-line
                // TextField too, since its one line's own lineRange()
                // already starts at 0.
                text::TextRange line = layoutEngine_.lineRange(caret_.position());
                moveCaret(line.isValid() ? line.start() : 0, extend);
                return SyncReturn::Handled;
            }
            case vkEnd: {
                text::TextRange line = layoutEngine_.lineRange(caret_.position());
                moveCaret(line.isValid() ? line.end() : model().length(), extend);
                return SyncReturn::Handled;
            }
            case vkReturn: {
                // Not multiline (TextField) - leave the key alone
                // entirely, for that caller's own onReturnPressed-style
                // hook to react to instead (see TextField::handleKeyDown()).
                if (!multiline_) {
                    return SyncReturn::Ignored;
                }
                size_t insertAt;
                if (!selection_.isEmpty()) {
                    text::TextRange range = selection_.ranges()[0];
                    clearSelection();
                    model().replace(range, L"\n");
                    insertAt = range.start() + 1;
                } else {
                    insertAt = caret_.position().isValid() ? caret_.position().offset() : model().length();
                    model().insert(insertAt, L"\n");
                    insertAt += 1;
                }
                caret_.setPosition(text::TextPosition(insertAt));
                return SyncReturn::Handled;
            }
            default:
                return SyncReturn::Ignored;
        }
    }

    SyncReturn TextController::handleModelBeforeChar(text::TextModel& sender, size_t offset, wchar_t ch, text::CharChangeKind kind, bool& canChange) {
        if (traits_.isReadOnly()) {
            canChange = false;
            return SyncReturn::Handled;
        }
        if (kind == text::CharChangeKind::Inserted && traits_.maxLength() > 0 && model().length() >= traits_.maxLength()) {
            canChange = false;
        }
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleModelBeforeRangeChanged(text::TextModel& sender, const text::TextRange& range, const std::wstring& replacement, bool& canChange) {
        if (traits_.isReadOnly()) {
            canChange = false;
            return SyncReturn::Handled;
        }
        if (traits_.maxLength() > 0) {
            size_t clampedLength = (range.length() > model().length() - range.start()) ? (model().length() - range.start()) : range.length();
            size_t resultLength = model().length() - clampedLength + replacement.size();
            if (resultLength > traits_.maxLength()) {
                canChange = false;
            }
        }
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // TextField
    // -----------------------------------------------------------------

    TextField::TextField() : controller_(std::make_unique<TextController>(*this)) {
        setVisible(true);

        auto editStyle = std::make_unique<ThemedEditStyle>();
        editStyle_ = editStyle.get();
        setStyle(std::move(editStyle));

        onGotFocus.add(this, &TextField::handleGotFocus);
        onLostFocus.add(this, &TextField::handleLostFocus);
        onMouseDown.add(this, &TextField::handleMouseDown);
        onMouseMove.add(this, &TextField::handleMouseMove);
        onMouseUp.add(this, &TextField::handleMouseUp);
        onMouseDblClick.add(this, &TextField::handleMouseDblClick);
        onKeyPress.add(this, &TextField::handleKeyPress);
        onKeyDown.add(this, &TextField::handleKeyDown);
    }

    SyncReturn TextField::handleKeyDown(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) {
        if (VKeyCode == vkReturn) {
            onReturnPressed(*this);
            return SyncReturn::Handled;
        }
        return controller_->handleKeyDown(keyMask, keyCharVal, repeatCount, VKeyCode);
    }

    void TextField::paint(BLContext& ctx) {
        Rect clientBounds = getClientBounds();
        if (clientBounds.width() <= 0.0f || clientBounds.height() <= 0.0f) {
            return;
        }
        controller_->ensureLayoutUpToDate();

        ctx.save();
        ctx.translate(clientBounds.left(), clientBounds.top());
        controller_->drawSelection(ctx);
        renderer_.render(ctx, static_cast<int>(clientBounds.width()), static_cast<int>(clientBounds.height()),
            controller_->model().text(), controller_->font(), controller_->textColor(), controller_->scrollOffsetY(), /*wordWrap=*/false);
        controller_->drawCaret(ctx);
        ctx.restore();
    }

    // -----------------------------------------------------------------
    // TextControl
    // -----------------------------------------------------------------

    TextControl::TextControl() : controller_(std::make_unique<TextController>(*this)) {
        setVisible(true);

        auto editStyle = std::make_unique<ThemedEditStyle>();
        editStyle_ = editStyle.get();
        setStyle(std::move(editStyle));

        controller_->setMultiline(true);

        onGotFocus.add(this, &TextControl::handleGotFocus);
        onLostFocus.add(this, &TextControl::handleLostFocus);
        onMouseDown.add(this, &TextControl::handleMouseDown);
        onMouseMove.add(this, &TextControl::handleMouseMove);
        onMouseUp.add(this, &TextControl::handleMouseUp);
        onMouseDblClick.add(this, &TextControl::handleMouseDblClick);
        onKeyPress.add(this, &TextControl::handleKeyPress);
        onKeyDown.add(this, &TextControl::handleKeyDown);

        // Unconditional (unlike TextField, which never hooks these at
        // all) - see this class's own class comment for why only
        // TextControl participates in ScrollView hosting.
        onQueryContentSize.add(this, &TextControl::handleQueryContentSize);
        onScrollOffsetChanged.add(this, &TextControl::handleScrollOffsetChanged);
        controller_->model().onChanged.add(this, &TextControl::handleModelChanged);
    }

    void TextControl::paint(BLContext& ctx) {
        Rect clientBounds = getClientBounds();
        if (clientBounds.width() <= 0.0f || clientBounds.height() <= 0.0f) {
            return;
        }
        controller_->ensureLayoutUpToDate();

        Rect caretRect = controller_->caretDocumentRect();
        if (caretRect.height() > 0.0f) {
            onRequestScrollIntoView(*this, caretRect);
        }

        ctx.save();
        ctx.translate(clientBounds.left(), clientBounds.top());
        controller_->drawSelection(ctx);
        renderer_.render(ctx, static_cast<int>(clientBounds.width()), static_cast<int>(clientBounds.height()),
            controller_->model().text(), controller_->font(), controller_->textColor(), controller_->scrollOffsetY(), /*wordWrap=*/true);
        controller_->drawCaret(ctx);
        ctx.restore();
    }

    SyncReturn TextControl::handleQueryContentSize(View& /*sender*/, Size& outSize) {
        controller_->ensureLayoutUpToDate();
        outSize = Size(getClientBounds().width(), controller_->contentHeight());
        return SyncReturn::Handled;
    }

    SyncReturn TextControl::handleScrollOffsetChanged(View& /*sender*/, const Point& offset) {
        controller_->setScrollOffsetY(offset.y);
        return SyncReturn::Handled;
    }

    SyncReturn TextControl::handleModelChanged(Model& /*sender*/) {
        onContentSizeChanged(*this);
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // ListView
    // -----------------------------------------------------------------

    ListView::ListView() : controller_(std::make_unique<ListController>()) {
        setVisible(true);
        setStyle(std::make_unique<ThemedEditStyle>());

        onMouseDown.add(this, &ListView::handleMouseDown);
        onMouseMove.add(this, &ListView::handleMouseMove);
        onMouseLeft.add(this, &ListView::handleMouseLeft);
        onQueryContentSize.add(this, &ListView::handleQueryContentSize);
        onScrollOffsetChanged.add(this, &ListView::handleScrollOffsetChanged);
        controller_->onDataChanged.add(this, &ListView::handleDataChanged);
    }

    void ListView::setHoverHighlightEnabled(bool value) {
        hoverHighlightEnabled_ = value;
        if (!hoverHighlightEnabled_ && hoveredIndex_.has_value()) {
            hoveredIndex_.reset();
            style().markDirty();
        }
    }

    void ListView::setKeyboardHighlightedIndex(std::optional<std::size_t> index) {
        if (index.has_value() && *index >= controller_->itemCount()) {
            index.reset();
        }
        if (index == keyboardHighlightedIndex_) {
            return;
        }
        keyboardHighlightedIndex_ = index;
        style().markDirty();
    }

    void ListView::setController(std::unique_ptr<ListController> controller) {
        if (controller == nullptr) {
            return;
        }
        controller_ = std::move(controller);
        controller_->onDataChanged.add(this, &ListView::handleDataChanged);
        onContentSizeChanged(*this);
        style().markDirty();
    }

    void ListView::setModel(ListModel* model) {
        controller_->setModel(model);
        onContentSizeChanged(*this);
        style().markDirty();
    }

    void ListView::setRowHeight(float height) {
        if (height == controller_->defaultItemHeight()) {
            return;
        }
        controller_->setDefaultItemHeight(height);
        onContentSizeChanged(*this);
        style().markDirty();
    }

    std::optional<std::size_t> ListView::selectedIndex() const {
        if (selectionAnchor_.has_value() && selectedIndices_.count(*selectionAnchor_) != 0) {
            return selectionAnchor_;
        }
        if (selectedIndices_.empty()) {
            return std::nullopt;
        }
        return *selectedIndices_.begin();
    }

    void ListView::replaceSelection(std::set<std::size_t> newSelection) {
        if (newSelection == selectedIndices_) {
            return;
        }
        selectedIndices_ = std::move(newSelection);
        style().markDirty();
        onSelectionChanged(*this);
    }

    void ListView::setSelectedIndex(std::optional<std::size_t> index) {
        std::set<std::size_t> newSelection;
        if (index.has_value()) {
            newSelection.insert(*index);
        }
        replaceSelection(std::move(newSelection));
    }

    void ListView::addToSelection(std::size_t index) {
        if (selectedIndices_.count(index) != 0) {
            return;
        }
        std::set<std::size_t> newSelection = selectedIndices_;
        newSelection.insert(index);
        replaceSelection(std::move(newSelection));
    }

    void ListView::removeFromSelection(std::size_t index) {
        if (selectedIndices_.count(index) == 0) {
            return;
        }
        std::set<std::size_t> newSelection = selectedIndices_;
        newSelection.erase(index);
        replaceSelection(std::move(newSelection));
    }

    void ListView::toggleSelection(std::size_t index) {
        if (selectedIndices_.count(index) != 0) {
            removeFromSelection(index);
        } else {
            addToSelection(index);
        }
    }

    void ListView::selectRange(std::size_t first, std::size_t last) {
        std::size_t lo = first < last ? first : last;
        std::size_t hi = first < last ? last : first;
        std::set<std::size_t> newSelection;
        for (std::size_t i = lo; i <= hi; ++i) {
            newSelection.insert(i);
        }
        replaceSelection(std::move(newSelection));
    }

    void ListView::clearSelection() {
        replaceSelection(std::set<std::size_t>());
    }

    void ListView::paint(BLContext& ctx) {
        Rect clientBounds = getClientBounds();
        if (clientBounds.width() <= 0.0f || clientBounds.height() <= 0.0f) {
            return;
        }

        std::size_t count = controller_->itemCount();
        if (count == 0) {
            return;
        }

        ctx.save();
        ctx.translate(clientBounds.left(), clientBounds.top());
        ctx.translate(0.0f, -scrollOffsetY_);

        std::size_t firstVisible = controller_->indexAt(scrollOffsetY_);
        float y = controller_->itemOffset(firstVisible);

        for (std::size_t i = firstVisible; i < count && y < scrollOffsetY_ + clientBounds.height(); ++i) {
            // createItem() never returns nullptr - it throws instead (see
            // ItemController::instantiateItem()'s own doc comment,
            // controllers.h) - a null Item here would only ever mean a
            // real, silent configuration bug (an unregistered Item class),
            // never a legitimate "this row has nothing" state, so there's
            // nothing to skip past.
            ListItem* item = controller_->createItem(i);

            // Item (items.h) is deliberately not a View, so nothing ever
            // calls ViewStyle::setView() for its owned style the way
            // View::setStyle() normally would - without this, a
            // ThemedViewStyle (viewstyle.h) swapped in via setStyle()
            // can't resolve a live HWND (view()->rootView()->windowHandle())
            // and silently draws nothing - a real, confirmed live bug
            // when ListItem used to default to ThemedListItemStyle. Kept
            // here regardless of the current default (plain ViewStyle,
            // unaffected by this) for any caller that swaps in a themed
            // style of their own for the unselected-state chrome.
            item->style().setView(this);

            // setSelected()/setEnabled() (items.h) - not a themed style
            // field - see Item::paint()'s own doc comment for why
            // selected state is a plain flat highlight fill, not a
            // ThemedListItemStyle part.
            item->setSelected(isSelected(i));
            item->setEnabled(isEnabled());
            bool isHovered = hoverHighlightEnabled_ && hoveredIndex_.has_value() && *hoveredIndex_ == i;
            bool isKeyboardHighlighted = keyboardHighlightedIndex_.has_value() && *keyboardHighlightedIndex_ == i;
            item->setHighlighted(isHovered || isKeyboardHighlighted);

            float height = controller_->itemHeight(i);
            if (height <= 0.0f) {
                // A non-positive height would never let y advance -
                // looping forever repainting the same row instead of
                // finishing the frame is worse than failing loudly here,
                // same "fail loud on a genuinely broken invariant"
                // convention ItemController::instantiateItem()'s own
                // throw already uses (controllers.h/.cpp) - a custom
                // ListController::itemHeight() override returning this is
                // a real bug in that override, not a state ListView can
                // do anything sensible with.
                throw std::runtime_error("ListView::paint: ListController::itemHeight() returned a non-positive height");
            }

            Rect rowRect(0.0f, y, clientBounds.width(), height);
            item->paint(ctx, rowRect, i, *controller_);

            controller_->releaseItem(item);
            y += height;
        }

        ctx.restore();

        std::optional<std::size_t> primary = selectedIndex();
        if (primary.has_value()) {
            Rect selectedRect(0.0f, controller_->itemOffset(*primary), clientBounds.width(),
                controller_->itemHeight(*primary));
            onRequestScrollIntoView(*this, selectedRect);
        }
        if (keyboardHighlightedIndex_.has_value()) {
            Rect highlightedRect(0.0f, controller_->itemOffset(*keyboardHighlightedIndex_), clientBounds.width(),
                controller_->itemHeight(*keyboardHighlightedIndex_));
            onRequestScrollIntoView(*this, highlightedRect);
        }
    }

    SyncReturn ListView::handleMouseDown(View& /*sender*/, const Point& pt, std::uint32_t /*btnMask*/, std::uint32_t keyMask) {
        Rect clientBounds = getClientBounds();
        float localY = pt.y - clientBounds.top() + scrollOffsetY_;
        if (localY < 0.0f) {
            return SyncReturn::Ignored;
        }

        std::size_t index = controller_->indexAt(localY);
        if (index >= controller_->itemCount()) {
            return SyncReturn::Ignored;
        }

        if ((keyMask & kmShift) != 0 && selectionAnchor_.has_value()) {
            selectRange(*selectionAnchor_, index);
        } else if ((keyMask & kmCtrl) != 0) {
            toggleSelection(index);
            selectionAnchor_ = index;
        } else {
            setSelectedIndex(index);
            selectionAnchor_ = index;
        }
        return SyncReturn::Handled;
    }

    SyncReturn ListView::handleMouseMove(View& /*sender*/, const Point& pt, std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!hoverHighlightEnabled_) {
            return SyncReturn::Ignored;
        }

        Rect clientBounds = getClientBounds();
        float localY = pt.y - clientBounds.top() + scrollOffsetY_;

        std::optional<std::size_t> newHoveredIndex;
        if (localY >= 0.0f) {
            std::size_t index = controller_->indexAt(localY);
            if (index < controller_->itemCount()) {
                newHoveredIndex = index;
            }
        }

        if (newHoveredIndex == hoveredIndex_) {
            return SyncReturn::Ignored;
        }
        hoveredIndex_ = newHoveredIndex;
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn ListView::handleMouseLeft(View& /*sender*/, const Point& /*pt*/, std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!hoveredIndex_.has_value()) {
            return SyncReturn::Ignored;
        }
        hoveredIndex_.reset();
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn ListView::handleQueryContentSize(View& /*sender*/, Size& outSize) {
        outSize = Size(getClientBounds().width(), controller_->totalHeight());
        return SyncReturn::Handled;
    }

    SyncReturn ListView::handleScrollOffsetChanged(View& /*sender*/, const Point& offset) {
        scrollOffsetY_ = offset.y;
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn ListView::handleDataChanged(ListController& /*sender*/) {
        onContentSizeChanged(*this);
        style().markDirty();
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // TreeView
    // -----------------------------------------------------------------

    TreeView::TreeView() : controller_(std::make_unique<TreeController>()) {
        setVisible(true);
        setStyle(std::make_unique<ThemedEditStyle>());

        onMouseDown.add(this, &TreeView::handleMouseDown);
        onMouseMove.add(this, &TreeView::handleMouseMove);
        onMouseLeft.add(this, &TreeView::handleMouseLeft);
        onQueryContentSize.add(this, &TreeView::handleQueryContentSize);
        onScrollOffsetChanged.add(this, &TreeView::handleScrollOffsetChanged);
        controller_->onDataChanged.add(this, &TreeView::handleDataChanged);
    }

    void TreeView::setController(std::unique_ptr<TreeController> controller) {
        if (controller == nullptr) {
            return;
        }
        controller_ = std::move(controller);
        controller_->onDataChanged.add(this, &TreeView::handleDataChanged);
        onContentSizeChanged(*this);
        style().markDirty();
    }

    void TreeView::setModel(TreeModel* model) {
        controller_->setModel(model);
        onContentSizeChanged(*this);
        style().markDirty();
    }

    void TreeView::setHoverHighlightEnabled(bool value) {
        hoverHighlightEnabled_ = value;
        if (!hoverHighlightEnabled_ && hoveredVisibleIndex_.has_value()) {
            hoveredVisibleIndex_.reset();
            style().markDirty();
        }
    }

    void TreeView::setRowHeight(float height) {
        if (height == controller_->defaultItemHeight()) {
            return;
        }
        controller_->setDefaultItemHeight(height);
        onContentSizeChanged(*this);
        style().markDirty();
    }

    std::optional<std::vector<std::size_t>> TreeView::selectedPath() const {
        if (selectionAnchorPath_.has_value() && selectedPaths_.count(*selectionAnchorPath_) != 0) {
            return selectionAnchorPath_;
        }
        if (selectedPaths_.empty()) {
            return std::nullopt;
        }
        return *selectedPaths_.begin();
    }

    void TreeView::replaceSelection(std::set<std::vector<std::size_t>> newSelection) {
        if (newSelection == selectedPaths_) {
            return;
        }
        selectedPaths_ = std::move(newSelection);
        style().markDirty();
        onSelectionChanged(*this);
    }

    void TreeView::setSelectedPath(std::optional<std::vector<std::size_t>> path) {
        std::set<std::vector<std::size_t>> newSelection;
        if (path.has_value()) {
            newSelection.insert(*path);
        }
        replaceSelection(std::move(newSelection));
    }

    void TreeView::addToSelection(const std::vector<std::size_t>& path) {
        if (selectedPaths_.count(path) != 0) {
            return;
        }
        std::set<std::vector<std::size_t>> newSelection = selectedPaths_;
        newSelection.insert(path);
        replaceSelection(std::move(newSelection));
    }

    void TreeView::removeFromSelection(const std::vector<std::size_t>& path) {
        if (selectedPaths_.count(path) == 0) {
            return;
        }
        std::set<std::vector<std::size_t>> newSelection = selectedPaths_;
        newSelection.erase(path);
        replaceSelection(std::move(newSelection));
    }

    void TreeView::toggleSelection(const std::vector<std::size_t>& path) {
        if (selectedPaths_.count(path) != 0) {
            removeFromSelection(path);
        } else {
            addToSelection(path);
        }
    }

    void TreeView::selectRange(const std::vector<std::size_t>& first, const std::vector<std::size_t>& last) {
        std::optional<std::size_t> firstIndex = controller_->visibleIndexOf(first);
        std::optional<std::size_t> lastIndex = controller_->visibleIndexOf(last);
        if (!firstIndex.has_value() || !lastIndex.has_value()) {
            return;
        }
        std::size_t lo = *firstIndex < *lastIndex ? *firstIndex : *lastIndex;
        std::size_t hi = *firstIndex < *lastIndex ? *lastIndex : *firstIndex;
        std::set<std::vector<std::size_t>> newSelection;
        for (std::size_t i = lo; i <= hi; ++i) {
            newSelection.insert(controller_->pathAt(i));
        }
        replaceSelection(std::move(newSelection));
    }

    void TreeView::clearSelection() {
        replaceSelection(std::set<std::vector<std::size_t>>());
    }

    void TreeView::paint(BLContext& ctx) {
        Rect clientBounds = getClientBounds();
        if (clientBounds.width() <= 0.0f || clientBounds.height() <= 0.0f) {
            return;
        }

        std::size_t count = controller_->visibleCount();
        if (count == 0) {
            return;
        }

        ctx.save();
        ctx.translate(clientBounds.left(), clientBounds.top());
        ctx.translate(0.0f, -scrollOffsetY_);

        std::size_t firstVisible = controller_->indexAt(scrollOffsetY_);
        float y = controller_->itemOffset(firstVisible);

        for (std::size_t i = firstVisible; i < count && y < scrollOffsetY_ + clientBounds.height(); ++i) {
            std::vector<std::size_t> path = controller_->pathAt(i);
            TreeItem* item = controller_->createItem(path);

            item->style().setView(this);
            item->setSelected(isSelected(path));
            item->setEnabled(isEnabled());
            item->setHighlighted(hoverHighlightEnabled_ && hoveredVisibleIndex_.has_value() && *hoveredVisibleIndex_ == i);

            float height = controller_->itemHeight(i);
            if (height <= 0.0f) {
                throw std::runtime_error("TreeView::paint: TreeController::itemHeight() returned a non-positive height");
            }

            Rect rowRect(0.0f, y, clientBounds.width(), height);
            item->paint(ctx, rowRect, path, *controller_);

            controller_->releaseItem(item);
            y += height;
        }

        ctx.restore();

        std::optional<std::vector<std::size_t>> primary = selectedPath();
        if (primary.has_value()) {
            std::optional<std::size_t> primaryIndex = controller_->visibleIndexOf(*primary);
            if (primaryIndex.has_value()) {
                Rect selectedRect(0.0f, controller_->itemOffset(*primaryIndex), clientBounds.width(),
                    controller_->itemHeight(*primaryIndex));
                onRequestScrollIntoView(*this, selectedRect);
            }
        }
    }

    std::optional<std::size_t> TreeView::visibleIndexAtY(float localY) const {
        if (localY < 0.0f) {
            return std::nullopt;
        }
        std::size_t index = controller_->indexAt(localY);
        if (index >= controller_->visibleCount()) {
            return std::nullopt;
        }
        return index;
    }

    bool TreeView::isOverGlyph(float localX, std::size_t depth) const {
        float glyphLeft = float(depth) * kTreeIndentWidth;
        float glyphRight = glyphLeft + kTreeGlyphWidth;
        return localX >= glyphLeft && localX < glyphRight;
    }

    SyncReturn TreeView::handleMouseDown(View& /*sender*/, const Point& pt, std::uint32_t /*btnMask*/, std::uint32_t keyMask) {
        Rect clientBounds = getClientBounds();
        float localY = pt.y - clientBounds.top() + scrollOffsetY_;
        std::optional<std::size_t> visibleIndex = visibleIndexAtY(localY);
        if (!visibleIndex.has_value()) {
            return SyncReturn::Ignored;
        }

        std::vector<std::size_t> path = controller_->pathAt(*visibleIndex);

        float localX = pt.x - clientBounds.left();
        TreeModel* treeModel = controller_->model();
        bool hasChildren = treeModel != nullptr && treeModel->hasChildren(path);
        if (hasChildren && isOverGlyph(localX, treeDepthOf(path))) {
            controller_->toggleExpanded(path);
            return SyncReturn::Handled;
        }

        if ((keyMask & kmShift) != 0 && selectionAnchorPath_.has_value()) {
            selectRange(*selectionAnchorPath_, path);
        } else if ((keyMask & kmCtrl) != 0) {
            toggleSelection(path);
            selectionAnchorPath_ = path;
        } else {
            setSelectedPath(path);
            selectionAnchorPath_ = path;
        }
        return SyncReturn::Handled;
    }

    SyncReturn TreeView::handleMouseMove(View& /*sender*/, const Point& pt, std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!hoverHighlightEnabled_) {
            return SyncReturn::Ignored;
        }

        Rect clientBounds = getClientBounds();
        float localY = pt.y - clientBounds.top() + scrollOffsetY_;
        std::optional<std::size_t> newHoveredIndex = visibleIndexAtY(localY);

        if (newHoveredIndex == hoveredVisibleIndex_) {
            return SyncReturn::Ignored;
        }
        hoveredVisibleIndex_ = newHoveredIndex;
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn TreeView::handleMouseLeft(View& /*sender*/, const Point& /*pt*/, std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!hoveredVisibleIndex_.has_value()) {
            return SyncReturn::Ignored;
        }
        hoveredVisibleIndex_.reset();
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn TreeView::handleQueryContentSize(View& /*sender*/, Size& outSize) {
        outSize = Size(getClientBounds().width(), controller_->totalHeight());
        return SyncReturn::Handled;
    }

    SyncReturn TreeView::handleScrollOffsetChanged(View& /*sender*/, const Point& offset) {
        scrollOffsetY_ = offset.y;
        style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn TreeView::handleDataChanged(TreeController& /*sender*/) {
        onContentSizeChanged(*this);
        style().markDirty();
        return SyncReturn::Handled;
    }

    // -----------------------------------------------------------------
    // DropDownList
    // -----------------------------------------------------------------

    DropDownList::DropDownList() : controller_(std::make_unique<ListController>()) {
        setVisible(true);
        setStyle(std::make_unique<ThemedEditStyle>());

        onMouseDown.add(this, &DropDownList::handleMouseDown);
        onKeyDown.add(this, &DropDownList::handleKeyDown);
    }

    DropDownList::~DropDownList() {
        // Same "force the live window down synchronously before Frame's
        // own destructor sees a live frameHandle_ next to a live
        // rootView_" reasoning as Dialog::~Dialog() (dialogs.cpp) -
        // Frame::~Frame() throws otherwise.
        if (popup_ != nullptr && popup_->frameHandle() != nullptr) {
            ::DestroyWindow(popup_->frameHandle());
        }
    }

    void DropDownList::setModel(ListModel* model) {
        controller_->setModel(model);
        if (selectedIndex_.has_value() && (model == nullptr || *selectedIndex_ >= model->size())) {
            selectedIndex_.reset();
        }
        if (popupListView_ != nullptr) {
            popupListView_->setModel(model);
        }
        style().markDirty();
    }

    void DropDownList::setSelectedIndex(std::optional<std::size_t> index) {
        if (index == selectedIndex_) {
            return;
        }
        selectedIndex_ = index;
        style().markDirty();
        onSelectionChanged(*this);
    }

    bool DropDownList::isOpen() const {
        return popup_ != nullptr && popup_->isVisible();
    }

    Rect DropDownList::buttonRect() const {
        Rect clientBounds = getClientBounds();
        float buttonWidth = clientBounds.size().height;
        if (buttonWidth > clientBounds.size().width) {
            buttonWidth = clientBounds.size().width;
        }
        return Rect(clientBounds.left() + clientBounds.size().width - buttonWidth, clientBounds.top(),
            buttonWidth, clientBounds.size().height);
    }

    void DropDownList::paint(BLContext& ctx) {
        Rect clientBounds = getClientBounds();
        if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
            return;
        }

        Rect button = buttonRect();
        float textWidth = button.left() - clientBounds.left();
        if (textWidth < 0.0f) {
            textWidth = 0.0f;
        }

        if (selectedIndex_.has_value() && controller_->model() != nullptr && textWidth > 0.0f) {
            std::any value = controller_->model()->value(*selectedIndex_);
            if (const std::string* s = std::any_cast<std::string>(&value)) {
                const std::string& text = *s;
                if (!text.empty()) {
                    Font font = FontManager::getSystemFont(SystemUIFont::Message);
                    BLFont* blFont = font.blFont();
                    if (blFont == nullptr || !blFont->is_valid()) {
                        throw std::runtime_error("DropDownList::paint: font not resolved to a valid BLFont");
                    }

                    BLGlyphBuffer glyphBuffer;
                    glyphBuffer.set_utf8_text(text.c_str(), text.size());
                    blFont->shape(glyphBuffer);

                    const BLFontMetrics& fontMetrics = blFont->metrics();
                    double textHeight = fontMetrics.ascent + fontMetrics.descent;
                    double x = clientBounds.left();
                    double y = clientBounds.top() + (clientBounds.size().height - textHeight) * 0.5 + fontMetrics.ascent;

                    ctx.save();
                    ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32());
                    ctx.fill_utf8_text(BLPoint(x, y), *blFont, text.c_str(), text.size());
                    ctx.restore();
                }
            }
        }

        // Small hand-drawn filled-triangle arrow glyph inside button() -
        // same BLPath technique TreeItem's own expand/collapse glyph uses
        // (items.cpp's paintExpandGlyph) - down when closed, up when the
        // popup is currently showing.
        double cx = button.left() + button.size().width * 0.5;
        double cy = button.top() + button.size().height * 0.5;
        double glyphSize = (button.size().width < button.size().height ? button.size().width : button.size().height) * 0.4;
        double half = glyphSize * 0.5;

        BLPath path;
        if (isOpen()) {
            path.move_to(cx - half, cy + half * 0.6);
            path.line_to(cx + half, cy + half * 0.6);
            path.line_to(cx, cy - half * 0.6);
        } else {
            path.move_to(cx - half, cy - half * 0.6);
            path.line_to(cx + half, cy - half * 0.6);
            path.line_to(cx, cy + half * 0.6);
        }
        path.close();

        ctx.save();
        ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32());
        ctx.fill_path(path);
        ctx.restore();
    }

    void DropDownList::openPopup() {
        if (controller_->model() == nullptr || controller_->model()->size() == 0) {
            return;
        }

        RootView* root = rootView();
        if (root == nullptr || root->windowHandle() == nullptr) {
            return;
        }
        // The popup's owner has to be the real top-level window, not
        // RootView's own WS_CHILD HWND - same reasoning
        // MenuBarButtonClicked() (menus.cpp) already documents for
        // TrackPopupMenu's owner.
        Frame* frame = root->getFrame();
        if (frame == nullptr || frame->frameHandle() == nullptr) {
            return;
        }

        bool firstOpen = (popup_ == nullptr);
        if (firstOpen) {
            popup_ = std::make_unique<PopupFrame>();
            popup_->onDismissed.add(this, &DropDownList::handlePopupDismissed);

            popupListView_ = new ListView();
            popupListView_->setVisible(true);
            popupListView_->setLayoutParams(std::make_unique<FlexLayoutParams>(1.0f));

            // A real Layout (not manual bounds-poking) is load-bearing
            // here, not just tidy - see the long comment on why in this
            // method's own header. RootView::setBounds() (rootview.cpp)
            // already calls updateLayout() synchronously as part of
            // handling the real WM_SIZE that show()'s own ShowWindow()
            // triggers - giving popup_->rootView() a FlexLayout means
            // popupListView_ gets arranged to fill it automatically, in
            // that same synchronous call, with no separately-timed
            // setBounds() call of our own that could race show()'s
            // immediate forced repaint.
            auto popupLayout = std::make_unique<FlexLayout>(Orientation::Vertical);
            popupLayout->setSpacing(0.0f);
            popupLayout->setPadding(0.0f);
            popup_->rootView().setLayout(std::move(popupLayout));
            popup_->rootView().addChild(popupListView_);

            // Without an explicit background, whatever's behind an
            // unpainted RootView shows through as a solid black rect
            // (confirmed live). Matches the plain WindowBackground fill
            // the main window's own root View gets in examples/mvc1.cpp.
            popup_->rootView().style().setBackgroundColor(
                UIColorManager::colorFor(UIColorRole::WindowBackground));
        }

        popupListView_->setModel(controller_->model());

        // Restore this control's own current selection into the popup's
        // ListView before wiring/re-triggering its own onSelectionChanged -
        // see restoringPopupSelection_'s own doc comment (controls.h) for
        // why this has to happen before that subscription is (first)
        // installed below.
        restoringPopupSelection_ = true;
        popupListView_->setSelectedIndex(selectedIndex_);
        restoringPopupSelection_ = false;

        // Seeds the keyboard-highlight preview at the already-selected row
        // (or clears it if nothing's selected) - see
        // moveKeyboardHighlight()'s own doc comment (controls.h) for why
        // this needs a well-defined starting point before the first arrow
        // press.
        popupListView_->setKeyboardHighlightedIndex(selectedIndex_);

        if (firstOpen) {
            popupListView_->onSelectionChanged.add(this, &DropDownList::handlePopupListSelectionChanged);
            popupListView_->onMouseDown.add(this, &DropDownList::handlePopupListMouseDown);
        }

        Point controlTopLeftScreen = root->localToScreen(root->accumulatedOffset(this));
        Point screenPt = root->localToScreen(root->accumulatedOffset(this) + Point(0.0f, bounds().size().height));

        static constexpr float kMaxPopupHeight = 200.0f;
        float desiredHeight = popupListView_->controller().totalHeight();
        float popupHeight = desiredHeight > 0.0f
            ? (desiredHeight < kMaxPopupHeight ? desiredHeight : kMaxPopupHeight)
            : controller_->defaultItemHeight();

        // Flips the popup above the control (its bottom edge aligned with
        // the control's own top edge) instead of below it, when there
        // isn't enough room below on the control's current monitor but
        // there is above - per user direction, so a DropDownList near the
        // bottom of the screen still shows a fully visible popup rather
        // than one clipped off/pushed past the screen edge. Best-effort:
        // if MonitorFromWindow()/GetMonitorInfo() fails, or the popup
        // doesn't actually fit in *either* direction, falls back to the
        // plain below placement rather than adding further clamping logic
        // nothing has asked for.
        HMONITOR monitor = ::MonitorFromWindow(frame->frameHandle(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo = {};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (monitor != nullptr && ::GetMonitorInfo(monitor, &monitorInfo)) {
            float spaceBelow = float(monitorInfo.rcWork.bottom) - screenPt.y;
            float spaceAbove = controlTopLeftScreen.y - float(monitorInfo.rcWork.top);
            if (popupHeight > spaceBelow && popupHeight <= spaceAbove) {
                screenPt.y = controlTopLeftScreen.y - popupHeight;
            }
        }

        Rect popupBounds(screenPt.x, screenPt.y, bounds().size().width, popupHeight);

        if (firstOpen) {
            popup_->setBounds(popupBounds);
            if (!popup_->initialize(frame->frameHandle())) {
                popup_.reset();
                popupListView_ = nullptr;
                return;
            }
        } else {
            popup_->moveTo(popupBounds);
        }

        popup_->show();

        style().markDirty();
    }

    void DropDownList::closePopup() {
        if (popup_ != nullptr) {
            popup_->hide();
        }

        // Reclaims both this app's own focus tracking and real Win32
        // keyboard focus back onto this control - closing the popup from
        // an in-app interaction (this method's only callers: the button
        // toggling it closed, a row click, Enter, Escape) must not leave
        // focus wherever RootView::handleMessage()'s own WM_LBUTTONDOWN
        // tail (rootview.cpp) last pointed it. That tail unconditionally
        // calls ::SetFocus(viewHwnd_)/::SetCapture(viewHwnd_) right after
        // dispatching a click - for a row click, popup_->hide() above (via
        // handlePopupListMouseDown(), controls.cpp) already ran *during*
        // that same click's dispatch on the popup's own RootView, so those
        // calls end up targeting the popup's own now-hidden child HWND
        // instead - confirmed live as "the dropdown control loses focus"
        // and a following click landing on whatever's underneath instead
        // (the TreeView, in the reported case). Deliberately NOT done for
        // the outside-click-dismiss path (handlePopupDismissed() below,
        // never routed through this method) - the user dismissed by
        // clicking elsewhere on purpose, so stealing focus back would be
        // exactly the kind of rude behavior a badly-behaved popup does.
        RootView* root = rootView();
        if (root != nullptr) {
            root->setFocusedSubView(this);
            if (root->windowHandle() != nullptr) {
                ::SetFocus(root->windowHandle());
            }
        }

        style().markDirty();
    }

    SyncReturn DropDownList::handleMouseDown(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }
        if (!buttonRect().contains(pt)) {
            return SyncReturn::Ignored;
        }
        if (isOpen()) {
            closePopup();
        } else {
            openPopup();
        }
        return SyncReturn::Handled;
    }

    SyncReturn DropDownList::handlePopupListSelectionChanged(ListView& sender) {
        if (restoringPopupSelection_) {
            return SyncReturn::Ignored;
        }
        // Only syncs selectedIndex_ - does NOT close the popup itself
        // (see handlePopupListMouseDown()'s own doc comment, controls.h,
        // for why: this only fires when the selected *value* actually
        // changes, so re-clicking the already-selected row would never
        // reach here at all).
        setSelectedIndex(sender.selectedIndex());
        return SyncReturn::Handled;
    }

    SyncReturn DropDownList::handlePopupListMouseDown(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        // popupListView_'s own onMouseDown (added in its constructor) has
        // already run by the time this fires - subscriptions call in
        // add()-order, and this is added after that one (openPopup()) -
        // so the click has already been applied to its selection, if it
        // was going to be. This handler only decides whether to close.
        Rect clientBounds = popupListView_->getClientBounds();
        float localY = pt.y - clientBounds.top();
        if (localY < 0.0f) {
            return SyncReturn::Ignored;
        }
        std::size_t index = popupListView_->controller().indexAt(localY);
        if (index >= popupListView_->controller().itemCount()) {
            return SyncReturn::Ignored;
        }
        closePopup();
        return SyncReturn::Ignored;
    }

    SyncReturn DropDownList::handlePopupDismissed(PopupFrame& /*sender*/) {
        style().markDirty();
        return SyncReturn::Handled;
    }

    void DropDownList::moveKeyboardHighlight(int delta) {
        if (popupListView_ == nullptr) {
            return;
        }
        std::size_t count = popupListView_->controller().itemCount();
        if (count == 0) {
            return;
        }
        std::optional<std::size_t> current = popupListView_->keyboardHighlightedIndex();
        long next = current.has_value() ? static_cast<long>(*current) + delta : 0;
        if (next < 0) {
            next = 0;
        }
        if (next >= static_cast<long>(count)) {
            next = static_cast<long>(count) - 1;
        }
        popupListView_->setKeyboardHighlightedIndex(static_cast<std::size_t>(next));
    }

    SyncReturn DropDownList::handleKeyDown(View& /*sender*/, std::uint32_t /*keyMask*/,
            int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t VKeyCode) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }

        if (isOpen()) {
            switch (VKeyCode) {
                case vkUpArrow:
                    moveKeyboardHighlight(-1);
                    return SyncReturn::Handled;
                case vkDownArrow:
                    moveKeyboardHighlight(1);
                    return SyncReturn::Handled;
                case vkReturn: {
                    std::optional<std::size_t> highlighted = popupListView_->keyboardHighlightedIndex();
                    if (highlighted.has_value()) {
                        setSelectedIndex(highlighted);
                    }
                    closePopup();
                    return SyncReturn::Handled;
                }
                case vkEscape:
                    closePopup();
                    return SyncReturn::Handled;
                default:
                    return SyncReturn::Ignored;
            }
        }

        // Popup not visible - Up/Down move the committed selection
        // directly to the prev/next item, per user direction. Starts from
        // index 0 the first time nothing's selected yet, in either
        // direction - a simple, predictable default rather than treating
        // "no selection" as one-before-the-first/one-past-the-last.
        if (controller_->model() == nullptr) {
            return SyncReturn::Ignored;
        }
        std::size_t count = controller_->model()->size();
        if (count == 0) {
            return SyncReturn::Ignored;
        }

        switch (VKeyCode) {
            case vkUpArrow: {
                std::size_t next = 0;
                if (selectedIndex_.has_value() && *selectedIndex_ > 0) {
                    next = *selectedIndex_ - 1;
                }
                setSelectedIndex(next);
                return SyncReturn::Handled;
            }
            case vkDownArrow: {
                std::size_t next = 0;
                if (selectedIndex_.has_value()) {
                    next = *selectedIndex_ + 1;
                    if (next >= count) {
                        next = count - 1;
                    }
                }
                setSelectedIndex(next);
                return SyncReturn::Handled;
            }
            default:
                return SyncReturn::Ignored;
        }
    }

}

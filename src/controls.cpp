#include "newui/controls.h"
#include "newui/color.h"
#include "newui/uicolormanager.h"

#include <cmath>
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
        ctx.set_comp_op(buttonStyle_->compositingOp);
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

}

#include "newui/controls.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/keyboard_constants.h"
#include "newui/runloop.h"
#include "newui/uicolormanager.h"

#include <algorithm>
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
        Application::instance().runLoop().postIdle([this, alive]() {
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
        Application::instance().runLoop().postIdle([this, alive]() {
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

        updateLayout();
    }

    void ScrollView::addChild(SubView* child) {
        viewport_->addChild(child);
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
    }

    void ScrollView::setContentSize(const Size& size) {
        if (contentSize_ == size) {
            return;
        }
        contentSize_ = size;
        updateLayout();
    }

    Point ScrollView::contentOrigin() const {
        return viewport_->origin();
    }

    void ScrollView::updateLayout() {
        Rect client = getClientBounds();
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

        Point origin = viewport_->origin();
        viewport_->setOrigin(Point(needH ? hBar_->value() : 0.0f, needV ? vBar_->value() : 0.0f));
        if (origin != viewport_->origin()) {
            viewport_->redraw();
        }
    }

    SyncReturn ScrollView::handleSizeChanged(View& /*sender*/, const Size& /*size*/) {
        updateLayout();
        return SyncReturn::Handled;
    }

    SyncReturn ScrollView::handleVBarValueChanged(ScrollBar& /*sender*/) {
        Point origin = viewport_->origin();
        origin.y = vBar_->value();
        viewport_->setOrigin(origin);
        viewport_->redraw();
        return SyncReturn::Handled;
    }

    SyncReturn ScrollView::handleHBarValueChanged(ScrollBar& /*sender*/) {
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

        onImagePathChanged.add(this, &Image::updateImage);
    }

    void Image::setImagePath(const std::string& val)
    {
        if (imagePath_ != val) {
            imagePath_ = val;
            onImagePathChanged(*this, this->imagePath_);
        }
    }

    SyncReturn Image::updateImage(Image&, const std::string& newPath)
    {
        auto& curStyle = style();
        curStyle.setBackgroundImage(newPath);
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

        // Any model_ mutation (typed input, a future paste, or a direct
        // programmatic setText()/insert() call) marks owner_ dirty on
        // its own via Model::updateAllViews() - handlers below never
        // need to call owner_.style().markDirty() themselves after
        // editing model_.
        model_.addView(&owner_);

        model_.onBeforeChar.add(this, &TextController::handleModelBeforeChar);
        model_.onBeforeRangeChanged.add(this, &TextController::handleModelBeforeRangeChanged);

        // Always created (see this class's own doc comment on why
        // scrolling is internal), always a real child of owner_ so it
        // paints/hit-tests through the ordinary SubView machinery -
        // starts invisible; paint() below is what decides, every time,
        // whether content actually needs it.
        vScrollBar_ = new ScrollBar();
        vScrollBar_->setVisible(false);
        vScrollBar_->onValueChanged.add(this, &TextController::handleScrollBarValueChanged);
        owner_.addChild(vScrollBar_);
    }

    SyncReturn TextController::handleCaretVisibilityChanged(text::Caret& sender) {
        owner_.style().markDirty();
        return SyncReturn::Handled;
    }

    SyncReturn TextController::handleGotFocus() {
        caret_.start(Application::instance().runLoop());
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

    SyncReturn TextController::handleScrollBarValueChanged(ScrollBar& sender) {
        scrollOffsetY_ = sender.value();
        owner_.style().markDirty();
        return SyncReturn::Handled;
    }

    void TextController::ensureCaretVisible(float viewportHeight) {
        Point caretTopLeft;
        float caretHeight = 0.0f;
        layoutEngine_.hitTestPosition(caret_.position(), caretTopLeft, caretHeight);
        if (caretHeight <= 0.0f) {
            return;
        }
        float newValue = vScrollBar_->value();
        if (caretTopLeft.y < newValue) {
            newValue = caretTopLeft.y;
        } else if (caretTopLeft.y + caretHeight > newValue + viewportHeight) {
            newValue = caretTopLeft.y + caretHeight - viewportHeight;
        }
        // setValue() clamps and no-ops (no onValueChanged, no markDirty())
        // if this doesn't actually change anything - safe to call every
        // paint() unconditionally rather than only when a caret move just
        // happened.
        vScrollBar_->setValue(newValue);
    }

    SyncReturn TextController::handleMouseWheel(const Point& pt, float delta) {
        if (!vScrollBar_->isVisible()) {
            return SyncReturn::Ignored;
        }
        // Same delta convention (WM_MOUSEWHEEL's WHEEL_DELTA == 120.0f
        // per notch) and "scroll opposite the wheel's sign" direction
        // ScrollView::handleMouseWheel() already uses.
        float notches = delta / 120.0f;
        vScrollBar_->setValue(vScrollBar_->value() - notches * vScrollBar_->lineStep());
        return SyncReturn::Handled;
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

        const std::wstring& text = model_.text();
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
        size_t length = model_.length();
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
            model_.replace(range, std::wstring(1, ch));
            insertAt = range.start() + 1;
        } else {
            insertAt = caret_.position().isValid() ? caret_.position().offset() : model_.length();
            model_.insert(insertAt, std::wstring(1, ch));
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
                    model_.remove(range);
                    caret_.setPosition(text::TextPosition(range.start()));
                } else if (caret_.position().isValid() && caret_.position().offset() > 0) {
                    size_t pos = caret_.position().offset() - 1;
                    model_.remove(text::TextRange(pos, 1));
                    caret_.setPosition(text::TextPosition(pos));
                }
                return SyncReturn::Handled;
            }
            case vkDelete: {
                if (!selection_.isEmpty()) {
                    text::TextRange range = selection_.ranges()[0];
                    clearSelection();
                    model_.remove(range);
                    caret_.setPosition(text::TextPosition(range.start()));
                } else if (caret_.position().isValid() && caret_.position().offset() < model_.length()) {
                    model_.remove(text::TextRange(caret_.position().offset(), 1));
                }
                return SyncReturn::Handled;
            }
            case vkLeftArrow: {
                size_t current = caret_.position().isValid() ? caret_.position().offset() : 0;
                moveCaret(current > 0 ? current - 1 : current, extend);
                return SyncReturn::Handled;
            }
            case vkRightArrow: {
                size_t current = caret_.position().isValid() ? caret_.position().offset() : model_.length();
                moveCaret(current < model_.length() ? current + 1 : current, extend);
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
                moveCaret(line.isValid() ? line.end() : model_.length(), extend);
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
                    model_.replace(range, L"\n");
                    insertAt = range.start() + 1;
                } else {
                    insertAt = caret_.position().isValid() ? caret_.position().offset() : model_.length();
                    model_.insert(insertAt, L"\n");
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
        if (kind == text::CharChangeKind::Inserted && traits_.maxLength() > 0 && model_.length() >= traits_.maxLength()) {
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
            size_t clampedLength = (range.length() > model_.length() - range.start()) ? (model_.length() - range.start()) : range.length();
            size_t resultLength = model_.length() - clampedLength + replacement.size();
            if (resultLength > traits_.maxLength()) {
                canChange = false;
            }
        }
        return SyncReturn::Handled;
    }

    void TextController::paint(BLContext& ctx, const Rect& clientBounds) {
        if (clientBounds.width() <= 0.0f || clientBounds.height() <= 0.0f) {
            return;
        }

        // Two-pass, same shape ScrollView::updateLayout() already uses
        // for its own vBar_/hBar_ ("whether one bar is needed can change
        // the space left for content"): lay out at the full width first;
        // if multiline content turns out taller than clientBounds,
        // reserve room for vScrollBar_ and rebuild at the narrower
        // width. Narrowing text can only ever increase wrapped height,
        // never reduce it, so this can't oscillate back to "doesn't need
        // one" on the second pass.
        float textWidth = clientBounds.width();
        layoutEngine_.update(model_.storage(), font_, textWidth, clientBounds.height());

        bool needsScrollBar = multiline_ && layoutEngine_.contentHeight() > clientBounds.height();
        if (needsScrollBar) {
            textWidth = (clientBounds.width() > kScrollBarThickness) ? (clientBounds.width() - kScrollBarThickness) : 0.0f;
            layoutEngine_.update(model_.storage(), font_, textWidth, clientBounds.height());
        }

        vScrollBar_->setVisible(needsScrollBar);
        if (needsScrollBar) {
            float contentHeight = layoutEngine_.contentHeight();
            vScrollBar_->setBounds(Rect(clientBounds.left() + textWidth, clientBounds.top(), kScrollBarThickness, clientBounds.height()));
            vScrollBar_->setRange(0.0f, contentHeight);
            vScrollBar_->setPageSize(clientBounds.height());
            // One text line per lineStep() (used by both vScrollBar_'s
            // own arrow-click stepping and, times a few lines, by
            // handleMouseWheel()) - a single line's own height, from
            // wherever the caret currently is, is as good a proxy as any
            // for "one line" (this font's lines are all the same height
            // regardless of content, so which line doesn't matter).
            Point lineTopLeft;
            float lineHeight = 0.0f;
            layoutEngine_.hitTestPosition(caret_.position(), lineTopLeft, lineHeight);
            vScrollBar_->setLineStep(lineHeight > 0.0f ? lineHeight : 16.0f);
            ensureCaretVisible(clientBounds.height());
        } else {
            scrollOffsetY_ = 0.0f;
        }

        ctx.save();
        ctx.translate(clientBounds.left(), clientBounds.top());

        if (!selection_.isEmpty()) {
            // Selection highlight rects are plain BLContext fills (no
            // intermediate D2D/WIC bitmap the way text/caret rendering
            // below involves) - shifting them via an ordinary
            // ctx.translate() is exactly as cheap regardless of
            // scrollOffsetY_'s value, no need for renderer_'s own
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

        renderer_.render(ctx, static_cast<int>(textWidth), static_cast<int>(clientBounds.height()),
            model_.text(), font_, textColor_, scrollOffsetY_);

        if (caret_.isVisible()) {
            Point caretTopLeft;
            float caretHeight = 0.0f;
            layoutEngine_.hitTestPosition(caret_.position(), caretTopLeft, caretHeight);
            caret_.draw(ctx, Point(caretTopLeft.x, caretTopLeft.y - scrollOffsetY_), caretHeight);
        }

        ctx.restore();
    }

    // -----------------------------------------------------------------
    // TextField
    // -----------------------------------------------------------------

    TextField::TextField() : controller_(*this) {
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
        onMouseWheel.add(this, &TextField::handleMouseWheel);
    }

    SyncReturn TextField::handleKeyDown(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) {
        if (VKeyCode == vkReturn) {
            onReturnPressed(*this);
            return SyncReturn::Handled;
        }
        return controller_.handleKeyDown(keyMask, keyCharVal, repeatCount, VKeyCode);
    }

    // -----------------------------------------------------------------
    // TextControl
    // -----------------------------------------------------------------

    TextControl::TextControl() : controller_(*this) {
        setVisible(true);

        auto editStyle = std::make_unique<ThemedEditStyle>();
        editStyle_ = editStyle.get();
        setStyle(std::move(editStyle));

        controller_.setMultiline(true);

        onGotFocus.add(this, &TextControl::handleGotFocus);
        onLostFocus.add(this, &TextControl::handleLostFocus);
        onMouseDown.add(this, &TextControl::handleMouseDown);
        onMouseMove.add(this, &TextControl::handleMouseMove);
        onMouseUp.add(this, &TextControl::handleMouseUp);
        onMouseDblClick.add(this, &TextControl::handleMouseDblClick);
        onKeyPress.add(this, &TextControl::handleKeyPress);
        onKeyDown.add(this, &TextControl::handleKeyDown);
        onMouseWheel.add(this, &TextControl::handleMouseWheel);
    }

}

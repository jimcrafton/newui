#pragma once

#include <cstdint>

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/subview.h>
#include <newui/viewstyle.h>

#include <blend2d/blend2d.h>

namespace newui {

    // The "basic Control" half of this toolkit's Control/Controller split
    // (see controllers.h's class comment): a SubView that represents
    // something the user interacts with (a button) or that displays live
    // status (a progress bar), and that may itself host other SubViews -
    // but that doesn't own or need any real data behind it. A Control
    // that *does* need real data (a future ListView/TreeView) is expected
    // to own a Controller as a member, not become one - see controllers.h.
    //
    // Not meant to be instantiated directly (constructor is protected) -
    // a subclassing point, like UIKit's UIControl, for Button/Progress/
    // EditControl/TextControl below and future controls.
    class Control : public SubView {
    public:
        enum StateFlags {
            Disabled    = 0x0001,
            HighLighted = 0x0002,
            Selected    = 0x0004,
            Focused     = 0x0008,
        };

        class State {
            public:
                void setEnabled(bool v) { setDisabled(!v); }

                void setDisabled(bool v) {
                    state_ = v ? state_ | StateFlags::Disabled : state_ & ~StateFlags::Disabled;
                }

                bool isEnabled() const { return (state_ & StateFlags::Disabled) == 0; }

                operator std::uint32_t () const { return state_; }
                State& operator=(const std::uint32_t& rhs) { state_ = rhs; return *this; }
            private:
            std::uint32_t   state_ = 0;
        };

        typedef Delegate<Control> StateChangedDelegate;

        // Fired on a "press, then release while still over this Control's
        // bounds" gesture - this toolkit's equivalent of UIControl's
        // target-action .touchUpInside, built on top of View's own raw
        // onMouseDown/onMouseUp (see Control::Control()) rather than a
        // separate dispatch mechanism, since Delegate already covers what
        // UIControl's addTarget:action:for: does (and more - multicast,
        // lambdas, member functions). Does not fire while disabled() -
        // see setEnabled().
        typedef Delegate<Control> ClickDelegate;

        virtual ~Control() {}

        StateChangedDelegate onStateChanged;
        ClickDelegate onClick;

        void setEnabled(bool v) {
            state_.setEnabled(v);
            if (!v) {
                // A press already in progress when this Control becomes
                // disabled shouldn't still produce a click on release -
                // matches UIControl, which never sends actions while
                // isEnabled is false.
                tracking_ = false;
            }
            onStateChanged(*this);
        }

        bool isEnabled() const { return state_.isEnabled(); }

    protected:
        Control();

    private:
        State state_;
        bool tracking_ = false;

        SyncReturn handleTrackingMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleTrackingMouseUp(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
    };




    // A native pushbutton (BUTTON/BP_PUSHBUTTON, via ThemedButtonStyle)
    // with its own text drawn on top in paint() - unlike Progress, no
    // child SubView for the content. That's deliberate, not just simpler:
    // hit-testing walks to the *deepest* SubView under the cursor (see
    // View::hitTestChildren()), so a text child would intercept hover and
    // become RootView's hoveredSubView_ instead of the Button itself -
    // and PBS_HOT (the native "hot"/hover look, see
    // ThemedButtonStyle::stateId()) is driven by isHighlighted(), which
    // only ever gets set on whichever SubView RootView actually considers
    // hovered. Drawing text directly in this Button's own paint() means
    // there's nothing else that could steal the hover.
    class Button : public Control {
    public:
        Button();
        virtual ~Button() {}

        const std::string& text() const { return text_; }
        void setText(const std::string& text);

        // Drawn on top of the native chrome - ThemedButtonStyle (like
        // every other ThemedViewStyle) only knows how to draw the native
        // part itself, not text (same "chrome vs. content" split
        // LabelStyle handles for a plain ViewStyle, viewstyle.h).
        void setTextColor(BLRgba32 color);

        // Momentary (default, a normal push button) vs. toggle mode. A
        // momentary Button's pressed look only follows the live mouse-
        // down-inside gesture. A toggle Button additionally flips
        // isChecked() on every completed click (the same down-then-up-
        // inside gesture that fires onClick()) and stays visually pressed
        // (PBS_PRESSED) while isChecked() is true, even once the mouse is
        // released - e.g. a toolbar Bold/Italic toggle. This is a
        // *pushbutton* that stays sunken, not the checkbox/radio glyph -
        // see ThemedCheckBoxStyle/ThemedRadioButtonStyle (viewstyle.h)
        // for that shape instead.
        bool isToggleButton() const { return isToggleButton_; }
        void setToggleButton(bool value) { isToggleButton_ = value; }

        bool isChecked() const { return checked_; }
        void setChecked(bool value);

        typedef Delegate<Button> CheckedChangedDelegate;
        // Fired whenever isChecked() actually changes - both from a
        // completed toggle click and from a direct setChecked() call.
        CheckedChangedDelegate onCheckedChanged;

        void paint(BLContext& ctx) override;

    private:
        void updatePressedVisual();

        // Separate from Control's own private tracking_ (click-gesture
        // detection, already correct - see Control::handleTrackingMouseUp())
        // - this Button additionally listens to the same onMouseDown/
        // onMouseUp to know when to *look* pressed. Doesn't attempt real
        // Win32 button behavior of un-pressing while the cursor drags
        // outside the bounds before releasing - stays visually pressed
        // for the whole gesture once started over this Button, same
        // simplification Control's own click-tracking doesn't make
        // (that one already correctly requires release-*inside* to fire
        // onClick - only the intermediate visual feedback is simplified
        // here).
        SyncReturn handlePressStart(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handlePressEnd(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleClicked(Control& sender);

        std::string text_;
        BLVar textColor_;
        bool isToggleButton_ = false;
        bool checked_ = false;
        bool pressing_ = false;
        ThemedButtonStyle* buttonStyle_ = nullptr;
    };

    // A checkable Control drawn as a native checkbox (BUTTON/BP_CHECKBOX)
    // or radio button (BUTTON/BP_RADIOBUTTON) glyph - own style() is
    // whichever of ThemedCheckBoxStyle/ThemedRadioButtonStyle
    // isRadioStyle() currently selects (see rebuildStyle(), controls.cpp),
    // swapped wholesale rather than shared through one style with a flag,
    // since the two are different theme parts, not the same part with
    // different state values. No text/label of its own (unlike Button/
    // Label) - just the indicator glyph, the same "one focused thing per
    // Control" shape as Slider's thumb/track; pair with a sibling Label
    // in a row for a captioned checkbox/radio, the same way the
    // controls1 demo already pairs a Label next to each Slider.
    //
    // isRadioStyle(false) (the default) behaves like a real Win32
    // checkbox: each completed click (Control::onClick(), the down-then-
    // up-inside gesture) flips isChecked(). isRadioStyle(true) behaves
    // like a real radio button instead: a completed click always *sets*
    // isChecked() to true and never clears it - clicking an already-
    // checked radio-style Toggle is a no-op, matching how a real radio
    // button only turns off when a *different* one in its group gets
    // selected. This class has no group/mutual-exclusion concept of its
    // own (no shared owner, no "the other Toggles in this group" list) -
    // a caller wanting real radio-group behavior across several Toggles
    // wires that up itself, e.g. each Toggle's onCheckedChanged calling
    // setChecked(false) on the rest of the group.
    class Toggle : public Control {
    public:
        Toggle();
        virtual ~Toggle() {}

        bool isRadioStyle() const { return radioStyle_; }
        void setRadioStyle(bool value);

        bool isChecked() const { return checked_; }
        void setChecked(bool value);

        typedef Delegate<Toggle> CheckedChangedDelegate;
        // Fired whenever isChecked() actually changes - both from a
        // completed click and from a direct setChecked() call (same
        // "fires either way" shape as Button::onCheckedChanged).
        CheckedChangedDelegate onCheckedChanged;

    private:
        // Replaces style() with a fresh instance of whichever concrete
        // type isRadioStyle() currently selects, carrying checked_/
        // pressing_/isEnabled() over into it - called once from the
        // constructor and again on every setRadioStyle() change.
        void rebuildStyle();
        // Pushes checked_/pressing_/isEnabled() into whichever of
        // checkBoxStyle_/radioButtonStyle_ is currently installed.
        void updateStyleFields();

        SyncReturn handlePressStart(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handlePressEnd(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleClicked(Control& sender);
        SyncReturn handleStateChanged(Control& sender);

        bool radioStyle_ = false;
        bool checked_ = false;
        bool pressing_ = false;
        // Only one of these two is ever non-null at a time - whichever
        // matches the currently-installed style() (see rebuildStyle()).
        ThemedCheckBoxStyle* checkBoxStyle_ = nullptr;
        ThemedRadioButtonStyle* radioButtonStyle_ = nullptr;
    };

    // A text-only Control - own style() is a LabelStyle (which already
    // does exactly "background/border plus centered text", see
    // viewstyle.h), reused directly rather than reimplementing text
    // drawing the way Button has to (Button's chrome is native/uxtheme,
    // LabelStyle's is plain Blend2D, so there's nothing to layer text on
    // top of here - no paint() override needed). No child SubView either,
    // same reasoning as Button: a text child would intercept hover
    // (View::hitTestChildren() walks to the deepest SubView) instead of
    // this Label itself.
    //
    // isHotLink(false) (the default) is a plain, non-interactive caption -
    // Control's own click-tracking still technically runs (same as
    // Progress ignoring clicks, see its own class comment) but nothing
    // ever reacts to it. isHotLink(true) turns this into an HTML-anchor-
    // like control: text recolors between linkColor()/hoveredLinkColor()
    // as the mouse enters/leaves (onMouseEntered()/onMouseLeft(), the
    // same RootView-driven hover delegates Button's own PBS_HOT look
    // relies on isHighlighted() for - this uses the delegates directly
    // instead, since there's no native "hot" state to hook into here),
    // shows a hand cursor, and a completed click (the same down-then-up-
    // inside gesture Control::onClick() already detects) fires
    // onLinkClicked() specifically, in addition to onClick() itself
    // (which always fires regardless of hotLink()). Disabled
    // (!isEnabled()) always shows DisabledText and never recolors on
    // hover, regardless of hotLink() - matches Control's own "no clicks
    // while disabled" behavior extending to "no hyperlink invitation
    // either".
    class Label : public Control {
    public:
        Label();
        virtual ~Label() {}

        const std::string& text() const { return text_; }
        void setText(const std::string& text);

        bool isHotLink() const { return hotLink_; }
        void setHotLink(bool value);

        // Defaults: UIColorRole::ControlText/LinkText/LinkHoverText
        // (uicolormanager.h) - all three already track Windows' OS-level
        // Light/Dark mode setting, so a caller only needs these setters
        // for a genuinely custom palette, not just to support dark mode.
        void setTextColor(BLRgba32 color);
        void setLinkColor(BLRgba32 color);
        void setHoveredLinkColor(BLRgba32 color);

        typedef Delegate<Label> LinkClickedDelegate;
        LinkClickedDelegate onLinkClicked;

    private:
        void updateTextColor();
        SyncReturn handleMouseEntered(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseLeft(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleStateChanged(Control& sender);
        SyncReturn handleClicked(Control& sender);

        std::string text_;
        bool hotLink_ = false;
        bool hovering_ = false;
        BLVar textColor_;
        BLVar linkColor_;
        BLVar hoveredLinkColor_;
        LabelStyle* labelStyle_ = nullptr;
    };

    // A read-only status Control - not really interactive (still inherits
    // Control's click tracking, same as a real Win32 progress bar just
    // ignores clicks rather than this class specially suppressing them),
    // just "represents something the user interacts with, or displays
    // information" per Control's own class comment, the "displays
    // information" half.
    //
    // Built the same two-part way a real Win32 progress bar (and this
    // toolkit's own ThemedTrackbarTrackStyle/ThemedTrackbarThumbStyle
    // pairing) already is: this Control's own style() is
    // ThemedProgressBarTrackStyle (the background/track, sized to the
    // whole control), and it owns one child SubView, fill() - styled with
    // ThemedProgressBarFillStyle - whose bounds are kept proportional to
    // value() automatically, so a caller never positions fill() by hand
    // (see updateFillBounds()). Matches ThemedProgressBarFillStyle's own
    // doc comment: "how much of the bar is filled isn't a field there -
    // that's the caller's job via ordinary bounds" - this class is that
    // caller.
    class Progress : public Control {
    public:
        Progress();
        virtual ~Progress() {}

        typedef Delegate<Progress> ValueChangedDelegate;

        // Fired whenever value() actually changes (setValue() clamps and
        // no-ops if the clamped result is unchanged).
        ValueChangedDelegate onValueChanged;

        // 0.0 (empty) - 1.0 (full). Clamped on the way in.
        float value() const { return value_; }
        void setValue(float newValue);

        bool isHorizontal() const { return horizontal_; }
        void setHorizontal(bool value);

        // PBFS_NORMAL/ERROR/PAUSED - see ThemedProgressBarFillStyle::FillState.
        ThemedProgressBarFillStyle::FillState fillState() const { return fillStyle_->state; }
        void setFillState(ThemedProgressBarFillStyle::FillState state) { fillStyle_->state = state; style().markDirty(); }

        // The child SubView representing the filled portion - exposed
        // read-only in case a caller wants to reach past this class's own
        // ThemedProgressBarFillStyle defaults (e.g. setStyle() a custom
        // one), though normal use never needs to touch this directly.
        SubView* fill() const { return fill_; }

    private:
        void updateFillBounds();
        SyncReturn handleSizeChanged(View& sender, const Size& size);

        float value_ = 0.0f;
        bool horizontal_ = true;
        SubView* fill_ = nullptr;
        ThemedProgressBarTrackStyle* trackStyle_ = nullptr;
        ThemedProgressBarFillStyle* fillStyle_ = nullptr;
    };

    // A draggable value control - native trackbar groove
    // (ThemedTrackbarTrackStyle, this Control's own style()) plus a
    // draggable thumb child (thumb(), ThemedTrackbarThumbStyle) kept
    // proportional to value() the same two-part way Progress's fill()
    // already is (see its own class comment) - except here the caller
    // (the user, via mouse drag) drives value() instead of this class's
    // own setValue() being the only way it changes.
    //
    // Vertical orientation puts maxValue() at the *top*, matching
    // Progress::updateFillBounds()'s own "fills from the bottom up"
    // convention for consistency between the two.
    //
    // isInteger(true) rounds value() to the nearest whole number (both on
    // setValue() and mid-drag) - independent of minValue()/maxValue(),
    // which stay plain floats either way (an integer-mode Slider from 0
    // to 10 is exactly as valid as one from 0.0 to 1.0, just always
    // landing on whole numbers within that range).
    //
    // Deliberately draws no text of its own (no value label, no format
    // string) - a caller wanting to show the current value pairs this
    // with its own Label, refreshed from onValueChanged(), the same way
    // controls1.cpp's demo already pairs a Progress with a caption Label.
    class Slider : public Control {
    public:
        Slider();
        virtual ~Slider() {}

        typedef Delegate<Slider> ValueChangedDelegate;
        // Fired whenever value() actually changes (setValue() rounds -
        // if isInteger() - then clamps, and no-ops if the result is
        // unchanged) - both from a drag and from a direct setValue()
        // call.
        ValueChangedDelegate onValueChanged;

        float value() const { return value_; }
        void setValue(float value);

        float minValue() const { return min_; }
        float maxValue() const { return max_; }
        // Re-clamps the current value() into the new range immediately -
        // onValueChanged() fires if that actually changes it.
        void setRange(float minValue, float maxValue);

        bool isInteger() const { return integer_; }
        void setInteger(bool value);

        // 0 (the default) means continuous - no snapping, value() can
        // land anywhere in [minValue(), maxValue()] (subject to
        // isInteger() alone, if that's also set). A positive step()
        // snaps value() to the nearest multiple of step() *relative to
        // minValue()* (so e.g. minValue() 10, step() 5 lands on
        // 10/15/20/..., not 10/14/19/... - "the nearest reachable
        // stop counting from the start of the range", not from zero).
        // Applied before isInteger()'s own rounding in setValue() - the
        // two are independent knobs, not reconciled with each other, so
        // a fractional step() combined with isInteger() is the caller's
        // own responsibility to keep sensible (e.g. don't set step() to
        // 0.5 on an integer Slider and expect anything other than
        // isInteger()'s rounding to win last).
        float step() const { return step_; }
        void setStep(float value);

        bool isHorizontal() const { return horizontal_; }
        void setHorizontal(bool value);

        // The child SubView representing the draggable thumb - exposed
        // read-only in case a caller wants to reach past this class's own
        // ThemedTrackbarThumbStyle defaults, though normal use never
        // needs to touch this directly.
        SubView* thumb() const { return thumb_; }

        // Off by default. ThemedTrackbarTicksStyle (viewstyle.h) reserves
        // a strip alongside the track (below for horizontal, to the right
        // for vertical - see trackRect()) and draws the actual tick lines
        // itself via Blend2D - uxtheme's own TKP_TICS/TKP_TICSVERT theme
        // asset turned out to render as a flat, featureless fill in
        // current Windows visual styles (real trackbar tick marks are
        // drawn by comctl32's own non-themed control code, not by
        // uxtheme), so relying on it alone left the strip visible but
        // blank. updateTickCount() keeps the drawn tick count in sync
        // with step()/minValue()/maxValue() (capped - see its own doc
        // comment) every time setShowTicks(true)/setStep()/setRange() run.
        bool showTicks() const { return showTicks_; }
        void setShowTicks(bool value);

        // The child SubView representing the tick-mark strip - nullptr
        // until the first setShowTicks(true) call (not created up front
        // the way thumb() always is, since most Sliders never use this).
        SubView* ticks() const { return ticks_; }

    private:
        // getClientBounds() minus the reserved tick-strip region when
        // showTicks() is true (unchanged otherwise) - what
        // updateThumbBounds()/updateValueFromLocalPoint() actually treat
        // as "the track" instead of raw getClientBounds() directly, so
        // the thumb's travel and the value-from-click mapping both stay
        // confined to the actual groove once a tick strip is reserved
        // alongside it.
        Rect trackRect() const;
        void updateThumbBounds();
        void updateTicksBounds();
        // Derives ThemedTrackbarTicksStyle::tickCount from step()/range -
        // one interval per step() if set (rounded, so a step that doesn't
        // divide the range evenly still gets a sane mark count), else a
        // fixed default. Capped at kMaxTickIntervals so a tiny step()
        // (e.g. 0.01 across [0,10]) can't paint hundreds of marks into a
        // solid smear - reported live as "too many tick marks" before
        // this cap existed. No-op if ticksStyle_ is still null (nothing
        // to update until the first setShowTicks(true)).
        void updateTickCount();
        // ThemedViewStyle::partSize() queries GetThemePartSize() using
        // whatever stateId() the style's *current* runtime fields resolve
        // to right now - for ThemedTrackbarThumbStyle that includes the
        // live pressed flag, not just the highlighted argument. Some
        // visual styles report a visibly smaller natural size for
        // TUS_PRESSED than TUS_NORMAL, so querying thumbStyle_->partSize()
        // directly from updateThumbBounds() while a drag is in progress
        // (handleDragStart() sets pressed=true first) made the thumb's
        // *layout* size shrink the instant it was clicked (reported and
        // confirmed live). resolvedThumbSize() neutralizes pressed/enabled
        // for the query so layout stays stable regardless of interaction
        // state. Deliberately not cached - see its own definition
        // (controls.cpp) for why an earlier cached version was wrong.
        Size resolvedThumbSize() const;
        Size resolvedTicksSize() const;
        // Maps a point (in this Slider's own local space - see
        // toLocalSpace() below for what handles the thumb's own local
        // space instead) to the nearest value() and applies it.
        void updateValueFromLocalPoint(const Point& localPt);
        // handleDragStart/Move/End are subscribed to *both* this
        // Slider's own onMouseDown/onMouseMove/onMouseUp and thumb_'s -
        // hit-testing walks to the deepest SubView (View::hitTestChildren()),
        // so a drag that starts exactly on the thumb delivers its events
        // to thumb_, not this Slider, and RootView's mouse-capture then
        // keeps routing the rest of that same drag to whichever of the
        // two was actually hit first (see RootView::mouseMove()'s
        // capturedSubView_ handling) - this needs to work starting from
        // either one. pt therefore arrives in *sender's* own local space
        // (this Slider's if the drag started on empty track, thumb_'s if
        // it started on the thumb itself) - toLocalSpace() converts
        // either case into this Slider's own local space uniformly
        // before updateValueFromLocalPoint() does the actual mapping.
        Point toLocalSpace(View& sender, const Point& pt) const;

        SyncReturn handleSizeChanged(View& sender, const Size& size);
        SyncReturn handleDragStart(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleDragMove(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleDragEnd(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleStateChanged(Control& sender);

        static constexpr float kThumbSize = 14.0f;
        static constexpr float kTicksSize = 8.0f;
        static constexpr int kDefaultTickIntervals = 10;
        static constexpr int kMaxTickIntervals = 20;

        float value_ = 0.0f;
        float min_ = 0.0f;
        float max_ = 100.0f;
        float step_ = 0.0f;
        bool integer_ = false;
        bool horizontal_ = true;
        bool dragging_ = false;
        bool showTicks_ = false;
        SubView* thumb_ = nullptr;
        SubView* ticks_ = nullptr;
        ThemedTrackbarTrackStyle* trackStyle_ = nullptr;
        ThemedTrackbarThumbStyle* thumbStyle_ = nullptr;
        ThemedTrackbarTicksStyle* ticksStyle_ = nullptr;
    };



    class TextCaret {

    };

    class TextSelection {
    public:
        static const size_t Invalid = (size_t)-1;

        class Range {
            public:

            size_t rowStart = Invalid;
            size_t colStart = Invalid;

            size_t rowEnd = Invalid;
            size_t colEnd = Invalid;
        };

        size_t start = Invalid;
        size_t end = Invalid;

        Range range;

        void draw(BLContext& ctx);
    };


    //single line text editing control
    class EditControl : public Control {

    };

    //multiline line text editing control
    class TextControl : public Control {

    };
}

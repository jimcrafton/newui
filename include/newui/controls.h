#pragma once

#include <chrono>
#include <cstdint>
#include <memory>

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/subview.h>
#include <newui/text.h>
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

    // A real, interactive scrollbar (SCROLLBAR/SBP_ARROWBTN +
    // SBP_THUMBBTNHORZ|VERT + SBP_*TRACK*, via ThemedScrollbarArrowStyle/
    // ThemedScrollbarThumbStyle/ThemedScrollbarTrackStyle - viewstyle.h,
    // previously only ever hand-wired as loose demo SubViews, never a real
    // control). value()/minValue()/maxValue()/setRange() borrow Slider's
    // own shape (above), but two things are genuinely different, not just
    // relabeled:
    //  - value()/minValue()/maxValue() describe a *viewport into a larger
    //    range*, not a single point on a track - pageSize() is how much of
    //    [minValue(),maxValue()] is visible at once, so the effective
    //    scrollable position range is [minValue(), maxValue()-pageSize()]
    //    and the thumb is sized proportionally to pageSize()/(maxValue()-
    //    minValue()), not fixed-size like Slider's thumb.
    //  - two arrow buttons step by lineStep() on click, and a click
    //    anywhere on the empty track pages by pageSize() toward the click
    //    side. Both auto-repeat while held (see startRepeat() below).
    //
    // Unlike Slider, the arrows/thumb aren't separate child SubViews - see
    // this class's own doc comment just above private: for why (a real,
    // live-diagnosed bug with that shape, not a style preference).
    //
    // See ScrollView for the composite that actually wires a pair of these
    // to a scrollable content view's origin() - a bare ScrollBar is just
    // the generic range control, same "usable standalone, not just as
    // scrolling plumbing" spirit as Slider.
    // A single View, not four - up/down arrows and the thumb are plain
    // Rects this ScrollBar tracks and hit-tests itself (regionAt()) rather
    // than separate child SubViews, each drawn by calling its own
    // Themed*Style::paint() directly (they still exist, just as plain
    // owned style objects - never attached to a SubView via setStyle())
    // translated to its own rect from this ScrollBar's own paint()
    // override. Two real problems with an earlier per-part-SubView
    // version drove this:
    //  - each part's own ThemedViewStyle::computeClientBounds() queries
    //    GetThemeBackgroundContentRect() for SBP_ARROWBTN/THUMBBTN* -
    //    fine for a part sized like a real scrollbar, but a query against
    //    a *cross-axis*-thin box (this ScrollBar's own short axis, e.g. a
    //    horizontal bar's ~16px height) came back with a content-rect
    //    deflation large enough to collapse arrows/thumb to a sliver -
    //    confirmed live (screenshot showed arrows/thumb squashed to a
    //    couple of pixels while the track background painted at full
    //    size, since *that* still uses the un-deflated bounds_.size()).
    //    A single View sidesteps the whole question - regionAt() and
    //    updateChildBounds() below work in this ScrollBar's own plain
    //    bounds, never asking uxtheme for a content rect at all.
    //  - four independent SubViews meant four independent
    //    RootView::hoveredSubView_ targets, each getting its own
    //    isHighlighted() independently - hovering across arrow/thumb/
    //    track read as hopping between separate controls rather than one
    //    cohesive scrollbar. A real scrollbar highlights as a unit -
    //    thumb and both arrows all read "hot" together the instant the
    //    cursor is anywhere over the control, not per-region - which a
    //    single View gets for free: isHighlighted() (View's own, driven
    //    by RootView::updateHoveredSubView() same as any other View) is
    //    passed as every sub-part's own "highlighted" paint() argument
    //    below, uniformly. regionAt() still exists and still matters -
    //    which region a *click* landed in still needs to be known,
    //    for setValue() and startRepeat() to do the right thing - it's
    //    only the hover *highlight* that's unified, not hit-testing.
    class ScrollBar : public Control {
    public:
        ScrollBar();
        ~ScrollBar() override;

        typedef Delegate<ScrollBar> ValueChangedDelegate;
        ValueChangedDelegate onValueChanged;

        float value() const { return value_; }
        void setValue(float value);

        float minValue() const { return min_; }
        float maxValue() const { return max_; }
        // Re-clamps the current value() (and pageSize(), if it no longer
        // fits) into the new range immediately.
        void setRange(float minValue, float maxValue);

        // How much of [minValue(),maxValue()] is visible at once - drives
        // both the thumb's proportional size and how far a track-click
        // pages. Clamped to (0, maxValue()-minValue()] - a zero or
        // negative pageSize() would make the effective scrollable range
        // and the thumb's own size both meaningless.
        float pageSize() const { return pageSize_; }
        void setPageSize(float pageSize);

        // How far one arrow click moves value(). Clamped to > 0 - see
        // pageSize()'s own reasoning.
        float lineStep() const { return lineStep_; }
        void setLineStep(float lineStep);

        bool isHorizontal() const { return horizontal_; }
        void setHorizontal(bool value);

        void paint(BLContext& ctx) override;

    private:
        enum class Region { None, UpArrow, DownArrow, Thumb, TrackBefore, TrackAfter };

        // Which of upArrowRect_/downArrowRect_/thumbRect_/neither a local
        // point falls in - TrackBefore/TrackAfter is empty track on the
        // near/far side of the thumb (main-axis position relative to
        // thumbRect_, not just "not the thumb/an arrow") since a track
        // click needs to know which direction to page.
        Region regionAt(const Point& localPt) const;

        // This ScrollBar's own plain bounds as a zero-origin Rect - what
        // updateChildBounds()/regionAt() work in, deliberately *not*
        // getClientBounds() - see this class's own doc comment above for
        // why.
        Rect localRect() const;
        // localRect() minus both arrow buttons' reserved length along the
        // main axis - what updateChildBounds()/updateValueFromLocalPoint()/
        // pageTowardLocalPoint() all treat as "the track".
        Rect trackRect() const;
        // Natural arrow-button size, queried the same "neutralize
        // pressed/enabled before asking" way Slider::resolvedThumbSize()
        // does and for the same reason (stable layout size regardless of
        // live interaction state).
        Size resolvedArrowSize() const;
        // Track length (main axis) the thumb has to travel in, and the
        // thumb's own proportional length along it - factored out since
        // both updateChildBounds() and updateValueFromLocalPoint() need
        // them kept in exact agreement (a click has to map to the value
        // that would actually put the thumb there).
        float resolvedThumbLength(float trackLength) const;
        // Recomputes upArrowRect_/downArrowRect_/thumbRect_ - called
        // whenever this ScrollBar's own bounds or anything value_/min_/
        // max_/pageSize_/horizontal_ depends on changes.
        void updateChildBounds();
        void updateValueFromLocalPoint(const Point& localPt);
        // pageSize() applied in whichever direction localPt falls on the
        // far side of thumbRect_ from - the direction a real track click
        // pages toward.
        void pageTowardLocalPoint(const Point& localPt);

        // amount is signed (already includes direction) - added to
        // value() again on every qualifying repeat tick (the *first* step
        // is the caller's own immediate setValue() call, not this)
        // until stopRepeat() (mouse released, disabled, or - for a
        // track-click repeat only - the thumb has reached trackClickPt_,
        // matching real Win32 auto-paging behavior that stops once the
        // thumb catches up to the cursor without needing a release).
        // Queues one postIdle task, guarded by aliveFlag_ the same way
        // RootView::scheduleRepaint() guards its own postIdle task
        // against outliving *this - safe to call again mid-repeat (e.g.
        // never actually happens today since mouse capture serializes
        // press/release, but doing so would just queue a second task
        // alongside the first rather than corrupt state).
        void startRepeat(float amount, bool isTrackRepeat, const Point& trackClickPt);
        void stopRepeat();

        SyncReturn handleSizeChanged(View& sender, const Size& size);
        SyncReturn handleMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseMove(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseUp(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleStateChanged(Control& sender);

        static constexpr float kArrowFallbackSize = 16.0f;
        static constexpr float kMinThumbLength = 16.0f;
        static constexpr std::chrono::milliseconds kRepeatInitialDelay{400};
        static constexpr std::chrono::milliseconds kRepeatInterval{60};

        float value_ = 0.0f;
        float min_ = 0.0f;
        float max_ = 100.0f;
        float pageSize_ = 10.0f;
        float lineStep_ = 1.0f;
        bool horizontal_ = false;
        bool dragging_ = false;

        bool repeating_ = false;
        bool trackRepeat_ = false;
        float repeatAmount_ = 0.0f;
        Point trackClickPt_;
        std::chrono::steady_clock::time_point repeatNextTime_;

        // See startRepeat()'s own doc comment - flips false in ~ScrollBar()
        // so a repeat task still queued/running past this control's
        // destruction becomes a safe no-op instead of touching a dangling
        // this.
        std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

        Rect upArrowRect_;
        Rect downArrowRect_;
        Rect thumbRect_;

        ThemedScrollbarTrackStyle* trackStyle_ = nullptr;  // this ScrollBar's own style() - owned there, not here
        std::unique_ptr<ThemedScrollbarThumbStyle> thumbStyle_;
        std::unique_ptr<ThemedScrollbarArrowStyle> upArrowStyle_;
        std::unique_ptr<ThemedScrollbarArrowStyle> downArrowStyle_;
    };

    // A native up/down spin-button pair (SPIN/SPNP_UP + SPNP_DOWN, via
    // ThemedSpinButtonStyle) that increments/decrements a numeric value()
    // by step() - called "Stepper" rather than the Win32 name "spin
    // button"/"up-down control", since that's what most callers actually
    // mean by it (matches macOS's NSStepper for the same shape). Two
    // stacked arrows and nothing else - own style() is left as the
    // default plain (invisible) ViewStyle, since a real up-down control
    // has no background chrome of its own around the two buttons. Same
    // single-View, hand-rolled hit-testing shape ScrollBar's own arrows
    // already use, and for the same reasons (see ScrollBar's own class
    // comment) - a themed content-rect query against a tiny box and
    // independent per-part hover are both real, previously-diagnosed
    // bugs that shape avoids by construction. Always vertical (up on
    // top, down on bottom) - SPIN has no horizontal counterpart the way
    // SCROLLBAR does.
    //
    // Pair with a sibling EditControl/Label showing the live value() the
    // same way a caller is expected to pair Slider with its own value
    // display (see its class comment) - Stepper draws no text of its own.
    class Stepper : public Control {
    public:
        Stepper();
        ~Stepper() override;

        typedef Delegate<Stepper> ValueChangedDelegate;
        // Fired whenever value() actually changes (setValue() clamps and
        // no-ops if the clamped result is unchanged) - both from a
        // click/repeat and from a direct setValue() call.
        ValueChangedDelegate onValueChanged;

        float value() const { return value_; }
        void setValue(float value);

        float minValue() const { return min_; }
        float maxValue() const { return max_; }
        // Re-clamps the current value() into the new range immediately -
        // onValueChanged() fires if that actually changes it.
        void setRange(float minValue, float maxValue);

        // How much one click (or one auto-repeat tick while held) moves
        // value(). Clamped to > 0.
        float step() const { return step_; }
        void setStep(float step);

        void paint(BLContext& ctx) override;

    private:
        enum class Region { None, Up, Down };
        Region regionAt(const Point& localPt) const;
        // The top/bottom half of this Stepper's own bounds - no theme
        // query needed (unlike ScrollBar::resolvedArrowSize()) since
        // these two arrows always fill the whole control between them,
        // rather than reserving natural-sized room at the ends of a
        // separate track.
        Rect upRect() const;
        Rect downRect() const;

        // amount is signed (already includes direction) - added to
        // value() again on every qualifying repeat tick (the *first*
        // step is the caller's own immediate setValue() call in
        // handleMouseDown(), not this), same convention/timing constants
        // ScrollBar::startRepeat() already uses.
        void startRepeat(float amount);
        void stopRepeat();

        SyncReturn handleMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseMove(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseUp(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseLeft(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseEnter(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleStateChanged(Control& sender);

        static constexpr std::chrono::milliseconds kRepeatInitialDelay{400};
        static constexpr std::chrono::milliseconds kRepeatInterval{60};

        float value_ = 0.0f;
        float min_ = 0.0f;
        float max_ = 100.0f;
        float step_ = 1.0f;

        // Which half the cursor is actually over right now - drives each
        // arrow's own independent HOT look in paint(). Deliberately NOT
        // the whole-Control isHighlighted() ScrollBar's own arrows share
        // (see this class's own doc comment on why that shape doesn't fit
        // here): up/down are two spatially separate buttons, not one
        // draggable unit, so a single shared hot flag lit *both* arrows
        // together the instant the cursor entered either half - including
        // the one NOT under the cursor, which read as "the wrong button
        // is highlighted" (confirmed live: clicking the top arrow
        // correctly incremented value() while the bottom arrow lit up
        // HOT, since it never got pressed=true and this shared flag was
        // still true for it too). pressed (per-style, already correct)
        // still wins over this in ThemedSpinButtonStyle::stateId(), so
        // this only ever matters for the un-pressed idle/hover look.
        Region hoverRegion_ = Region::None;

        bool repeating_ = false;
        float repeatAmount_ = 0.0f;
        std::chrono::steady_clock::time_point repeatNextTime_;

        // See startRepeat()'s own doc comment - flips false in
        // ~Stepper() so a repeat task still queued/running past this
        // control's destruction becomes a safe no-op instead of
        // touching a dangling this - same convention ScrollBar's own
        // aliveFlag_ already uses.
        std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);

        std::unique_ptr<ThemedSpinButtonStyle> upStyle_;
        std::unique_ptr<ThemedSpinButtonStyle> downStyle_;
    };

    // A scrollable container - bundles a content viewport with a vertical
    // and/or horizontal ScrollBar and mouse-wheel support, wired together
    // automatically. Real content goes in via the ordinary addChild()/
    // removeChild() (overridden below to redirect into viewport_ instead
    // of this ScrollView's own child list - see viewport_'s own doc
    // comment for why) - nothing about using a ScrollView looks different
    // from using a plain SubView as a container, it just also scrolls.
    //
    // contentSize() is caller-declared, not auto-measured from viewport_'s
    // children - this toolkit has no intrinsic-content-size measurement
    // (a Layout arranges children into whatever bounds it's given, it
    // doesn't report how much room its content would *like*), and
    // guessing from children's current bounds would silently misbehave
    // for any content that uses its own internal Layout. Same "give
    // hooks, not policy" spirit as DocumentController not dictating
    // unsaved-changes UI - the caller (who actually knows their content's
    // real size) sets it explicitly, once, whenever it changes.
    class ScrollView : public Control {
    public:
        ScrollView();
        ~ScrollView() override {}

        void addChild(SubView* child) override;
        void removeChild(SubView* child) override;

        const Size& contentSize() const { return contentSize_; }
        void setContentSize(const Size& size);

        // How many lineStep()s of vBar() one mouse-wheel notch scrolls -
        // see handleMouseWheel(). Default 3, the same convention most
        // desktop toolkits use for "one notch" (Windows' own
        // SPI_GETWHEELSCROLLLINES default).
        int wheelLines() const { return wheelLines_; }
        void setWheelLines(int lines) { wheelLines_ = lines; }

        ScrollBar* vBar() const { return vBar_; }
        ScrollBar* hBar() const { return hBar_; }

        // Current scroll position - viewport_'s own origin() (view.h),
        // exposed read-only since viewport_ itself isn't (it's internal
        // chrome, not something a caller should add/remove children
        // through directly - see viewport_'s own doc comment). Always
        // matches (hBar()->value(), vBar()->value()) for whichever of the
        // two is currently visible() (0 on the axis the other has no
        // corresponding scrolling need on).
        Point contentOrigin() const;

    private:
        // Recomputes which of vBar_/hBar_ are needed for the current
        // contentSize_ vs. this ScrollView's own bounds, each visible
        // bar's range()/pageSize(), and viewport_'s bounds (client area
        // minus whichever bar(s) end up reserved) - called from both
        // handleSizeChanged() and setContentSize(), the two things that
        // can change the answer.
        void updateLayout();

        SyncReturn handleSizeChanged(View& sender, const Size& size);
        SyncReturn handleVBarValueChanged(ScrollBar& sender);
        SyncReturn handleHBarValueChanged(ScrollBar& sender);
        SyncReturn handleMouseWheel(View& sender, const Point& pt, float delta);

        Size contentSize_;
        int wheelLines_ = 3;

        // Real content lives here, not directly under this ScrollView -
        // origin() (View's own scroll-offset primitive - see its doc
        // comment, view.h) lives on viewport_, not on this ScrollView
        // itself, so vBar_/hBar_ (this ScrollView's OWN direct children,
        // its always-visible chrome) stay pinned in place regardless of
        // scroll position instead of scrolling along with the content
        // they control.
        SubView* viewport_ = nullptr;
        ScrollBar* vBar_ = nullptr;
        ScrollBar* hBar_ = nullptr;
    };

    class Image : public Control {
    public:
        Image();
        virtual ~Image() {}

        typedef Delegate<Image, const std::string&> ImagePathChanged;


        ImagePathChanged onImagePathChanged;

        std::string imagePath() const { return imagePath_; }
        void setImagePath(const std::string& val);


    private:
        std::string imagePath_;
        SyncReturn updateImage(Image&, const std::string& newPath);
    };

    // A native toolbar button (TOOLBAR/TP_BUTTON, via
    // ThemedToolbarButtonStyle) - same "own text drawn on top of native
    // chrome, no child SubView" shape as Button (see its own class
    // comment for why hit-testing requires that), and the same
    // momentary-vs-toggle distinction (setToggleButton()/isChecked()/
    // onCheckedChanged) - just a different, more compact native part
    // meant to sit inside a Toolbar rather than stand alone (e.g. a
    // toolbar Bold/Italic button that stays visually pressed while
    // isChecked() is true). A Control, not a bare SubView, for the same
    // reason Button is: the toggle behavior needs Control's own click-
    // tracking (onMouseDown/onMouseUp -> onClick on a down-then-up-
    // inside gesture), not something worth reimplementing here.
    class ToolbarButton : public Control {
    public:
        ToolbarButton();
        virtual ~ToolbarButton() {}

        const std::string& text() const { return text_; }
        void setText(const std::string& text);

        void setTextColor(BLRgba32 color);

        bool isToggleButton() const { return isToggleButton_; }
        void setToggleButton(bool value) { isToggleButton_ = value; }

        bool isChecked() const { return checked_; }
        void setChecked(bool value);

        typedef Delegate<ToolbarButton> CheckedChangedDelegate;
        // Fired whenever isChecked() actually changes - both from a
        // completed toggle click and from a direct setChecked() call.
        CheckedChangedDelegate onCheckedChanged;

        void paint(BLContext& ctx) override;

    private:
        void updatePressedVisual();

        SyncReturn handlePressStart(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handlePressEnd(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleClicked(Control& sender);

        std::string text_;
        BLVar textColor_;
        bool isToggleButton_ = false;
        bool checked_ = false;
        bool pressing_ = false;
        ThemedToolbarButtonStyle* buttonStyle_ = nullptr;
    };

    // A toolbar separator (TOOLBAR/TP_SEPARATOR or TP_SEPARATORVERT, via
    // ThemedToolbarSeparatorStyle) - a thin, non-interactive divider
    // between groups of ToolbarButtons. Unlike ToolbarButton, a plain
    // SubView rather than a Control - there's no click/toggle/state
    // behavior here to inherit, just a themed background. isHorizontal()
    // should normally match the owning Toolbar's own orientation() (true
    // for a horizontal toolbar, whose separator is drawn as a vertical
    // dividing line - see ThemedToolbarSeparatorStyle's own doc comment);
    // Toolbar's addChild() doesn't enforce this since a ToolbarSeparator
    // is just an ordinary child from Toolbar's point of view.
    class ToolbarSeparator : public SubView {
    public:
        ToolbarSeparator();
        virtual ~ToolbarSeparator() {}

        bool isHorizontal() const { return horizontal_; }
        void setHorizontal(bool value);

    private:
        bool horizontal_ = true;
        ThemedToolbarSeparatorStyle* separatorStyle_ = nullptr;
    };

    // A horizontal (or vertical) strip container for ToolbarButton/
    // ToolbarSeparator children - FlexLayout-based, the same "own
    // ThemedViewStyle background + FlexLayout arranges the children"
    // shape as MenuBar (menus.h), but plain addChild()/removeChild()
    // rather than MenuBar's own setMenuItems(): a toolbar's items are
    // already real, independently useful Controls the caller constructs
    // directly, not synthesized from a lightweight MenuItem-style data
    // model, so there's nothing for Toolbar itself to own or build.
    // Background is ThemedRebarBandStyle (REBAR/RP_BAND) - the same
    // native chrome a real toolbar sits inside of when hosted in a
    // rebar control.
    class Toolbar : public SubView {
    public:
        explicit Toolbar(Orientation orientation = Orientation::Horizontal);
        virtual ~Toolbar() {}

        Orientation orientation() const;
        void setOrientation(Orientation orientation);
    };

    // The shared "how a text-editing Control behaves" logic behind both
    // TextField (single-line, below) and TextControl (multi-line,
    // further below) - owns exactly the state text.h provides for
    // editing (TextModel/TextSelection/Caret/TextInputTraits) plus the
    // DirectWrite bridge (TextRenderer/TextLayoutEngine) and every
    // mouse/keyboard input handler, so neither owning Control has to
    // duplicate any of it. A member, not a shared base class TextField/
    // TextControl would inherit from - matches this toolkit's existing
    // Control/Controller split (see Controller's own class comment,
    // controllers.h): "a data-driven Control is expected to own a
    // Controller as a member, not inherit from it." Not itself a
    // Controller subclass - Controller is Model-observation-only per its
    // own doc comment, and this class needs real View-side
    // responsibilities Controller doesn't have (repainting its owner,
    // translating input coordinates into the owner's own local space) -
    // but named and composed the same way, for the same reason:
    // presentation (the owning Control - bounds, style, being a SubView
    // at all) and text-editing behavior (this class) stay separate
    // collaborators rather than one class doing both jobs.
    //
    // Holds a non-owning Control& back-reference purely to call
    // style().markDirty() and read getClientBounds() - the same "the
    // owner outlives what it's given to" convention Caret::runLoop_
    // already uses - never reaches into anything else on it.
    //
    // setMultiline(true) (TextControl's own constructor) changes exactly
    // three behaviors from the TextField (single-line) default: Enter
    // inserts a newline into model_ instead of being ignored, Up/Down
    // arrow navigate between visual lines (a harmless no-op either way
    // on a genuinely single-line control - there's nowhere else to go),
    // and a vertical ScrollBar (vScrollBar_) can appear when content
    // grows taller than the owner's own bounds. Every other behavior -
    // click/drag/multi-click selection, Shift+Arrow extension, Home/End
    // (already scoped to the *current visual line* via
    // TextLayoutEngine::lineRange(), not the whole document, so it's
    // correct unchanged for both), Backspace/Delete, traits_ enforcement -
    // is identical regardless of this flag.
    //
    // Scrolling is deliberately internal (vScrollBar_ + scrollOffsetY_,
    // both entirely private to this class), not "size this control to
    // its full content height and let a caller's own ScrollView scroll
    // it" - the owner's own bounds/clientBounds stay small and fixed
    // either way, which is what keeps TextRenderer's underlying D2D/WIC
    // render target bounded to that same small size regardless of how
    // long the actual document is (see TextRenderer::render()'s own
    // scrollOffsetY parameter, text.h) - sizing this control to its full
    // content height instead would mean that render target growing
    // just as large on every repaint, since there's no general way (yet)
    // for a View to learn what portion of its own bounds is actually
    // visible once clipped by an arbitrary scrolling ancestor - Blend2D
    // exposes no clip-bounds query, and D2D/WIC rendering happens in a
    // completely separate pipeline unaware of Blend2D's clip state
    // regardless. A real "here's your visible sub-rect" primitive
    // through View's own paint dispatch would help here - and would
    // help any other control whose paint() cost scales with its own
    // bounds size (e.g. a future Image displaying a large bitmap) - but
    // that's a standalone core-framework feature, not attempted here.
    class TextController {
    public:
        explicit TextController(Control& owner);

        text::TextModel& model() { return model_; }
        const text::TextModel& model() const { return model_; }

        text::TextSelection& selection() { return selection_; }
        const text::TextSelection& selection() const { return selection_; }

        text::Caret& caret() { return caret_; }
        const text::Caret& caret() const { return caret_; }

        text::TextInputTraits& inputTraits() { return traits_; }
        const text::TextInputTraits& inputTraits() const { return traits_; }

        const Font& font() const { return font_; }
        void setFont(const Font& font) { font_ = font; owner_.style().markDirty(); }

        const Color& textColor() const { return textColor_; }
        void setTextColor(const Color& color) { textColor_ = color; owner_.style().markDirty(); }

        bool isMultiline() const { return multiline_; }
        void setMultiline(bool value) { multiline_ = value; }

        // Draws (in order) selection_'s highlight, model_.text() itself
        // via text::TextRenderer, then caret_ on top - into clientBounds,
        // the same "chrome first, content on top" order
        // View::paintChildren() already runs Button/Label's own paint()
        // overrides through. layoutEngine_ is updated first (a no-op
        // unless text/font/size actually changed since the last paint())
        // and drives both selection_'s highlight rects and caret_'s own
        // screen position. A no-op if clientBounds is empty.
        void paint(BLContext& ctx, const Rect& clientBounds);

        // Every one of these mirrors the identically-named View delegate
        // (minus the View& sender parameter, which this class has no use
        // for - owner_ already identifies it) - the owning Control's own
        // constructor wires onGotFocus/onMouseDown/etc. to a thin
        // forwarding method that calls straight into these (see
        // TextField::TextField()/TextControl::TextControl()).
        SyncReturn handleGotFocus();
        SyncReturn handleLostFocus();
        SyncReturn handleMouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseMove(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleMouseUp(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        // Windows has no triple-click message of its own (WM_LBUTTONDBLCLK
        // only ever covers a *second* click) - handleMouseDown() tracks
        // clickCount_/lastClickTime_/lastClickPos_ itself (using
        // ::GetDoubleClickTime()/::GetSystemMetrics(SM_CXDOUBLECLK/
        // SM_CYDOUBLECLK), the same timing/distance Windows' own
        // double-click detection uses) to recognize a third rapid click
        // as "select everything" - see its own definition (controls.cpp).
        SyncReturn handleMouseDblClick(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        SyncReturn handleKeyPress(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode);
        // vkReturn only inserts a newline when isMultiline() - a
        // non-multiline caller (TextField) gets SyncReturn::Ignored back
        // for it instead, leaving the key entirely for that caller's own
        // onReturnPressed-style hook to react to.
        SyncReturn handleKeyDown(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode);

        // Ignored (a no-op) whenever vScrollBar_ isn't currently visible -
        // same "only scroll if there's something to scroll" gate
        // ScrollView::handleMouseWheel() already uses, and the same
        // delta convention (WM_MOUSEWHEEL's WHEEL_DELTA == 120.0f per
        // notch) - see its own definition (controls.cpp).
        SyncReturn handleMouseWheel(const Point& pt, float delta);

    private:
        // Maps a point in the owner's own local space (as delivered by
        // onMouseDown/onMouseMove/onMouseUp/onMouseDblClick) into
        // layoutEngine_'s own coordinate space, anchored at
        // owner_.getClientBounds()'s own top-left - see paint()'s own
        // ctx.translate(clientBounds.left(), clientBounds.top()) - plus
        // scrollOffsetY_, since layoutEngine_'s own coordinates are
        // always in absolute, unscrolled document space (y=0 is the very
        // top of the document, not the top of whatever's currently
        // visible).
        Point toLayoutSpace(const Point& localPt) const;

        // Moves caret_ to newOffset. extendSelection false (a plain
        // click, or Left/Right/Home/End/Up/Down without Shift) clears
        // any selection_/selectionAnchor_ first, the ordinary "just move
        // the caret" case. true (Shift+Left/Right/Home/End/Up/Down, or
        // every onMouseMove of an in-progress drag) extends selection_
        // from selectionAnchor_ instead - set to caret_'s own pre-move
        // position the first time an extend gesture starts (when
        // selectionAnchor_ is still invalid) and left alone on every
        // later call within that same gesture, so a multi-key Shift+
        // Arrow sequence or a whole drag's worth of onMouseMove calls
        // all extend from the same fixed anchor rather than from
        // wherever the caret last was. resetPreferredColumn defaults
        // true - moveCaretVertically() below passes false so a run of
        // consecutive Up/Down presses keeps targeting the same on-screen
        // column instead of drifting to whatever column each individual
        // move happened to land on (matches every real text editor).
        // Always repaints immediately - see handleGotFocus()'s own
        // comment (controls.cpp) on why that's needed here rather than
        // relying on some other event to do it.
        void moveCaret(size_t newOffset, bool extendSelection, bool resetPreferredColumn = true);

        // vkUpArrow/vkDownArrow - hit-tests one line above/below the
        // caret's current on-screen position (via two existing
        // TextLayoutEngine queries, hitTestPosition() then
        // hitTestPoint() - no new layout-engine API needed) rather than
        // walking TextStorage character-by-character, so it lands at the
        // right horizontal column even across lines of different length
        // - see its own definition (controls.cpp).
        void moveCaretVertically(bool up, bool extendSelection);

        // selection_.clear() alone doesn't reset selectionAnchor_ - every
        // caller wanting a genuinely fresh, non-mid-gesture selection
        // state goes through this instead of calling selection_.clear()
        // directly.
        void clearSelection();

        // handleMouseDblClick()'s own behavior - selects the word under
        // localPt (a contiguous run of alnum/underscore characters), or
        // just repositions the caret there with no selection if localPt
        // lands on a non-word character.
        void selectWordAt(const Point& localPt);

        // handleMouseDown()'s own triple-click behavior - see its own
        // doc comment above.
        void selectAll();

        // caret_'s blink timer flips isVisible() on its own schedule,
        // independent of any Windows message - without this, that new
        // state never reaches the screen until something else incidentally
        // repaints the owner too; confirmed live in an early TextField-
        // only version of this class (the caret only visibly blinked
        // while the mouse was moving). See Caret::onVisibilityChanged's
        // own doc comment.
        SyncReturn handleCaretVisibilityChanged(text::Caret& sender);

        // traits_ enforcement lives here, not in the key handlers above -
        // subscribing to model_'s own vetoable Before events means every
        // mutation path (typed input, a future paste, even a direct
        // programmatic setText()/insert() call) respects isReadOnly()/
        // maxLength() uniformly, not just the keyboard path. Matches this
        // toolkit's existing "veto in a Before handler" convention (see
        // TextModel::onBeforeChar/onBeforeRangeChanged's own doc comment).
        SyncReturn handleModelBeforeChar(text::TextModel& sender, size_t offset, wchar_t ch, text::CharChangeKind kind, bool& canChange);
        SyncReturn handleModelBeforeRangeChanged(text::TextModel& sender, const text::TextRange& range, const std::wstring& replacement, bool& canChange);

        // Fires whenever vScrollBar_'s value() changes (a drag, a wheel
        // notch via handleMouseWheel(), or ensureCaretVisible() below) -
        // the single place scrollOffsetY_ ever gets written, so it never
        // drifts out of sync with vScrollBar_'s own value().
        SyncReturn handleScrollBarValueChanged(ScrollBar& sender);

        // Scrolls vScrollBar_ (if visible) just far enough that caret_'s
        // own current position is back within [scrollOffsetY_,
        // scrollOffsetY_ + viewportHeight) - a no-op if it already is.
        // Called from paint() itself (before any actual drawing) rather
        // than at every individual caret_.setPosition() call site
        // (typing, Backspace/Delete, Enter, arrow keys, a click, ...) -
        // one call site instead of a dozen, and it means the correction
        // (if any) always lands in the very paint() that would otherwise
        // have drawn the caret outside the visible area, not one frame
        // later.
        void ensureCaretVisible(float viewportHeight);

        Control& owner_;
        bool multiline_ = false;

        // See this class's own doc comment on why scrolling is internal
        // to this class rather than "size the owner to its full content
        // height, let a caller's ScrollView handle it". vScrollBar_ is
        // always created (matches ScrollView's own "always create both
        // bars, toggle visible()" convention) even for a non-multiline
        // TextField, which just never shows or uses it. scrollOffsetY_
        // always mirrors vScrollBar_->value() - see
        // handleScrollBarValueChanged().
        static constexpr float kScrollBarThickness = 16.0f;
        ScrollBar* vScrollBar_ = nullptr;
        float scrollOffsetY_ = 0.0f;

        text::TextModel model_;
        text::TextSelection selection_;
        text::Caret caret_;
        text::TextInputTraits traits_;

        // Selection-gesture state - see moveCaret()'s own doc comment for
        // selectionAnchor_, and handleMouseDown()'s for the rest.
        text::TextPosition selectionAnchor_;
        bool dragging_ = false;
        int clickCount_ = 0;
        std::chrono::steady_clock::time_point lastClickTime_;
        Point lastClickPos_;

        // moveCaretVertically()'s own "remember my column across a run
        // of Up/Down presses" state - see moveCaret()'s own doc comment.
        float preferredColumnX_ = 0.0f;
        bool hasPreferredColumnX_ = false;

        Font font_;
        Color textColor_;
        text::TextRenderer renderer_;
        text::TextLayoutEngine layoutEngine_;
    };

    // A single-line text editing control - a thin View-integration shim
    // around one TextController (controller_, above), which owns every
    // bit of actual text-editing state and behavior: DirectWrite-backed
    // rendering, layout/hit-testing, selection/caret painting, and real
    // mouse/keyboard/focus input - see text-plan.md at the repo root for
    // the phase-by-phase history of how that came together. style()
    // defaults to ThemedEditStyle (EDIT/EP_EDITTEXT) for the native
    // background/border chrome, same "chrome now, real content on top"
    // split Button/Label already draw between their own native/
    // LabelStyle background and their own separately-drawn text -
    // ThemedEditStyle's own class comment (viewstyle.h) documents this
    // split explicitly ("pair with a client/subview that draws its own
    // text"). Real keyboard focus arrives via RootView::setFocusedSubView()
    // (rootview.cpp) automatically selecting whichever SubView a click
    // hit-tests to - this class only reacts to the resulting onGotFocus/
    // onLostFocus, it doesn't request focus itself.
    class TextField : public Control {
    public:
        TextField();
        virtual ~TextField() {}

        text::TextModel& model() { return controller_.model(); }
        const text::TextModel& model() const { return controller_.model(); }

        // Convenience forwarders to model() - text()/setText() are the
        // common case; reach model() directly for insert()/remove()/
        // replace(), or to subscribe to its onBeforeChar/onAfterChar/
        // onBeforeRangeChanged/onAfterRangeChanged/onChanged events.
        const std::wstring& text() const { return controller_.model().text(); }
        void setText(const std::wstring& text) { controller_.model().setText(text); }

        text::TextSelection& selection() { return controller_.selection(); }
        const text::TextSelection& selection() const { return controller_.selection(); }

        text::Caret& caret() { return controller_.caret(); }
        const text::Caret& caret() const { return controller_.caret(); }

        text::TextInputTraits& inputTraits() { return controller_.inputTraits(); }
        const text::TextInputTraits& inputTraits() const { return controller_.inputTraits(); }

        const Font& font() const { return controller_.font(); }
        void setFont(const Font& font) { controller_.setFont(font); }

        const Color& textColor() const { return controller_.textColor(); }
        void setTextColor(const Color& color) { controller_.setTextColor(color); }

        typedef Delegate<TextField> ReturnPressedDelegate;
        // Fired when Enter/Return is pressed while this TextField has
        // focus - this class's own equivalent of UIKit's
        // UITextFieldDelegate.textField(_:shouldReturn:), minus the
        // veto: TextField has no built-in "submit" action of its own to
        // gate, so this is purely a notification hook for the owning app
        // to react to (trigger a search, submit a form, ...). Never
        // inserts a newline into model() - that's TextControl's own
        // behavior for the same key.
        ReturnPressedDelegate onReturnPressed;

        // Draws into getClientBounds(), on top of whatever paintStyle()
        // already drew for editStyle_'s own native background/border -
        // see TextController::paint()'s own doc comment for the actual
        // drawing order.
        void paint(BLContext& ctx) override { controller_.paint(ctx, getClientBounds()); }

    private:
        // Phase 5 - Win32 message loop interop. RootView already routes
        // real keyboard/mouse/focus messages down to whichever SubView
        // is focusedSubView_/hit-tested (see RootView::keyEvent()/
        // mouseDown(), rootview.cpp) - these just subscribe to the
        // View-level delegates that dispatch already reaches (the same
        // way every other Control in this file wires its own onMouseDown/
        // onMouseUp, see Control::Control()) and forward straight into
        // controller_. handleKeyDown() is the one with real logic of its
        // own here (not just a forward) - vkReturn fires onReturnPressed
        // instead of going to controller_ at all, since TextController
        // only knows "insert a newline or ignore it", not "notify the
        // owner" (see ReturnPressedDelegate's own doc comment above).
        SyncReturn handleGotFocus(View& sender) { return controller_.handleGotFocus(); }
        SyncReturn handleLostFocus(View& sender) { return controller_.handleLostFocus(); }
        SyncReturn handleMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseDown(pt, btnMask, keyMask); }
        SyncReturn handleMouseMove(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseMove(pt, btnMask, keyMask); }
        SyncReturn handleMouseUp(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseUp(pt, btnMask, keyMask); }
        SyncReturn handleMouseDblClick(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseDblClick(pt, btnMask, keyMask); }
        SyncReturn handleKeyPress(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) { return controller_.handleKeyPress(keyMask, keyCharVal, repeatCount, VKeyCode); }
        SyncReturn handleKeyDown(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode);
        SyncReturn handleMouseWheel(View& sender, const Point& pt, float delta) { return controller_.handleMouseWheel(pt, delta); }

        TextController controller_;
        ThemedEditStyle* editStyle_ = nullptr;
    };

    // A multi-line text editing control - TextField's own class comment
    // (above) covers everything shared between the two: both are thin
    // View-integration shims around one TextController, which does the
    // real work (see its own class comment for exactly what's shared vs.
    // different between single- and multi-line use). The only real
    // behavioral differences from TextField: Enter inserts a newline
    // (TextController::setMultiline(true), set in this class's own
    // constructor) instead of firing a shouldReturn-style hook, and
    // Up/Down arrow navigate between visual lines/a vertical scrollbar
    // can appear - all already handled generically by TextController
    // once multiline is set, no per-class special-casing needed here.
    class TextControl : public Control {
    public:
        TextControl();
        virtual ~TextControl() {}

        text::TextModel& model() { return controller_.model(); }
        const text::TextModel& model() const { return controller_.model(); }

        const std::wstring& text() const { return controller_.model().text(); }
        void setText(const std::wstring& text) { controller_.model().setText(text); }

        text::TextSelection& selection() { return controller_.selection(); }
        const text::TextSelection& selection() const { return controller_.selection(); }

        text::Caret& caret() { return controller_.caret(); }
        const text::Caret& caret() const { return controller_.caret(); }

        text::TextInputTraits& inputTraits() { return controller_.inputTraits(); }
        const text::TextInputTraits& inputTraits() const { return controller_.inputTraits(); }

        const Font& font() const { return controller_.font(); }
        void setFont(const Font& font) { controller_.setFont(font); }

        const Color& textColor() const { return controller_.textColor(); }
        void setTextColor(const Color& color) { controller_.setTextColor(color); }

        void paint(BLContext& ctx) override { controller_.paint(ctx, getClientBounds()); }

    private:
        SyncReturn handleGotFocus(View& sender) { return controller_.handleGotFocus(); }
        SyncReturn handleLostFocus(View& sender) { return controller_.handleLostFocus(); }
        SyncReturn handleMouseDown(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseDown(pt, btnMask, keyMask); }
        SyncReturn handleMouseMove(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseMove(pt, btnMask, keyMask); }
        SyncReturn handleMouseUp(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseUp(pt, btnMask, keyMask); }
        SyncReturn handleMouseDblClick(View& sender, const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) { return controller_.handleMouseDblClick(pt, btnMask, keyMask); }
        SyncReturn handleKeyPress(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) { return controller_.handleKeyPress(keyMask, keyCharVal, repeatCount, VKeyCode); }
        SyncReturn handleKeyDown(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) { return controller_.handleKeyDown(keyMask, keyCharVal, repeatCount, VKeyCode); }
        SyncReturn handleMouseWheel(View& sender, const Point& pt, float delta) { return controller_.handleMouseWheel(pt, delta); }

        TextController controller_;
        ThemedEditStyle* editStyle_ = nullptr;
    };
}

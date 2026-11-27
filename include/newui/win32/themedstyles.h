#pragma once

// Windows-only: the uxtheme-backed ("visual styles") ViewStyle family (ThemedViewStyle and every Themed*/Fluent*
// style derived from it). Split out of viewstyle.h so the cross-platform header has no uxtheme/<windows.h> dependency.

#include "newui/viewstyle.h"
#include "newui/win32/winheaders.h"
#include <uxtheme.h>
#include <vssym32.h>

namespace newui {

    // Base for a second family of styles alongside ViewStyle/ButtonStyle/
    // CheckBoxStyle above - instead of hand-drawing chrome with Blend2D,
    // these ask Windows itself to draw a real native "visual styles"
    // control via uxtheme.dll, so a themed View looks exactly like a real
    // Win32 control under whatever visual style is active.
    //
    // uxtheme's DrawThemeBackground() draws into a GDI HDC, not a
    // BLContext - paint() (defined in viewstyle.cpp) bridges the two via
    // the modern "buffered paint" pattern (BeginBufferedPaint/
    // DrawThemeBackground/GetBufferedPaintBits/EndBufferedPaint), which is
    // what correctly extracts a real per-pixel alpha channel for theme
    // parts that aren't fully opaque (a hover glow, a focus highlight,
    // ...) - a raw DrawThemeBackground into a plain memory DC would
    // silently drop that. GetBufferedPaintBits() hands back top-down,
    // premultiplied-alpha BGRA pixels - exactly BL_FORMAT_PRGB32, so no
    // conversion is needed before wrapping them in a BLImage and blitting
    // into ctx. opacity is deliberately NOT applied to that blit (unlike
    // ViewStyle::paint()'s fills) - Blend2D's blit_image() has no
    // set_fill_alpha()-equivalent global-alpha knob, and a native control
    // faded via alpha isn't an obviously-wanted look; out of scope for v1.
    //
    // A subclass names the theme "class" (themeClassName, e.g. "BUTTON",
    // passed to the constructor) and which part/state to draw
    // (partId()/stateId(), pure virtual - each concrete widget knows its
    // own vssym32.h constants and how highlighted/its own state fields map
    // to them - see ThemedButtonStyle/ThemedCheckBoxStyle). The HTHEME
    // handle is opened lazily (needs a live HWND, only available once this
    // style is attached to a View with an already-initialize()'d RootView
    // - see View::rootView()/RootView::windowHandle()) and cached;
    // closeTheme() drops it so the next paint() reopens it (e.g. after a
    // WM_THEMECHANGED - not wired up automatically in v1).
    //
    // Non-copyable: owns a Win32 HTHEME handle.
    class ThemedViewStyle : public ViewStyle {
    public:
        explicit ThemedViewStyle(std::string themeClassName) : themeClassName_(std::move(themeClassName)) {}
        ~ThemedViewStyle() override;

        ThemedViewStyle(const ThemedViewStyle&) = delete;
        ThemedViewStyle& operator=(const ThemedViewStyle&) = delete;

        void closeTheme();

        // Queries GetThemeBackgroundContentRect() for the real deflation
        // this theme part's chrome needs - but only if a theme is already
        // cached (see paint()'s comment); otherwise falls back to "no
        // deflation" (matches ViewStyle's own no-border default) rather
        // than guessing. A paint-free caller (e.g. Layout::arrange()
        // before this View has ever been painted) can therefore see a
        // too-generous clientBounds the very first time - resolves itself
        // once paint() has run once and cached the theme.
        Rect computeClientBounds(const Size& size) const override;

        // Queries GetThemePartSize() (TS_TRUE - the size the current
        // visual style actually draws this part/state at, not TS_MIN's
        // smallest-legible-size or TS_DRAW's needs-a-real-rect variant)
        // for partId()/stateId(false)'s own natural size - e.g.
        // ThemedTrackbarThumbStyle's real thumb dimensions, or
        // ThemedTrackbarTicksStyle's real strip thickness, instead of a
        // guessed fixed pixel constant. Same "only if a theme is already
        // cached" fallback computeClientBounds() above already has, for
        // the same reason - returns fallback unchanged if nothing has
        // painted this style through a live HWND yet (resolves itself
        // once paint() has run once).
        Size partSize(const Size& fallback) const;

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;

    protected:
        // Which vssym32.h part/state to draw - highlighted mirrors the
        // view's isHighlighted() at paint time, same as every other
        // paint() in this file.
        virtual int partId() const = 0;
        virtual int stateId(bool highlighted) const = 0;

        // Draws this part from the loaded ThemeData's colours (fill/edgeFill, border, text) for its exact state instead of asking
        // uxtheme - what makes an app's theme file win over Windows' own look. Covers push buttons, toolbar buttons, check boxes and
        // radio buttons; returns false (and paint() goes native) for any other part, or when no theme is loaded or it has no colours
        // for this part and state.
        virtual bool paintFromThemeData(BLContext& ctx, const Size& size, bool highlighted) const;

    private:
        std::string themeClassName_;
        mutable HTHEME theme_ = nullptr;
    };

    // ThemedViewStyle plus a native "push button" (BUTTON/BP_PUSHBUTTON).
    // pressed mirrors an explicit "mouse currently down on this view"
    // signal the caller has to track itself - this toolkit has no
    // built-in press-tracking (the same convention ButtonStyle's own
    // edge-swap-on-mouse-down demo already relies on); highlighted
    // (passed into paint()) already covers hover/focus.
    class ThemedButtonStyle : public ThemedViewStyle {
    public:
        ThemedButtonStyle() : ThemedViewStyle("BUTTON") {}

        bool pressed = false;
        bool enabled = true;
        bool defaulted = false;   // the window's default button (Control::isDefaultControl()): drawn emphasised until hovered or pressed


    protected:
        int partId() const override { return BP_PUSHBUTTON; }

        int stateId(bool highlighted) const override {
            if (!enabled) return PBS_DISABLED;
            if (pressed) return PBS_PRESSED;
            if (highlighted) return PBS_HOT;
            if (defaulted) return PBS_DEFAULTED;
            return PBS_NORMAL;
        }
    };

    // Experimental: a Fluent-style rounded-corner Button chrome, custom-
    // drawn with Blend2D instead of ThemedButtonStyle's native
    // DrawThemeBackground(BP_PUSHBUTTON) - confirmed live that the native
    // path still renders the classic square-cornered look even on
    // Windows 11 (DrawThemeBackground has no rounded-corner Win11 visual
    // data to draw; that's a WinUI3/XAML-only rendering, not something
    // classic UxTheme exposes to a raw Win32 app), so matching Fluent's
    // own 4px corner-radius convention for in-page controls (see
    // learn.microsoft.com/windows/apps/design/signature-experiences/
    // geometry) means not using DrawThemeBackground for this one at all.
    //
    // Deliberately a *subclass* of ThemedButtonStyle, not a ViewStyle
    // sibling - Button (controls.h) holds `buttonStyle_` as a concrete
    // `ThemedButtonStyle*` and reads `.pressed`/`.enabled`/`.font()`/
    // `.compositingOp()`/`.opacity()` off it directly, all inherited
    // unchanged here; only paint()/computeClientBounds() are overridden,
    // to a genuinely different rendering path rather than routing back
    // through ThemedViewStyle::paint()'s theme machinery at all - no
    // uxtheme calls happen for this style, ever.
    //
    // Opt-in only, today - `Button::Button()` still defaults to
    // `ThemedButtonStyle` (the native look); a caller previews this by
    // `button->setStyle(std::make_unique<FluentButtonStyle>())` after
    // construction. Not yet decided whether this becomes Button's new
    // default - a deliberate visual-philosophy departure from this
    // codebase's usual "look exactly like a native Win32 control"
    // approach elsewhere, worth seeing live before committing to it
    // more broadly.
    class FluentButtonStyle : public ThemedButtonStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            // ViewStyle::computeClientBounds(), not ThemedViewStyle's own
            // override two levels up - that one queries
            // GetThemeBackgroundContentRect(), meaningless here since
            // paint() below never opens a theme at all. No native chrome
            // to deflate for - the 1px border below is thin enough that
            // text laid out at the full bounds still looks right.
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a native checkbox (BUTTON/BP_CHECKBOX).
    class ThemedCheckBoxStyle : public ThemedViewStyle {
    public:
        ThemedCheckBoxStyle() : ThemedViewStyle("BUTTON") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return BP_CHECKBOX; }

        int stateId(bool highlighted) const override {
            if (!enabled) return checked ? CBS_CHECKEDDISABLED : CBS_UNCHECKEDDISABLED;
            if (pressed) return checked ? CBS_CHECKEDPRESSED : CBS_UNCHECKEDPRESSED;
            if (highlighted) return checked ? CBS_CHECKEDHOT : CBS_UNCHECKEDHOT;
            return checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
        }
    };

    // Fluent-style checkbox glyph, custom-drawn instead of
    // ThemedCheckBoxStyle's native DrawThemeBackground(BP_CHECKBOX) - same
    // rationale/precedent as FluentButtonStyle above (classic UxTheme has
    // no Windows 11 visual data for this either). A subclass of
    // ThemedCheckBoxStyle, not a ViewStyle sibling, for the same reason
    // FluentButtonStyle is a ThemedButtonStyle subclass: Toggle
    // (controls.h) dynamic_casts style() to ThemedCheckBoxStyle* to push
    // checked/pressed/enabled into it (Toggle::updateStyleFields()) - this
    // stays a valid target for that cast unchanged, opted into via
    // `toggle->setStyle(std::make_unique<FluentCheckBoxStyle>())` after
    // construction (Toggle::rebuildStyle() still defaults to the native
    // ThemedCheckBoxStyle/ThemedRadioButtonStyle pair).
    //
    // Fills its whole given size (Toggle has no separate label - the
    // glyph View's own bounds *are* the whole control, same as a native
    // BP_CHECKBOX part), so a caller sizes the Toggle itself to whatever
    // glyph size they want rather than this style centering a fixed-size
    // glyph inside a larger box.
    class FluentCheckBoxStyle : public ThemedCheckBoxStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a native radio button (BUTTON/BP_RADIOBUTTON) -
    // same field/state shape as ThemedCheckBoxStyle above, just a
    // different part.
    class ThemedRadioButtonStyle : public ThemedViewStyle {
    public:
        ThemedRadioButtonStyle() : ThemedViewStyle("BUTTON") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return BP_RADIOBUTTON; }

        int stateId(bool highlighted) const override {
            if (!enabled) return checked ? RBS_CHECKEDDISABLED : RBS_UNCHECKEDDISABLED;
            if (pressed) return checked ? RBS_CHECKEDPRESSED : RBS_UNCHECKEDPRESSED;
            if (highlighted) return checked ? RBS_CHECKEDHOT : RBS_UNCHECKEDHOT;
            return checked ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL;
        }
    };

    // Fluent-style radio glyph - same rationale/opt-in shape as
    // FluentCheckBoxStyle just above (a ThemedRadioButtonStyle subclass,
    // so Toggle's own dynamic_cast<ThemedRadioButtonStyle*> still finds
    // it), just a circle instead of a rounded square.
    class FluentRadioButtonStyle : public ThemedRadioButtonStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a native group box frame (BUTTON/BP_GROUPBOX) -
    // typically drawn behind/around a set of related controls. The real
    // control also leaves a caption gap in the top border for a label,
    // which this style doesn't draw (same "chrome only, no text" split as
    // ButtonStyle not drawing a button's own label) -
    // GetThemeBackgroundContentRect() (via computeClientBounds(), see
    // ThemedViewStyle) already accounts for the frame's own border
    // thickness regardless.
    class ThemedGroupBoxStyle : public ThemedViewStyle {
    public:
        ThemedGroupBoxStyle() : ThemedViewStyle("BUTTON") {}

        bool enabled = true;


    protected:
        int partId() const override { return BP_GROUPBOX; }

        int stateId(bool /*highlighted*/) const override {
            return enabled ? GBS_NORMAL : GBS_DISABLED;
        }
    };

    // Fluent-style group box frame - custom-drawn instead of
    // ThemedGroupBoxStyle's native DrawThemeBackground(BP_GROUPBOX), same
    // ThemedGroupBoxStyle-subclass opt-in shape as every other Fluent*
    // style (GroupBox::handleStateChanged(), controls.cpp, dynamic_casts
    // style() to ThemedGroupBoxStyle&, which this still satisfies). A
    // plain rounded-rect stroke, no fill - a GroupBox is a grouping frame
    // around already-laid-out sibling content, not a clickable/elevated
    // surface, so unlike FluentCardStyle this has no background fill or
    // shadow of its own. GroupBox::paint()'s own caption backdrop/text
    // (controls.cpp) draws on top of this identically regardless of
    // which border style is active - see its own comment.
    class FluentGroupBoxStyle : public ThemedGroupBoxStyle {
    public:
        // Same "1px border is thin enough that content laid out at the
        // full bounds still looks right" call FluentButtonStyle's own
        // comment makes - a caller wanting real breathing room between
        // this frame and its own child content adds that via its own
        // Layout padding, the same way the bordered focus-scope panel in
        // examples/uiinputmanager1.cpp already does for an unrelated
        // bordered SubView.
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // A Fluent-style elevated "Card" surface - a plain rounded-rect panel
    // background/border for grouping in-page content, with a Card-level
    // drop shadow (ElevationLevel::Card above) on by default. Unlike
    // every other Fluent* style in this file, this one is a direct
    // ViewStyle subclass, not a Themed*Style subclass - there's no native
    // uxtheme part this replaces (no existing GroupBox/Card control class
    // in this codebase to stay cast-compatible with either, unlike
    // FluentButtonStyle/FluentCheckBoxStyle/etc. - see each of their own
    // comments). rectRadius()/elevation() are real, freely overridable
    // ViewStyle properties, set to sensible defaults by the constructor;
    // paint() is still overridden (rather than leaving background/border
    // painting to the inherited ViewStyle::paint()) purely so the
    // fill/border colors stay live UIColorManager::colorFor() lookups,
    // matching every other Fluent* style's own dark/light-mode-reactive
    // approach - ViewStyle::backgroundFill()/borderFill() only ever hold
    // a static snapshot color, which would go stale across a live theme
    // switch the way this codebase's other Fluent styles deliberately
    // don't.
    class FluentCardStyle : public ViewStyle {
    public:
        FluentCardStyle();

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a native toolbar button (TOOLBAR/TP_BUTTON) -
    // checked is a toggle-button state (e.g. a pressed-in "bold" button
    // in a formatting toolbar), independent of pressed (the transient
    // "mouse currently down on this button right now" state).
    class ThemedToolbarButtonStyle : public ThemedViewStyle {
    public:
        ThemedToolbarButtonStyle() : ThemedViewStyle("TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return TP_BUTTON; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TS_DISABLED;
            if (pressed) return TS_PRESSED;
            if (checked) return highlighted ? TS_HOTCHECKED : TS_CHECKED;
            if (highlighted) return TS_HOT;
            return TS_NORMAL;
        }
    };

    // Fluent-style toolbar/command-bar button - custom-drawn, no border
    // ever (matching Fluent's own flat command-bar convention - see
    // ToolbarButton::paint()'s own "a flat toolbar button shows no visible
    // border/fill at rest either way" comment, controls.cpp), just a
    // rounded-rect highlight fill that appears for hover/pressed/checked.
    // Same ThemedToolbarButtonStyle-subclass opt-in shape as
    // FluentButtonStyle/FluentCheckBoxStyle above (ToolbarButton::
    // updatePressedVisual() dynamic_casts style() to
    // ThemedToolbarButtonStyle&, which this still satisfies).
    class FluentToolbarButtonStyle : public ThemedToolbarButtonStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a native toolbar drop-down button (TOOLBAR/
    // TP_DROPDOWNBUTTON) - the chrome for a button whose entire face opens
    // a dropdown (as opposed to a split button, below, where only part of
    // the face does). Every TOOLBARPARTS part shares one state enum
    // (TOOLBARSTYLESTATES) - same checked/pressed/enabled shape and
    // precedence as ThemedToolbarButtonStyle above, just a different part.
    class ThemedToolbarDropDownButtonStyle : public ThemedViewStyle {
    public:
        ThemedToolbarDropDownButtonStyle() : ThemedViewStyle("TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return TP_DROPDOWNBUTTON; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TS_DISABLED;
            if (pressed) return TS_PRESSED;
            if (checked) return highlighted ? TS_HOTCHECKED : TS_CHECKED;
            if (highlighted) return TS_HOT;
            return TS_NORMAL;
        }
    };

    // ThemedViewStyle plus the small dropdown-arrow glyph on a plain
    // drop-down button (TOOLBAR/TP_DROPDOWNBUTTONGLYPH) - a separate part
    // from TP_DROPDOWNBUTTON itself (same "chrome + glyph drawn as two
    // Views" division as ThemedHeaderItemStyle/ThemedHeaderSortArrowStyle) -
    // shares the same TOOLBARSTYLESTATES enum, so same fields/precedence.
    class ThemedToolbarDropDownButtonGlyphStyle : public ThemedViewStyle {
    public:
        ThemedToolbarDropDownButtonGlyphStyle() : ThemedViewStyle("TOOLBAR") {}

        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return TP_DROPDOWNBUTTONGLYPH; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TS_DISABLED;
            if (pressed) return TS_PRESSED;
            if (highlighted) return TS_HOT;
            return TS_NORMAL;
        }
    };

    // ThemedViewStyle plus a native toolbar split button's main face
    // (TOOLBAR/TP_SPLITBUTTON) - the clickable-on-its-own part; pair with
    // ThemedToolbarSplitButtonDropDownStyle below for the separate
    // dropdown-arrow part next to it (two Views side by side, same
    // division as the plain drop-down button's chrome/glyph split above).
    class ThemedToolbarSplitButtonStyle : public ThemedViewStyle {
    public:
        ThemedToolbarSplitButtonStyle() : ThemedViewStyle("TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return TP_SPLITBUTTON; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TS_DISABLED;
            if (pressed) return TS_PRESSED;
            if (checked) return highlighted ? TS_HOTCHECKED : TS_CHECKED;
            if (highlighted) return TS_HOT;
            return TS_NORMAL;
        }
    };

    // ThemedViewStyle plus a native toolbar split button's own dropdown-
    // arrow part (TOOLBAR/TP_SPLITBUTTONDROPDOWN) - see
    // ThemedToolbarSplitButtonStyle above.
    class ThemedToolbarSplitButtonDropDownStyle : public ThemedViewStyle {
    public:
        ThemedToolbarSplitButtonDropDownStyle() : ThemedViewStyle("TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return TP_SPLITBUTTONDROPDOWN; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TS_DISABLED;
            if (pressed) return TS_PRESSED;
            if (checked) return highlighted ? TS_HOTCHECKED : TS_CHECKED;
            if (highlighted) return TS_HOT;
            return TS_NORMAL;
        }
    };

    // ThemedViewStyle plus a toolbar separator (TOOLBAR/TP_SEPARATOR or
    // TP_SEPARATORVERT) - horizontal picks which of the two theme parts
    // (a vertical toolbar's separator is itself a horizontal line, and
    // vice versa - same orientation-picks-the-part shape as
    // ThemedTrackbarTrackStyle). A separator doesn't respond to
    // hot/pressed/disabled in practice even though it formally shares
    // TOOLBARSTYLESTATES with every other TOOLBAR part, so stateId()
    // ignores highlighted and always returns TS_NORMAL - same "hardcode
    // the one sensible value" shape as ThemedTabPaneStyle.
    class ThemedToolbarSeparatorStyle : public ThemedViewStyle {
    public:
        ThemedToolbarSeparatorStyle() : ThemedViewStyle("TOOLBAR") {}

        bool horizontal = true;


    protected:
        int partId() const override { return horizontal ? TP_SEPARATOR : TP_SEPARATORVERT; }
        int stateId(bool /*highlighted*/) const override { return TS_NORMAL; }
    };

    // ThemedViewStyle plus a native status bar pane background (STATUS/
    // SP_PANE). This part has no state variants at all - stateId()
    // always returns 0 (same for ThemedRebarBandStyle below).
    //
    // Real, expected uxtheme behavior worth knowing if this ever looks
    // "wrong": DrawThemeBackground() draws SP_PANE as just a thin one-
    // sided divider line (typically only the right edge), not a full box
    // border - a real status bar's own background/edge already frames
    // the whole strip, and each individual pane only needs a divider
    // between itself and its neighbor. Shown as a single standalone View
    // (as in examples/layout1.cpp's sidebar demo) this reads as "only
    // has a border on one side" - that's correct/native, not a bug; it
    // looks like a normal status bar once multiple panes actually sit
    // side by side in a real strip.
    class ThemedStatusPaneStyle : public ThemedViewStyle {
    public:
        ThemedStatusPaneStyle() : ThemedViewStyle("STATUS") {}

    protected:
        int partId() const override { return SP_PANE; }
        int stateId(bool /*highlighted*/) const override { return 0; }
    };

    // ThemedViewStyle plus a native rebar band background (REBAR/
    // RP_BAND) - no state variants either, see ThemedStatusPaneStyle
    // above (same reasoning, same no-fields/no-serialization-override
    // shape).
    class ThemedRebarBandStyle : public ThemedViewStyle {
    public:
        ThemedRebarBandStyle() : ThemedViewStyle("REBAR") {}

    protected:
        int partId() const override { return RP_BAND; }
        int stateId(bool /*highlighted*/) const override { return 0; }
    };

    // ThemedViewStyle plus a rebar/toolbar overflow chevron (REBAR/
    // RP_CHEVRON or RP_CHEVRONVERT) - the "»" button that appears when a
    // toolbar's items don't all fit and opens a dropdown of the
    // overflowed ones. horizontal picks the part (same orientation-picks-
    // the-part shape as ThemedTrackbarTrackStyle/ThemedToolbarSeparatorStyle).
    // CHEVRONSTATES/CHEVRONVERTSTATES have no disabled variant (a chevron
    // that isn't needed just isn't shown at all, rather than shown
    // disabled), so there's no enabled field here, unlike the toolbar
    // button styles above.
    class ThemedRebarChevronStyle : public ThemedViewStyle {
    public:
        ThemedRebarChevronStyle() : ThemedViewStyle("REBAR") {}

        bool horizontal = true;
        bool pressed = false;


    protected:
        int partId() const override { return horizontal ? RP_CHEVRON : RP_CHEVRONVERT; }

        int stateId(bool highlighted) const override {
            if (horizontal) {
                if (pressed) return CHEVS_PRESSED;
                if (highlighted) return CHEVS_HOT;
                return CHEVS_NORMAL;
            }
            if (pressed) return CHEVSV_PRESSED;
            if (highlighted) return CHEVSV_HOT;
            return CHEVSV_NORMAL;
        }
    };

    // ThemedViewStyle plus a native tooltip balloon background (TOOLTIP/
    // TTP_STANDARD). linked selects the "this tooltip is itself
    // clickable" visual (TTSS_LINK) - most tooltips just want the
    // default (false, TTSS_NORMAL).
    class ThemedTooltipStyle : public ThemedViewStyle {
    public:
        ThemedTooltipStyle() : ThemedViewStyle("TOOLTIP") {}

        bool linked = false;


    protected:
        int partId() const override { return TTP_STANDARD; }
        int stateId(bool /*highlighted*/) const override { return linked ? TTSS_LINK : TTSS_NORMAL; }
    };

    // ThemedViewStyle plus one native spin-button arrow (SPIN/SPNP_UP or
    // SPNP_DOWN). Up and down are two entirely separate theme parts, not
    // a state variant of a single part the way pressed/highlighted are -
    // which one this style draws is its own field (isUpButton) rather
    // than derived from anything else. A real up/down spinner control
    // pairs two Views, one of each.
    class ThemedSpinButtonStyle : public ThemedViewStyle {
    public:
        ThemedSpinButtonStyle() : ThemedViewStyle("SPIN") {}

        bool isUpButton = true;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return isUpButton ? SPNP_UP : SPNP_DOWN; }

        int stateId(bool highlighted) const override {
            if (isUpButton) {
                if (!enabled) return UPS_DISABLED;
                if (pressed) return UPS_PRESSED;
                if (highlighted) return UPS_HOT;
                return UPS_NORMAL;
            }
            if (!enabled) return DNS_DISABLED;
            if (pressed) return DNS_PRESSED;
            if (highlighted) return DNS_HOT;
            return DNS_NORMAL;
        }
    };

    // ThemedViewStyle plus a native edit control's background/border
    // chrome (EDIT/EP_EDITTEXT) - the text itself isn't drawn by this
    // style (same "chrome only" split ButtonStyle/ThemedButtonStyle
    // already have from a button's own label - see LabelStyle for actual
    // text drawing); pair with a client/subview that draws its own text
    // into getClientBounds().
    class ThemedEditStyle : public ThemedViewStyle {
    public:
        ThemedEditStyle() : ThemedViewStyle("EDIT") {}

        bool focused = false;
        bool readOnly = false;
        bool enabled = true;

        // Deliberately does NOT override paintFocusRing() - an earlier
        // version of this class did, on the assumption that stateId()'s
        // own ETS_FOCUSED (above) would draw a visibly distinct native
        // border once focused is true, making the generic dashed ring
        // redundant. Confirmed live to be wrong: on at least one real
        // Windows install/theme, ETS_FOCUSED renders pixel-identical to
        // ETS_NORMAL for EP_EDITTEXT - the only real difference a focused
        // TextField/TextControl had was the blinking caret itself (see
        // TextController::drawCaret(), controls.cpp), easy to miss and
        // not present at all for ListView/TreeView/DropDownList (no
        // caret there). So this now falls through to ViewStyle's own
        // default ring like every other control - see
        // TextController::handleGotFocus()/handleLostFocus() (controls.cpp)
        // and ListView/TreeView/DropDownList's own handleGotFocus()/
        // handleLostFocus() for what keeps `focused` above in sync with
        // real keyboard focus regardless.

    protected:
        int partId() const override { return EP_EDITTEXT; }

        int stateId(bool highlighted) const override {
            if (!enabled) return ETS_DISABLED;
            if (readOnly) return ETS_READONLY;
            if (focused) return ETS_FOCUSED;
            if (highlighted) return ETS_HOT;
            return ETS_NORMAL;
        }
    };

    // Fluent-style text field chrome - custom-drawn instead of
    // ThemedEditStyle's native DrawThemeBackground(EP_EDITTEXT), same
    // ThemedEditStyle-subclass opt-in shape as the other Fluent* styles
    // above (TextField/TextControl dynamic_cast style() to
    // ThemedEditStyle* - TextController::handleGotFocus()/
    // handleLostFocus(), controls.cpp - which this still satisfies).
    // Rounded border, plus WinUI TextBox's own distinctive visual
    // signature: a plain 1px border at rest that becomes a thicker,
    // accent-colored underline along just the bottom edge once focused
    // (see paint()'s own definition, viewstyle.cpp, for why this stays
    // additive rather than replacing ViewStyle::postPaint()'s generic
    // ring - unlike the native EP_EDITTEXT part, ETS_FOCUSED here would
    // be trivially distinguishable from ETS_NORMAL, but the ring stays on
    // anyway for consistency with every other Fluent* style, none of
    // which suppress it either).
    class FluentEditStyle : public ThemedEditStyle {
    public:
        Rect computeClientBounds(const Size& size) const override;
        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // --- Batch 2 --------------------------------------------------------
    // ThemedViewStyle only ever draws chrome (a rect at a given part/
    // state) - it never handles input or real widget logic (same
    // division of labor ButtonStyle already has: clicks are the View's
    // job, not the style's). Everything below fits that shape once an
    // extra field picks which theme part to use (same trick
    // ThemedSpinButtonStyle's isUpButton already established) - real
    // interactive behavior (scrolling, dragging a thumb, selecting a
    // tab) is still out of scope, same as everywhere else in this file.

    // ThemedViewStyle plus one native ListView row background (LISTVIEW/
    // LVP_LISTITEM). "Explorer::ListView" - not the plain "LISTVIEW"
    // class - is deliberate: OpenThemeData()'s pszClassList parameter
    // accepts an "AppName::ClassName" form, and "Explorer::ListView" is
    // Microsoft's own documented way to get the modern Explorer-style
    // selection highlight (the blue/accent rectangle real Windows file
    // lists show) for a custom-drawn list row - the plain "LISTVIEW"
    // class renders LISS_SELECTED nearly indistinguishable from
    // LISS_NORMAL on current Windows, a real, confirmed-live gap (a
    // ListView, controls.h, whose rows painted fine but never visibly
    // highlighted when selected).
    class ThemedListItemStyle : public ThemedViewStyle {
    public:
        ThemedListItemStyle() : ThemedViewStyle("Explorer::ListView") {}

        bool selected = false;
        bool enabled = true;


    protected:
        int partId() const override { return LVP_LISTITEM; }

        int stateId(bool highlighted) const override {
            if (!enabled) return LISS_DISABLED;
            if (selected) return highlighted ? LISS_HOTSELECTED : LISS_SELECTED;
            if (highlighted) return LISS_HOT;
            return LISS_NORMAL;
        }
    };

    // ThemedViewStyle plus one native list/tree column header item
    // (HEADER/HP_HEADERITEM). sorted selects the "this column is the
    // current sort column" visual (HIS_SORTED*) - pair with
    // ThemedHeaderSortArrowStyle below for the actual up/down arrow
    // glyph, drawn as a separate small View over/inside this one (real
    // header controls draw the two separately too).
    class ThemedHeaderItemStyle : public ThemedViewStyle {
    public:
        ThemedHeaderItemStyle() : ThemedViewStyle("HEADER") {}

        bool pressed = false;
        bool sorted = false;


    protected:
        int partId() const override { return HP_HEADERITEM; }

        int stateId(bool highlighted) const override {
            if (pressed) return sorted ? HIS_SORTEDPRESSED : HIS_PRESSED;
            if (highlighted) return sorted ? HIS_SORTEDHOT : HIS_HOT;
            return sorted ? HIS_SORTEDNORMAL : HIS_NORMAL;
        }
    };

    // ThemedViewStyle plus the small sort-direction arrow glyph a header
    // item shows for whichever column is currently sorted (HEADER/
    // HP_HEADERSORTARROW) - no hot/pressed/normal state variants exist
    // for this part at all (just which way the arrow points), so
    // stateId() ignores highlighted, same as ThemedStatusPaneStyle/
    // ThemedRebarBandStyle ignoring it entirely (those have no state
    // variants either; this one has exactly two, selected by
    // sortedAscending rather than by highlighted).
    class ThemedHeaderSortArrowStyle : public ThemedViewStyle {
    public:
        ThemedHeaderSortArrowStyle() : ThemedViewStyle("HEADER") {}

        bool sortedAscending = true;


    protected:
        int partId() const override { return HP_HEADERSORTARROW; }

        int stateId(bool /*highlighted*/) const override {
            return sortedAscending ? HSAS_SORTEDUP : HSAS_SORTEDDOWN;
        }
    };

    // ThemedViewStyle plus one native TreeView row background (TREEVIEW/
    // TVP_TREEITEM) - same field/state shape as ThemedListItemStyle
    // above, just a different theme class/part. "Explorer::TreeView" for
    // the same reason ThemedListItemStyle uses "Explorer::ListView" - see
    // its own doc comment.
    class ThemedTreeItemStyle : public ThemedViewStyle {
    public:
        ThemedTreeItemStyle() : ThemedViewStyle("Explorer::TreeView") {}

        bool selected = false;
        bool enabled = true;


    protected:
        int partId() const override { return TVP_TREEITEM; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TREIS_DISABLED;
            if (selected) return highlighted ? TREIS_HOTSELECTED : TREIS_SELECTED;
            if (highlighted) return TREIS_HOT;
            return TREIS_NORMAL;
        }
    };

    // ThemedViewStyle plus a TreeView item's expand/collapse "+/-" glyph
    // (TREEVIEW/TVP_GLYPH) - expanded (open vs. closed) is the real state
    // (GLPS_OPENED/GLPS_CLOSED). uxtheme also defines a wholly separate
    // TVP_HOTGLYPH *part* (not a state of TVP_GLYPH) for a "hot"
    // appearance, but ThemedViewStyle::paint() only ever calls partId()
    // with no arguments at all (only stateId() receives highlighted -
    // see viewstyle.cpp) - there's no highlighted-dependent part
    // selection available without changing that base-class signature,
    // which would ripple through every other Themed*Style in this file.
    // Deliberately left as a known trim rather than doing that: this
    // style just never shows the separate hot-glyph look, same "keep it
    // simple" scope-trim as ThemedTabItemStyle's top-aligned-only choice
    // and ThemedTrackbarThumbStyle's plain-shape-only choice elsewhere in
    // this file.
    class ThemedTreeGlyphStyle : public ThemedViewStyle {
    public:
        ThemedTreeGlyphStyle() : ThemedViewStyle("TREEVIEW") {}

        bool expanded = false;


    protected:
        int partId() const override { return TVP_GLYPH; }

        int stateId(bool /*highlighted*/) const override {
            return expanded ? GLPS_OPENED : GLPS_CLOSED;
        }
    };

    // ThemedViewStyle plus one native tab strip item (TAB/TABP_TABITEM or
    // TABP_TOPTABITEM, each with its own LEFTEDGE/RIGHTEDGE/BOTHEDGE
    // siblings - 8 parts total). Two independent selectors:
    //   - alignment picks which theme-part *group* - verified against the
    //     real SDK header rather than assumed: uxtheme only has a
    //     dedicated look for a tab strip along the *top* of its content
    //     (TABP_TOPTABITEM*); Bottom/Left/Right all render through the
    //     same plain TABP_TABITEM* group Windows itself uses for
    //     "anywhere but the top" - there's no separate per-edge rendering
    //     beyond that top/not-top split in the real theme data.
    //   - position picks which of the four parts *within* that group (a
    //     tab strip's outer items get rounded outer corners the middle
    //     ones don't).
    // All 8 resulting parts' state enums (TABITEMSTATES/TOPTABITEMSTATES/
    // TABITEMLEFTEDGESTATES/TOPTABITEMLEFTEDGESTATES/...) share the exact
    // same numeric values, so stateId() below doesn't need to switch on
    // either alignment or position at all, just partId() does.
    class ThemedTabItemStyle : public ThemedViewStyle {
    public:
        enum class Position {
            Middle,  // has neighbors on both sides
            Left,    // leftmost of the strip
            Right,   // rightmost of the strip
            Only     // the only tab in the strip
        };

        // Which edge of the tab *control* the whole strip sits along -
        // see the class comment for why only Top gets its own theme-part
        // group; Bottom/Left/Right all map to the same plain parts.
        enum class TabAlignment {
            Top,
            Bottom,
            Left,
            Right
        };

        ThemedTabItemStyle() : ThemedViewStyle("TAB") {}

        TabAlignment alignment = TabAlignment::Top;
        Position position = Position::Middle;
        bool selected = false;
        bool enabled = true;


    protected:
        int partId() const override {
            if (alignment == TabAlignment::Top) {
                switch (position) {
                    case Position::Left: return TABP_TOPTABITEMLEFTEDGE;
                    case Position::Right: return TABP_TOPTABITEMRIGHTEDGE;
                    case Position::Only: return TABP_TOPTABITEMBOTHEDGE;
                    case Position::Middle: default: return TABP_TOPTABITEM;
                }
            }
            switch (position) {
                case Position::Left: return TABP_TABITEMLEFTEDGE;
                case Position::Right: return TABP_TABITEMRIGHTEDGE;
                case Position::Only: return TABP_TABITEMBOTHEDGE;
                case Position::Middle: default: return TABP_TABITEM;
            }
        }

        int stateId(bool highlighted) const override {
            if (!enabled) return TIS_DISABLED;
            if (selected) return TIS_SELECTED;
            if (highlighted) return TIS_HOT;
            return TIS_NORMAL;
        }
    };

    // ThemedViewStyle plus a tab control's content pane background (TAB/
    // TABP_PANE) - no state variants, no extra fields, same
    // no-serialization-override shape as ThemedStatusPaneStyle/
    // ThemedRebarBandStyle.
    class ThemedTabPaneStyle : public ThemedViewStyle {
    public:
        ThemedTabPaneStyle() : ThemedViewStyle("TAB") {}

    protected:
        int partId() const override { return TABP_PANE; }
        int stateId(bool /*highlighted*/) const override { return 0; }
    };

    // ThemedViewStyle plus a trackbar/slider's track groove (TRACKBAR/
    // TKP_TRACK or TKP_TRACKVERT) - horizontal/vertical are two entirely
    // separate theme parts (same "orientation picks the part" shape as
    // ThemedTrackbarThumbStyle below); neither has any real state
    // variants (TRACKSTATES/TRACKVERTSTATES each define only *_NORMAL),
    // so stateId() ignores highlighted and just returns the one defined
    // "normal" value for whichever orientation.
    class ThemedTrackbarTrackStyle : public ThemedViewStyle {
    public:
        ThemedTrackbarTrackStyle() : ThemedViewStyle("TRACKBAR") {}

        bool horizontal = true;


    protected:
        int partId() const override { return horizontal ? TKP_TRACK : TKP_TRACKVERT; }
        int stateId(bool /*highlighted*/) const override { return horizontal ? TRS_NORMAL : TRVS_NORMAL; }
    };

    // Fluent-style slider groove - custom-drawn instead of
    // ThemedTrackbarTrackStyle's native DrawThemeBackground(TKP_TRACK),
    // same ThemedTrackbarTrackStyle-subclass opt-in shape as every other
    // Fluent* style (Slider::setHorizontal(), controls.cpp, dynamic_casts
    // style() to ThemedTrackbarTrackStyle&, which this still satisfies).
    // A thin rounded "pill" groove - no filled/unfilled split, same scope
    // as the native part it replaces (the real *value*-driven fill this
    // codebase's architecture has no concept of yet - Slider only ever
    // positions a separate thumb child along an otherwise-uniform groove;
    // adding a filled portion would need real new plumbing, out of scope
    // here).
    class FluentTrackbarTrackStyle : public ThemedTrackbarTrackStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a trackbar/slider's draggable thumb (TRACKBAR/
    // TKP_THUMB or TKP_THUMBVERT) - only the plain horizontal/vertical
    // thumb shapes are supported (uxtheme also has THUMBTOP/THUMBBOTTOM/
    // THUMBLEFT/THUMBRIGHT variants for a trackbar with tick marks only
    // on one side - out of scope, same trim as ThemedTabItemStyle's
    // top-aligned-only choice above). Positioning the thumb along the
    // track (the actual "value") is the caller's job via ordinary
    // bounds/Layout, same as everywhere else - this only draws the
    // thumb's own chrome wherever it's placed.
    class ThemedTrackbarThumbStyle : public ThemedViewStyle {
    public:
        ThemedTrackbarThumbStyle() : ThemedViewStyle("TRACKBAR") {}

        bool horizontal = true;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return horizontal ? TKP_THUMB : TKP_THUMBVERT; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TUS_DISABLED;
            if (pressed) return TUS_PRESSED;
            if (highlighted) return TUS_HOT;
            return TUS_NORMAL;
        }
    };

    // Fluent-style slider thumb - a filled circle (WinUI's own look: a
    // solid accent-colored disc, not an outlined/donut shape like
    // FluentRadioButtonStyle's checked glyph - the thumb has no
    // "unchecked" state to distinguish from). Same ThemedTrackbarThumbStyle-
    // subclass opt-in shape as FluentTrackbarTrackStyle above. Inherits
    // partSize() unchanged (ThemedViewStyle, viewstyle.cpp) - Slider::
    // resolvedThumbSize() (controls.cpp) still queries the *native*
    // TKP_THUMB/TKP_THUMBVERT part's natural size for layout even when
    // this style is opted into, which is a perfectly reasonable resting
    // footprint for a custom-drawn circle too, and keeps this from
    // needing its own hardcoded size that could drift from real DPI/
    // theme-driven scaling.
    class FluentTrackbarThumbStyle : public ThemedTrackbarThumbStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a trackbar/slider's tick marks (TRACKBAR/
    // TKP_TICS or TKP_TICSVERT) - same "orientation picks the part" shape
    // as ThemedTrackbarTrackStyle/ThemedTrackbarThumbStyle above, and
    // like ThemedTrackbarTrackStyle, neither part has any real state
    // variants (TICSSTATES/TICSVERTSTATES each define only *_NORMAL), so
    // stateId() ignores highlighted. uxtheme draws the *whole* row/column
    // of tick marks as one part (there's no per-tick theme part, and no
    // tickCount/spacing this style tracks) - positioning this style's own
    // bounds alongside the track (typically a thin strip the track's full
    // length) is the caller's job via ordinary bounds/Layout, same as
    // ThemedTrackbarThumbStyle's thumb positioning.
    class ThemedTrackbarTicksStyle : public ThemedViewStyle {
    public:
        ThemedTrackbarTicksStyle() : ThemedViewStyle("TRACKBAR") {}

        bool horizontal = true;

        // Number of tick *intervals* to draw (tickCount+1 marks, including
        // both ends) - Slider keeps this in sync with its own range/step
        // (see Slider::updateTickCount(), controls.cpp). 0 draws nothing.
        int tickCount = 10;


        // TKP_TICS/TKP_TICSVERT's own theme asset draws as a flat,
        // featureless fill in current Windows visual styles - real
        // trackbar tick *marks* are drawn by comctl32's own non-themed
        // control code, not by uxtheme (confirmed empirically: sampling
        // DrawThemeBackground's output pixels for this part showed zero
        // variation across the whole strip - a solid color, no lines).
        // This draws the actual tick lines itself with Blend2D on top of
        // that (harmless, still-themed) background fill.
        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;

    protected:
        int partId() const override { return horizontal ? TKP_TICS : TKP_TICSVERT; }
        int stateId(bool /*highlighted*/) const override { return horizontal ? TSS_NORMAL : TSVS_NORMAL; }
    };

    // ThemedViewStyle plus a progress bar's track/background (PROGRESS/
    // PP_BAR or PP_BARVERT) - horizontal/vertical are two entirely
    // separate theme parts (same "orientation picks the part" shape as
    // ThemedTrackbarTrackStyle above). Unlike PP_TRANSPARENTBAR/
    // PP_TRANSPARENTBARVERT (a different, out-of-scope part with its own
    // TRANSPARENTBARSTATES: PBBS_NORMAL/PBBS_PARTIAL), the plain PP_BAR/
    // PP_BARVERT parts this class uses have no state enum in the real
    // theme data at all, so stateId() always returns 0 - same shape as
    // ThemedTabPaneStyle.
    class ThemedProgressBarTrackStyle : public ThemedViewStyle {
    public:
        ThemedProgressBarTrackStyle() : ThemedViewStyle("PROGRESS") {}

        bool horizontal = true;


    protected:
        int partId() const override { return horizontal ? PP_BAR : PP_BARVERT; }
        int stateId(bool /*highlighted*/) const override { return 0; }
    };

    // Fluent-style progress bar track - custom-drawn instead of
    // ThemedProgressBarTrackStyle's native DrawThemeBackground(PP_BAR),
    // same subclass opt-in shape as every other Fluent* style (Progress::
    // setHorizontal(), controls.cpp, dynamic_casts style() to
    // ThemedProgressBarTrackStyle&, which this still satisfies). A fully-
    // rounded "pill" bar (Fluent's own ProgressBar shape), muted so the
    // FluentProgressBarFillStyle fill (below) reads clearly on top of it.
    class FluentProgressBarTrackStyle : public ThemedProgressBarTrackStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a progress bar's fill (PROGRESS/PP_FILL or
    // PP_FILLVERT). How much of the bar is filled isn't a field here -
    // that's the caller's job via ordinary bounds/Layout (a narrower
    // SubView than the track, same as ThemedTrackbarThumbStyle's thumb
    // positioning). What this tracks instead is the fill's *state* - maps
    // to FILLSTATES/FILLVERTSTATES' PBFS_NORMAL/ERROR/PAUSED, the same
    // green/red/yellow coloring a real Win32 progress bar control shows
    // for PBM_SETSTATE(PBST_NORMAL/PBST_ERROR/PBST_PAUSED). highlighted
    // has no effect here, unlike most other Themed*Style classes - a
    // progress bar isn't interactive, so stateId() ignores it. Two things
    // deliberately out of scope, same "no real animation logic" trim as
    // ThemedSpinButtonStyle: PBFS_PARTIAL (an indeterminate-marquee-only
    // state), and the animated PP_PULSEOVERLAY/PP_MOVEOVERLAY parts
    // (marquee mode itself, which needs a timer to look right).
    class ThemedProgressBarFillStyle : public ThemedViewStyle {
    public:
        enum class FillState {
            Normal,
            Error,
            Paused
        };

        ThemedProgressBarFillStyle() : ThemedViewStyle("PROGRESS") {}

        bool horizontal = true;
        FillState state = FillState::Normal;


    protected:
        int partId() const override { return horizontal ? PP_FILL : PP_FILLVERT; }

        int stateId(bool /*highlighted*/) const override {
            if (horizontal) {
                switch (state) {
                    case FillState::Error: return PBFS_ERROR;
                    case FillState::Paused: return PBFS_PAUSED;
                    case FillState::Normal: default: return PBFS_NORMAL;
                }
            }
            switch (state) {
                case FillState::Error: return PBFVS_ERROR;
                case FillState::Paused: return PBFVS_PAUSED;
                case FillState::Normal: default: return PBFVS_NORMAL;
            }
        }
    };

    // Fluent-style progress bar fill - same rounded "pill" shape as
    // FluentProgressBarTrackStyle above, colored by `state` (accent for
    // Normal, matching Fluent's own severe-red/caution-amber for Error/
    // Paused - there's no UIColorRole for either, unlike Normal's
    // HighlightBackground, so these two are fixed colors rather than a
    // theme-reactive lookup, same trim ThemedProgressBarFillStyle's own
    // class comment already accepts for marquee/PBFS_PARTIAL).
    class FluentProgressBarFillStyle : public ThemedProgressBarFillStyle {
    public:
        Rect computeClientBounds(const Size& size) const override {
            return ViewStyle::computeClientBounds(size);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;
    };

    // ThemedViewStyle plus a scrollbar's draggable thumb (SCROLLBAR/
    // SBP_THUMBBTNHORZ or SBP_THUMBBTNVERT) - same shape as
    // ThemedTrackbarThumbStyle above, different theme class/parts.
    class ThemedScrollbarThumbStyle : public ThemedViewStyle {
    public:
        ThemedScrollbarThumbStyle() : ThemedViewStyle("SCROLLBAR") {}

        bool horizontal = true;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return horizontal ? SBP_THUMBBTNHORZ : SBP_THUMBBTNVERT; }

        int stateId(bool highlighted) const override {
            if (!enabled) return SCRBS_DISABLED;
            if (pressed) return SCRBS_PRESSED;
            if (highlighted) return SCRBS_HOT;
            return SCRBS_NORMAL;
        }
    };

    // ThemedViewStyle plus a scrollbar's directional arrow button
    // (SCROLLBAR/SBP_ARROWBTN - a single part covering all four
    // directions; which one is a *state* here, not a separate part,
    // unlike everything else oriented in this file) - direction picks
    // one of four NORMAL/HOT/PRESSED/DISABLED state blocks
    // (ARROWBTNSTATES), each exactly 4 states apart in the same order,
    // so stateId() computes an offset from each direction's own *NORMAL
    // constant rather than a long per-direction/per-state switch.
    class ThemedScrollbarArrowStyle : public ThemedViewStyle {
    public:
        enum class Direction { Up, Down, Left, Right };

        ThemedScrollbarArrowStyle() : ThemedViewStyle("SCROLLBAR") {}

        Direction direction = Direction::Up;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override { return SBP_ARROWBTN; }

        int stateId(bool highlighted) const override {
            int normalState;
            switch (direction) {
                case Direction::Down: normalState = ABS_DOWNNORMAL; break;
                case Direction::Left: normalState = ABS_LEFTNORMAL; break;
                case Direction::Right: normalState = ABS_RIGHTNORMAL; break;
                case Direction::Up: default: normalState = ABS_UPNORMAL; break;
            }
            if (!enabled) return normalState + 3;   // *_DISABLED
            if (pressed) return normalState + 2;    // *_PRESSED
            if (highlighted) return normalState + 1;  // *_HOT
            return normalState;
        }
    };

    // ThemedViewStyle plus a scrollbar's track (the groove a thumb slides
    // along, SCROLLBAR/SBP_*TRACK*) - both orientation (horizontal(Vert)
    // and which side of the thumb (lower/upper, i.e. before/after it)
    // pick between four separate theme parts.
    class ThemedScrollbarTrackStyle : public ThemedViewStyle {
    public:
        enum class Position { Lower, Upper };

        ThemedScrollbarTrackStyle() : ThemedViewStyle("SCROLLBAR") {}

        bool horizontal = true;
        Position position = Position::Lower;
        bool pressed = false;
        bool enabled = true;


    protected:
        int partId() const override {
            if (horizontal) {
                return position == Position::Lower ? SBP_LOWERTRACKHORZ : SBP_UPPERTRACKHORZ;
            }
            return position == Position::Lower ? SBP_LOWERTRACKVERT : SBP_UPPERTRACKVERT;
        }

        int stateId(bool highlighted) const override {
            if (!enabled) return SCRBS_DISABLED;
            if (pressed) return SCRBS_PRESSED;
            if (highlighted) return SCRBS_HOT;
            return SCRBS_NORMAL;
        }
    };

    // ThemedViewStyle plus one top-level menu-bar entry's own chrome
    // (MENU/MENU_BARITEM) - used by MenuBar's own SubView-tree menu bar
    // (menus.h) instead of a native Win32 HMENU bar, whose row height is
    // fixed by system metrics and can't be resized (confirmed live -
    // WM_MEASUREITEM's itemHeight is silently ignored for a window's own
    // top-level bar entries, unlike a dropdown/popup item). Same
    // pressed/enabled shape and disabled>pressed>hot>normal precedence as
    // ThemedButtonStyle - verified against the real SDK header
    // (vsstyle.h's BARITEMSTATES) rather than trusting memory, same
    // practice as every other batch in this file.
    class ThemedMenuBarItemStyle : public ThemedViewStyle {
    public:
        ThemedMenuBarItemStyle() : ThemedViewStyle("MENU") {}

        bool pressed = false;
        bool enabled = true;

        // Hot/pressed highlight geometry - see paint()'s own comment
        // (viewstyle.cpp) for why this is hand-drawn instead of relying
        // on DrawThemeBackground's plain square MBI_HOT/MBI_PUSHED fill.
        float highlightInset = 2.0f;
        float highlightCornerRadius = 4.0f;


        // Overrides the base ThemedViewStyle::paint() entirely (no native
        // DrawThemeBackground call at all for this style) - see its own
        // doc comment in viewstyle.cpp.
        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;

    protected:
        int partId() const override { return MENU_BARITEM; }

        int stateId(bool highlighted) const override {
            if (!enabled) return MBI_DISABLED;
            if (pressed) return MBI_PUSHED;
            if (highlighted) return MBI_HOT;
            return MBI_NORMAL;
        }
    };

    // ThemedViewStyle plus the menu bar row's own background strip
    // (MENU/MENU_BARBACKGROUND) - no real state to track (this toolkit
    // has no "inactive window" concept modeled anywhere), so stateId()
    // just hardcodes MB_ACTIVE - same "one sensible fixed value" shape as
    // ThemedTabPaneStyle/ThemedStatusPaneStyle.
    class ThemedMenuBarBackgroundStyle : public ThemedViewStyle {
    public:
        ThemedMenuBarBackgroundStyle() : ThemedViewStyle("MENU") {}

        // Always the full rect - overrides ThemedViewStyle's own
        // computeClientBounds() rather than inheriting it. That base
        // version is deliberately paint-order-dependent (returns the full
        // rect until this style's HTHEME is cached by a first real
        // paint(), then GetThemeBackgroundContentRect()'s real deflation
        // from then on - see its own comment) - fine for a leaf control,
        // but MenuBar (menus.h) is a container that arranges its own
        // button children via FlexLayout using getClientBounds(), so that
        // shift-after-first-paint was visible as the whole bar's buttons
        // (and their text) getting a few pixels shorter after any relayout
        // following the first paint (e.g. a window resize) - reported and
        // confirmed live. MENU_BARBACKGROUND is a pure background fill,
        // not a bordered content container in the first place, so there's
        // no real content-rect deflation worth deferring to here anyway.
        Rect computeClientBounds(const Size& size) const override {
            return Rect(0.0f, 0.0f, size.width, size.height);
        }

    protected:
        int partId() const override { return MENU_BARBACKGROUND; }
        int stateId(bool /*highlighted*/) const override { return MB_ACTIVE; }
    };


} // namespace newui

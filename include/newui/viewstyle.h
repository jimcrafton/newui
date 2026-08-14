#pragma once
#include <newui/newui.h>
#include <blend2d/blend2d.h>
#include <newui/font.h>
#include <newui/geometry.h>
#include <newui/uicomponent.h>

// <newui/uicomponent.h> already transitively includes <windows.h> (via
// "newui/utils.h") without NOMINMAX defined first - see the
// feedback_no_std_minmax memory if this file ever needs std::min/std::max;
// use a ternary instead. <uxtheme.h>/<vssym32.h> need windows.h already
// included, which by this point it is.
#include <uxtheme.h>
#include <vssym32.h>

#include <string>

namespace newui {
    class View;
    // Classic 3D beveled edge look, as used for raised buttons, pressed/
    // sunken controls, and grooved/ridged separators. See paintEdge3D().
    enum class Edge3DStyle {
        Raised,  // light top/left, dark bottom/right - sticking up
        Sunken,  // dark top/left, light bottom/right - pressed in
        Etched,  // sunken outer ring + raised inner ring - a groove
        Bump     // raised outer ring + sunken inner ring - a ridge
    };

    // Draws a classic 3D bevel into the (0,0)-(size.width,size.height) rect
    // of ctx's current coordinate space, using width-thick solid edges
    // (filled rects, not stroked lines, so corners come out crisp with no
    // cap/join artifacts). highlightColor/shadowColor may be left null to
    // skip that side of the bevel. Etched/Bump draw two nested bevels of
    // opposite orientation, offset inward by width.
    inline void paintEdge3D(BLContext& ctx, const Size& size, Edge3DStyle style,
            const BLVar& highlightColor, const BLVar& shadowColor, float width = 1.0f) noexcept {
        auto bevel = [&ctx](float x0, float y0, float x1, float y1, float w,
                const BLVar& hi, const BLVar& sh) noexcept {
            if (x1 <= x0 || y1 <= y0) {
                return;
            }
            if (!hi.is_null()) {
                ctx.set_fill_style(hi);
                ctx.fill_rect(BLRect(x0, y0, x1 - x0, w));  // top
                ctx.fill_rect(BLRect(x0, y0, w, y1 - y0));  // left
            }
            if (!sh.is_null()) {
                ctx.set_fill_style(sh);
                ctx.fill_rect(BLRect(x0, y1 - w, x1 - x0, w));  // bottom
                ctx.fill_rect(BLRect(x1 - w, y0, w, y1 - y0));  // right
            }
        };

        switch (style) {
            case Edge3DStyle::Raised:
                bevel(0.0f, 0.0f, size.width, size.height, width, highlightColor, shadowColor);
                break;
            case Edge3DStyle::Sunken:
                bevel(0.0f, 0.0f, size.width, size.height, width, shadowColor, highlightColor);
                break;
            case Edge3DStyle::Etched:
                bevel(0.0f, 0.0f, size.width, size.height, width, shadowColor, highlightColor);
                bevel(width, width, size.width - width, size.height - width, width, highlightColor, shadowColor);
                break;
            case Edge3DStyle::Bump:
                bevel(0.0f, 0.0f, size.width, size.height, width, highlightColor, shadowColor);
                bevel(width, width, size.width - width, size.height - width, width, shadowColor, highlightColor);
                break;
        }
    }

    // Common appearance drawn by View::paintStyle() before a view's own
    // paint() runs. backgroundFill/borderFill/highlightFill are BLVar, so
    // each can independently hold a solid color (BLRgba32/BLRgba64), a
    // gradient (BLGradient), or an image (BLPattern wrapping a BLImage
    // loaded via BLImage::read_from_file()/read_from_data()) - a null
    // BLVar (the default) means "don't draw that part".
    //
    // Polymorphic so a View can be handed a ButtonStyle/CheckBoxStyle/
    // custom subclass via View::setStyle() to draw widget-specific chrome
    // (a 3D edge, a checkmark, ...) - paint() is the extension point;
    // override it, call ViewStyle::paint() first for the background/
    // border, then add whatever's specific to that widget kind.
    class ViewStyle : public UIComponent {
    public:
        virtual ~ViewStyle() = default;

        BLVar backgroundFill;
        BLVar borderFill;
        float borderWidth = 0.0f;
        BLVar highlightFill;

        // Sets backgroundFill to path's image (PNG/BMP/JPEG/QOI - decoded
        // via BLImage::read_from_file(), same codecs Bundle::loadImage()
        // uses), wrapped in a BLPattern - backgroundFill already supports
        // this (it's a BLVar, which can hold a solid color, a gradient,
        // or a BLPattern/image - see the class comment above), this is
        // just the convenient way to load one from disk. The BLPattern
        // takes its own independent, refcounted handle to the decoded
        // image (ordinary BLImage COW semantics), so the local BLImage
        // this reads into along the way doesn't need to outlive the
        // call. Returns false (backgroundFill left unchanged) if the
        // file can't be read/decoded.
        //
        // Deliberately takes a path/BLImage, not a newui::Image
        // (graphics.h) - Image::blImage() wraps a Win32 DIB section's
        // own memory with no internal copy (meant for a live GDI mem-DC
        // you draw into and keep alive), so handing one to a BLPattern
        // directly would leave the fill referencing memory that's freed
        // the moment the Image is destroyed. A plain BLImage (what this
        // reads into internally, and what the overload below expects)
        // owns its pixel data the normal blend2d COW way instead, so
        // there's no such hazard.
        bool setBackgroundImage(const std::string& path);

        // Sets backgroundFill directly from an already-decoded BLImage,
        // wrapped in a BLPattern - see the path-based overload above for
        // why this takes a plain BLImage rather than a newui::Image.
        void setBackgroundImage(const BLImage& image);

        // Multiplies the alpha of whatever's set above (works uniformly for
        // colors, gradients and images) - separate from any alpha already
        // baked into a color/image itself.
        float opacity = 1.0f;

        // How background/border blend with whatever's already in the
        // buffer; see BLCompOp. Defaults to normal alpha-blended painting.
        BLCompOp compositingOp = BL_COMP_OP_SRC_OVER;

        // Font available for this style's own text-drawing needs - e.g. a
        // LabelStyle subclass that draws text in its paint() override. See
        // FontManager::createFont() to load one, by system font name or by
        // file path. Default-constructed (invalid/empty) until set; the
        // base ViewStyle::paint() doesn't draw text itself (this toolkit
        // has no automatic text layout/drawing yet), so this is just
        // storage for whoever paints text for this view.
        Font font;

        // The rect (local to a view of this size, (0,0) at its top-left) a
        // client should paint its own content/children in without
        // overlapping whatever chrome this style's paint() draws - the
        // paint-free equivalent of paint()'s clientBounds out-parameter
        // below, safe to call without a BLContext or an actual paint pass
        // (e.g. from View::getClientBounds(), or a Layout arranging
        // children). paint() delegates to this (see its comment) so the
        // two can never drift apart. Subclasses that paint additional
        // chrome on top of the base background/border (a 3D edge, a
        // checkbox glyph, ...) override this the same way they override
        // paint() - chain to the base first, then deflate further by
        // whatever they add; see ButtonStyle/CheckBoxStyle.
        virtual Rect computeClientBounds(const Size& size) const {
            return Rect(0.0f, 0.0f, size.width, size.height).deflated(borderWidth);
        }

        // Paints this style into ctx, which is already translated/clipped
        // to (0,0)-(size.width,size.height) for the view being styled;
        // highlighted mirrors the view's isHighlighted() at paint time.
        // Base implementation: background (backgroundFill, or
        // highlightFill when highlighted and set) then border.
        //
        // clientBounds is an out parameter, set to computeClientBounds(size)
        // above - since that call is unqualified, it dispatches virtually
        // on this object's real (possibly derived) type even when reached
        // via a subclass's paint() chaining to this base paint() first, so
        // clientBounds already comes out fully correct for the actual
        // style before a subclass's own paint() override has drawn
        // anything further - see ButtonStyle/CheckBoxStyle, which rely on
        // this and don't need to touch clientBounds again themselves.
        virtual void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
            clientBounds = computeClientBounds(size);

            if (size.width <= 0.0f || size.height <= 0.0f) {
                return;
            }

            const BLVar& background = (highlighted && !highlightFill.is_null()) ? highlightFill : backgroundFill;

            ctx.save();
            ctx.set_comp_op(compositingOp);

            if (!background.is_null()) {
                ctx.set_fill_style(background);
                ctx.set_fill_alpha(opacity);
                ctx.fill_rect(BLRect(0, 0, size.width, size.height));
            }

            if (borderWidth > 0.0f && !borderFill.is_null()) {
                double inset = borderWidth * 0.5;
                ctx.set_stroke_style(borderFill);
                ctx.set_stroke_alpha(opacity);
                ctx.set_stroke_width(borderWidth);
                ctx.stroke_box(inset, inset, size.width - inset, size.height - inset);
            }

            ctx.restore();
        }

        View* view() {
            return view_;
        }

        const View* view() const {
            return view_;
        }

        void setView(View* v) {
            view_ = v;
        }

        void markDirty();

        // UIComponent: backgroundFill/borderFill/borderWidth/highlightFill/
        // opacity/compositingOp/font. Defined out-of-line in viewstyle.cpp
        // (unlike paint() above) since the json5 types writeFields()/
        // readFields() take are only forward-declared here (see
        // uicomponent.h) - keeps json5's real headers out of this one.
        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    private:
        View* view_ = nullptr;
    };

    // ViewStyle plus a classic 3D beveled edge (e.g. a raised look for a
    // button's resting state). This class only draws whichever edgeStyle
    // is currently set - swap it (e.g. to Edge3DStyle::Sunken) from an
    // onMouseDown/onMouseUp pair for a "pressed in" look, and pair with
    // View::setHighlighted() for a separate hover/focus tint via
    // ViewStyle::highlightFill.
    class ButtonStyle : public ViewStyle {
    public:
        Edge3DStyle edgeStyle = Edge3DStyle::Raised;
        float edgeWidth = 2.0f;
        BLVar edgeHighlightColor;
        BLVar edgeShadowColor;

        // Etched/Bump are two nested bevels, so they occupy 2x edgeWidth
        // inward from the outer edge; Raised/Sunken are just the one.
        Rect computeClientBounds(const Size& size) const override {
            Rect bounds = ViewStyle::computeClientBounds(size);
            if (edgeWidth <= 0.0f || (edgeHighlightColor.is_null() && edgeShadowColor.is_null())) {
                return bounds;
            }
            bool doubled = (edgeStyle == Edge3DStyle::Etched || edgeStyle == Edge3DStyle::Bump);
            return bounds.deflated(doubled ? edgeWidth * 2.0f : edgeWidth);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override {
            ViewStyle::paint(ctx, size, highlighted, clientBounds);

            if (edgeWidth <= 0.0f || (edgeHighlightColor.is_null() && edgeShadowColor.is_null())) {
                return;
            }

            ctx.save();
            ctx.set_comp_op(compositingOp);
            paintEdge3D(ctx, size, edgeStyle, edgeHighlightColor, edgeShadowColor, edgeWidth);
            ctx.restore();
        }

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;
    };

    // ViewStyle plus a single line of text (background/border only besides
    // that), centered both horizontally and vertically within clientBounds.
    // ViewStyle::font resolves the BLFont to draw with (see FontManager);
    // textColor is null (nothing drawn) by default, like the other
    // optional fills in this file - set it to actually see text.
    class LabelStyle : public ViewStyle {
    public:
        std::string text;
        BLVar textColor;

        LabelStyle() {
			font = FontManager::getSystemFont(SystemUIFont::Message);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override {
            ViewStyle::paint(ctx, size, highlighted, clientBounds);

            if (text.empty() || textColor.is_null()) {
                return;
            }

            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
				throw std::runtime_error("LabelStyle::paint: font not resolved to a valid BLFont");
            }

            if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
                return;
            }

            BLGlyphBuffer glyphBuffer;
            glyphBuffer.set_utf8_text(text.c_str(), text.size());
            blFont->shape(glyphBuffer);

            BLTextMetrics textMetrics;
            blFont->get_text_metrics(glyphBuffer, textMetrics);

            const BLFontMetrics& fontMetrics = blFont->metrics();
            double textWidth = textMetrics.advance.x;
            double textHeight = fontMetrics.ascent + fontMetrics.descent;

            double x = clientBounds.left() + (clientBounds.size().width - textWidth) * 0.5;
            double y = clientBounds.top() + (clientBounds.size().height - textHeight) * 0.5 + fontMetrics.ascent;

            ctx.save();
            ctx.set_comp_op(compositingOp);
            ctx.set_fill_style(textColor);
            ctx.set_fill_alpha(opacity);
            ctx.fill_utf8_text(BLPoint(x, y), *blFont, text.c_str(), text.size());
            ctx.restore();
        }

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;
    };

    // ViewStyle plus a small sunken-look box (classic checkbox chrome),
    // left-aligned and vertically centered within the view, and a
    // checkmark drawn inside it when checked is true.
    class CheckBoxStyle : public ViewStyle {
    public:
        bool checked = false;
        float boxSize = 13.0f;
        Edge3DStyle boxEdgeStyle = Edge3DStyle::Sunken;
        float boxEdgeWidth = 2.0f;
        BLVar boxFill;
        BLVar boxEdgeHighlightColor;
        BLVar boxEdgeShadowColor;
        BLVar checkColor;
        float checkWidth = 2.0f;

        // Gap left between the box and clientBounds, e.g. for a label
        // drawn by the client to the right of the box.
        float boxLabelSpacing = 4.0f;

        // The box occupies the left boxSize + boxLabelSpacing of whatever
        // the border already left clientBounds with; a label drawn by the
        // client goes to the right of that.
        Rect computeClientBounds(const Size& size) const override {
            Rect bounds = ViewStyle::computeClientBounds(size);
            if (boxSize <= 0.0f) {
                return bounds;
            }
            return bounds.deflated(boxSize + boxLabelSpacing, 0.0f, 0.0f, 0.0f);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override {
            ViewStyle::paint(ctx, size, highlighted, clientBounds);

            if (boxSize <= 0.0f) {
                return;
            }

            float boxTop = (size.height - boxSize) * 0.5f;
            Size boxViewSize(boxSize, boxSize);

            ctx.save();
            ctx.set_comp_op(compositingOp);
            ctx.translate(0.0, boxTop);

            if (!boxFill.is_null()) {
                ctx.set_fill_style(boxFill);
                ctx.fill_rect(BLRect(0, 0, boxSize, boxSize));
            }

            if (boxEdgeWidth > 0.0f) {
                paintEdge3D(ctx, boxViewSize, boxEdgeStyle, boxEdgeHighlightColor, boxEdgeShadowColor, boxEdgeWidth);
            }

            if (checked && !checkColor.is_null()) {
                ctx.set_stroke_style(checkColor);
                ctx.set_stroke_width(checkWidth);
                float inset = boxSize * 0.2f;
                ctx.stroke_line(inset, boxSize * 0.5f, boxSize * 0.45f, boxSize - inset);
                ctx.stroke_line(boxSize * 0.45f, boxSize - inset, boxSize - inset, inset);
            }

            ctx.restore();
        }

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;
    };

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
    // A subclass names the theme "class" (themeClassName, e.g. L"BUTTON",
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
        explicit ThemedViewStyle(std::wstring themeClassName) : themeClassName_(std::move(themeClassName)) {}
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

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;

    protected:
        // Which vssym32.h part/state to draw - highlighted mirrors the
        // view's isHighlighted() at paint time, same as every other
        // paint() in this file.
        virtual int partId() const = 0;
        virtual int stateId(bool highlighted) const = 0;

    private:
        std::wstring themeClassName_;
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
        ThemedButtonStyle() : ThemedViewStyle(L"BUTTON") {}

        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return BP_PUSHBUTTON; }

        int stateId(bool highlighted) const override {
            if (!enabled) return PBS_DISABLED;
            if (pressed) return PBS_PRESSED;
            if (highlighted) return PBS_HOT;
            return PBS_NORMAL;
        }
    };

    // ThemedViewStyle plus a native checkbox (BUTTON/BP_CHECKBOX).
    class ThemedCheckBoxStyle : public ThemedViewStyle {
    public:
        ThemedCheckBoxStyle() : ThemedViewStyle(L"BUTTON") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return BP_CHECKBOX; }

        int stateId(bool highlighted) const override {
            if (!enabled) return checked ? CBS_CHECKEDDISABLED : CBS_UNCHECKEDDISABLED;
            if (pressed) return checked ? CBS_CHECKEDPRESSED : CBS_UNCHECKEDPRESSED;
            if (highlighted) return checked ? CBS_CHECKEDHOT : CBS_UNCHECKEDHOT;
            return checked ? CBS_CHECKEDNORMAL : CBS_UNCHECKEDNORMAL;
        }
    };

    // ThemedViewStyle plus a native radio button (BUTTON/BP_RADIOBUTTON) -
    // same field/state shape as ThemedCheckBoxStyle above, just a
    // different part.
    class ThemedRadioButtonStyle : public ThemedViewStyle {
    public:
        ThemedRadioButtonStyle() : ThemedViewStyle(L"BUTTON") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return BP_RADIOBUTTON; }

        int stateId(bool highlighted) const override {
            if (!enabled) return checked ? RBS_CHECKEDDISABLED : RBS_UNCHECKEDDISABLED;
            if (pressed) return checked ? RBS_CHECKEDPRESSED : RBS_UNCHECKEDPRESSED;
            if (highlighted) return checked ? RBS_CHECKEDHOT : RBS_UNCHECKEDHOT;
            return checked ? RBS_CHECKEDNORMAL : RBS_UNCHECKEDNORMAL;
        }
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
        ThemedGroupBoxStyle() : ThemedViewStyle(L"BUTTON") {}

        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return BP_GROUPBOX; }

        int stateId(bool /*highlighted*/) const override {
            return enabled ? GBS_NORMAL : GBS_DISABLED;
        }
    };

    // ThemedViewStyle plus a native toolbar button (TOOLBAR/TP_BUTTON) -
    // checked is a toggle-button state (e.g. a pressed-in "bold" button
    // in a formatting toolbar), independent of pressed (the transient
    // "mouse currently down on this button right now" state).
    class ThemedToolbarButtonStyle : public ThemedViewStyle {
    public:
        ThemedToolbarButtonStyle() : ThemedViewStyle(L"TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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

    // ThemedViewStyle plus a native toolbar drop-down button (TOOLBAR/
    // TP_DROPDOWNBUTTON) - the chrome for a button whose entire face opens
    // a dropdown (as opposed to a split button, below, where only part of
    // the face does). Every TOOLBARPARTS part shares one state enum
    // (TOOLBARSTYLESTATES) - same checked/pressed/enabled shape and
    // precedence as ThemedToolbarButtonStyle above, just a different part.
    class ThemedToolbarDropDownButtonStyle : public ThemedViewStyle {
    public:
        ThemedToolbarDropDownButtonStyle() : ThemedViewStyle(L"TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedToolbarDropDownButtonGlyphStyle() : ThemedViewStyle(L"TOOLBAR") {}

        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedToolbarSplitButtonStyle() : ThemedViewStyle(L"TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedToolbarSplitButtonDropDownStyle() : ThemedViewStyle(L"TOOLBAR") {}

        bool checked = false;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedToolbarSeparatorStyle() : ThemedViewStyle(L"TOOLBAR") {}

        bool horizontal = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return horizontal ? TP_SEPARATOR : TP_SEPARATORVERT; }
        int stateId(bool /*highlighted*/) const override { return TS_NORMAL; }
    };

    // ThemedViewStyle plus a native status bar pane background (STATUS/
    // SP_PANE). This part has no state variants at all - stateId()
    // always returns 0 (same for ThemedRebarBandStyle below); no extra
    // fields either, so no writeFields()/readFields() override is needed
    // (ViewStyle's own, inherited via ThemedViewStyle, is already
    // everything there is to save).
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
        ThemedStatusPaneStyle() : ThemedViewStyle(L"STATUS") {}

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
        ThemedRebarBandStyle() : ThemedViewStyle(L"REBAR") {}

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
        ThemedRebarChevronStyle() : ThemedViewStyle(L"REBAR") {}

        bool horizontal = true;
        bool pressed = false;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedTooltipStyle() : ThemedViewStyle(L"TOOLTIP") {}

        bool linked = false;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedSpinButtonStyle() : ThemedViewStyle(L"SPIN") {}

        bool isUpButton = true;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedEditStyle() : ThemedViewStyle(L"EDIT") {}

        bool focused = false;
        bool readOnly = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
    // LVP_LISTITEM).
    class ThemedListItemStyle : public ThemedViewStyle {
    public:
        ThemedListItemStyle() : ThemedViewStyle(L"LISTVIEW") {}

        bool selected = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedHeaderItemStyle() : ThemedViewStyle(L"HEADER") {}

        bool pressed = false;
        bool sorted = false;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedHeaderSortArrowStyle() : ThemedViewStyle(L"HEADER") {}

        bool sortedAscending = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return HP_HEADERSORTARROW; }

        int stateId(bool /*highlighted*/) const override {
            return sortedAscending ? HSAS_SORTEDUP : HSAS_SORTEDDOWN;
        }
    };

    // ThemedViewStyle plus one native TreeView row background (TREEVIEW/
    // TVP_TREEITEM) - same field/state shape as ThemedListItemStyle
    // above, just a different theme class/part.
    class ThemedTreeItemStyle : public ThemedViewStyle {
    public:
        ThemedTreeItemStyle() : ThemedViewStyle(L"TREEVIEW") {}

        bool selected = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedTreeGlyphStyle() : ThemedViewStyle(L"TREEVIEW") {}

        bool expanded = false;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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

        ThemedTabItemStyle() : ThemedViewStyle(L"TAB") {}

        TabAlignment alignment = TabAlignment::Top;
        Position position = Position::Middle;
        bool selected = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedTabPaneStyle() : ThemedViewStyle(L"TAB") {}

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
        ThemedTrackbarTrackStyle() : ThemedViewStyle(L"TRACKBAR") {}

        bool horizontal = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return horizontal ? TKP_TRACK : TKP_TRACKVERT; }
        int stateId(bool /*highlighted*/) const override { return horizontal ? TRS_NORMAL : TRVS_NORMAL; }
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
        ThemedTrackbarThumbStyle() : ThemedViewStyle(L"TRACKBAR") {}

        bool horizontal = true;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return horizontal ? TKP_THUMB : TKP_THUMBVERT; }

        int stateId(bool highlighted) const override {
            if (!enabled) return TUS_DISABLED;
            if (pressed) return TUS_PRESSED;
            if (highlighted) return TUS_HOT;
            return TUS_NORMAL;
        }
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
        ThemedTrackbarTicksStyle() : ThemedViewStyle(L"TRACKBAR") {}

        bool horizontal = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedProgressBarTrackStyle() : ThemedViewStyle(L"PROGRESS") {}

        bool horizontal = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        int partId() const override { return horizontal ? PP_BAR : PP_BARVERT; }
        int stateId(bool /*highlighted*/) const override { return 0; }
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

        ThemedProgressBarFillStyle() : ThemedViewStyle(L"PROGRESS") {}

        bool horizontal = true;
        FillState state = FillState::Normal;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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

    // ThemedViewStyle plus a scrollbar's draggable thumb (SCROLLBAR/
    // SBP_THUMBBTNHORZ or SBP_THUMBBTNVERT) - same shape as
    // ThemedTrackbarThumbStyle above, different theme class/parts.
    class ThemedScrollbarThumbStyle : public ThemedViewStyle {
    public:
        ThemedScrollbarThumbStyle() : ThemedViewStyle(L"SCROLLBAR") {}

        bool horizontal = true;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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

        ThemedScrollbarArrowStyle() : ThemedViewStyle(L"SCROLLBAR") {}

        Direction direction = Direction::Up;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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

        ThemedScrollbarTrackStyle() : ThemedViewStyle(L"SCROLLBAR") {}

        bool horizontal = true;
        Position position = Position::Lower;
        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
        ThemedMenuBarItemStyle() : ThemedViewStyle(L"MENU") {}

        bool pressed = false;
        bool enabled = true;

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

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
    // ThemedTabPaneStyle/ThemedStatusPaneStyle; no writeFields()/
    // readFields() override needed for the same reason.
    class ThemedMenuBarBackgroundStyle : public ThemedViewStyle {
    public:
        ThemedMenuBarBackgroundStyle() : ThemedViewStyle(L"MENU") {}

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

}

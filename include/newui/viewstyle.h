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

}

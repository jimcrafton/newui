#pragma once
#include <newui/newui.h>
#include <blend2d/blend2d.h>
#include <newui/font.h>
#include <newui/geometry.h>
#include <newui/uicomponent.h>

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

        // Paints this style into ctx, which is already translated/clipped
        // to (0,0)-(size.width,size.height) for the view being styled;
        // highlighted mirrors the view's isHighlighted() at paint time.
        // Base implementation: background (backgroundFill, or
        // highlightFill when highlighted and set) then border.
        //
        // clientBounds is an out parameter: the rect (in the same local
        // coordinates) a client should paint its own content in afterward,
        // so it doesn't draw over whatever chrome this style just painted -
        // e.g. a 2px border deflates it by 2px on each side. Subclasses
        // that paint additional chrome (a 3D edge, a checkbox glyph, ...)
        // should call the base paint() first to get this, then deflate it
        // further by whatever they added; see ButtonStyle/CheckBoxStyle.
        virtual void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const {
            clientBounds = Rect(0.0f, 0.0f, size.width, size.height).deflated(borderWidth);

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

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override {
            ViewStyle::paint(ctx, size, highlighted, clientBounds);

            if (edgeWidth <= 0.0f || (edgeHighlightColor.is_null() && edgeShadowColor.is_null())) {
                return;
            }

            ctx.save();
            ctx.set_comp_op(compositingOp);
            paintEdge3D(ctx, size, edgeStyle, edgeHighlightColor, edgeShadowColor, edgeWidth);
            ctx.restore();

            // Etched/Bump are two nested bevels, so they occupy 2x edgeWidth
            // inward from the outer edge; Raised/Sunken are just the one.
            bool doubled = (edgeStyle == Edge3DStyle::Etched || edgeStyle == Edge3DStyle::Bump);
            clientBounds = clientBounds.deflated(doubled ? edgeWidth * 2.0f : edgeWidth);
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

            // The box occupies the left boxSize + boxLabelSpacing of
            // whatever the border already left clientBounds with; a label
            // drawn by the client goes to the right of that.
            clientBounds = clientBounds.deflated(boxSize + boxLabelSpacing, 0.0f, 0.0f, 0.0f);
        }

        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;
    };

}

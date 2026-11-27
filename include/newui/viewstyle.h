#pragma once
#include <newui/newui.h>
#include <blend2d/blend2d.h>
#include <newui/font.h>
#include <newui/geometry.h>
#include <newui/color.h>
#include <newui/graphics.h>

// No <windows.h> here: use a ternary instead of std::min/std::max only where a Windows header is also in play.
// The uxtheme-backed Themed*/Fluent* styles live in <newui/win32/themedstyles.h>.

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


    enum StrokeStyle {
        StrokeNone = 0,
        StrokeColor,
        StrokeImage,
        StrokeGradient
    };

    // How an image fill (gfx::PaintKind::Image, on backgroundFill) maps its BLImage onto the
    // shape being filled -
    // Tile repeats the image at its own natural size (the only behavior
    // ViewStyle::paint() had before this enum existed); Stretch scales it
    // to exactly cover the fill rect, ignoring the image's own aspect
    // ratio; Align draws it once, unscaled, anchored within the fill rect
    // per imageAlignment below (Center is just one of that enum's nine
    // anchor points) - the area of the fill rect the image doesn't cover
    // is left unpainted rather than smeared with the image's own edge
    // pixels, so e.g. a small centered logo doesn't bleed a stretched-looking
    // border into the rest of the view.
    enum class ImageFillMode {
        Tile,
        Stretch,
        Align
    };

    // The nine-way anchor grid ImageFillMode::Align positions an unscaled
    // image at within the fill rect - same layout a docking UI or CSS's
    // background-position keywords use.
    enum class ImageAlignment {
        TopLeft, Top, TopRight,
        Left, Center, Right,
        BottomLeft, Bottom, BottomRight
    };

    //map these to BLCompOp, they could have been different but want to keep these separate
    enum CompositingFlag {
        CompNone = 0,
        CompSrcOver = 101,        
        CompSrcopy,        
        CompSrcIn,        
        CompSrcOut,        
        CompSrcAtop,
        CompDstOver,
        CompDstCopy,
        CompDstIn,
        CompDstOut,
        CompDstAtop,
        CompXOR,
        CompClear,
        CompPlus,
        CompMinus,
        CompModulate,
        CompMultiply,
        CompScreen,
        CompOverlay,
        CompDarken,
        CompLighten,
        CompColorDodge,
        CompColorBurn,
        CompLinearBurn,
        CompLinearLight,
        CompPinLight,
        CompHardLight,
        CompSoftLight,
        CompDifference,
        CompExclusion
    };


    BLCompOp toBLCompOp(CompositingFlag f);
    CompositingFlag toCompositingFlag(BLCompOp f);

    // Convenient, doc-matched starting points for ViewStyle::setElevation()
    // (below) - Fluent's own named elevation scale (learn.microsoft.com/
    // windows/apps/design/signature-experiences/layering) publishes these
    // names/relative magnitudes, not a concrete blur/offset formula (see
    // viewstyle.cpp's own elevationBlurRadius()/
    // elevationShadowOffsetMagnitude() for the bounded approximation that
    // actually turns a value from here into pixels). setElevation() itself
    // takes any float - these are just recognizable reference points, not
    // an enum a caller is restricted to. "Control" is spelled out in full
    // (not just Control) to avoid colliding with newui::Control
    // (controls.h) for anyone who brings this whole namespace in scope.
    namespace ElevationLevel {
        constexpr float Layer = 1.0f;
        constexpr float ControlLevel = 2.0f;
        constexpr float Card = 8.0f;
        constexpr float Tooltip = 16.0f;
        constexpr float Flyout = 32.0f;
        constexpr float Dialog = 128.0f;
    }

    // Common appearance drawn by View::paintStyle() before a view's own
    // paint() runs. backgroundFill/borderFill/highlightFill are gfx::Fill
    // (graphics.h), so each can independently hold a solid color, a
    // gradient, or an image - a default-constructed Fill (kind() ==
    // gfx::PaintKind::None) means "don't draw that part", resolved to
    // whatever set_fill_style()/set_stroke_style() actually accepts via
    // Fill::toBLVar() at paint time. Was raw BLVar until this class's own
    // "properties panel" story (cpp_codetools) needed these reflectable -
    // BLVar itself can't be (reflection.h - writes silently drop it,
    // property.h's IsPodLike check fails it for animation too), same
    // reasoning ShapeStyle's own fill()/stroke() (shapes.h) already
    // migrated to Fill/Stroke for.
    //
    // Polymorphic so a View can be handed a ButtonStyle/CheckBoxStyle/
    // custom subclass via View::setStyle() to draw widget-specific chrome
    // (a 3D edge, a checkmark, ...) - paint() is the extension point;
    // override it, call ViewStyle::paint() first for the background/
    // border, then add whatever's specific to that widget kind.
    class ViewStyle {
    protected:
        gfx::Fill backgroundFill_;
    public:
        // Explicit, not relying on the implicit compiler-generated one -
        // reflectgen's constructor collection only ever sees a
        // user-declared CONSTRUCTOR cursor; an implicit default
        // constructor is invisible to it, so this class would otherwise
        // never get a registered .constructor<>() at all and could never
        // be reconstructed from a saved "type": "ViewStyle" tag (e.g.
        // behind a View::style() slot - see FlexLayout's own comment,
        // layout.h, for the fuller reasoning behind this same fix applied
        // there first).
        ViewStyle() = default;
        virtual ~ViewStyle() = default;

        
        // Both const and non-const, same pair-shape as View::style() (view.h) - the *real* reason
        // this pairing matters here, not just symmetry: reflection.h's own property() addressability
        // rule explicitly requires a non-const lvalue reference with no setter to treat a getter as
        // addressable (see ClassBuilder::property()'s own `constexpr bool addressable` check) - a
        // const-only accessor would make PropertiesModel::classifyProperty() fall through to
        // Kind::PropertyUnsupported even though gfx::Fill itself is a real, reflectable Class (a
        // real bug caught live: this was true throughout this whole migration until here). Safe to
        // expose mutably despite the "only ever set via setBackgroundColor/Gradient/Image()"
        // framing elsewhere in this class - see bkgFillStyle's own removal, below, which was the
        // real reason a raw mutable reference used to be unsafe (a Properties-panel edit through
        // this reference would have silently desynced it from backgroundFill_.kind() otherwise).
        const gfx::Fill& backgroundFill() const { return backgroundFill_;  }
        gfx::Fill& backgroundFill() { return backgroundFill_;  }

        // Getter/setter pairs, not plain public fields, for every one of these - reflectgen only
        // ever registers a plain public field as a Class::Field (see reflection.h's own Field/
        // Property distinction), and cpp_codetools's PropertiesModel (the Designer's own Properties
        // panel) only ever walks Class::allProperties(), never Class::allFields() - a real,
        // previously-invisible gap: every one of these was completely absent from the Properties
        // panel despite being real, meaningful, design-time-editable state. Getter/setter pairs
        // register as real Properties instead, matching backgroundFill's own const/non-const-pair
        // treatment above (though these are simpler - plain by-value getter+setter, no addressable-
        // reference concerns, since none of them need in-place nested editing the way a Fill does).
        const Color& borderFill() const { return borderFill_; }
        void setBorderFill(const Color& color) { borderFill_ = color; }

        float borderWidth() const { return borderWidth_; }
        void setBorderWidth(float width) { borderWidth_ = width; }

        const Color& highlightFill() const { return highlightFill_; }
        void setHighlightFill(const Color& color) { highlightFill_ = color; }

        StrokeStyle strokeStyle() const { return strokeStyle_; }
        void setStrokeStyle(StrokeStyle style) { strokeStyle_ = style; }

        // How an image fill is drawn - see ImageFillMode's own comment. Only ever meaningful for
        // backgroundFill (gfx::PaintKind::Image) - highlightFill is solid-color-only, see its own
        // comment above.
        ImageFillMode imageFillMode() const { return imageFillMode_; }
        void setImageFillMode(ImageFillMode mode) { imageFillMode_ = mode; }

        // Anchor ImageFillMode::Align positions an unscaled image fill
        // at - ignored by Tile/Stretch.
        ImageAlignment imageAlignment() const { return imageAlignment_; }
        void setImageAlignment(ImageAlignment alignment) { imageAlignment_ = alignment; }

        float rectRadius() const { return rectRadius_; }
        void setRectRadius(float radius) { rectRadius_ = radius; }

        // Multiplies the alpha of whatever's set above (works uniformly for
        // colors, gradients and images) - separate from any alpha already
        // baked into a color/image itself.
        float opacity() const { return opacity_; }
        void setOpacity(float value) { opacity_ = value; }

        // How background/border blend with whatever's already in the
        // buffer; see BLCompOp. Defaults to normal alpha-blended painting.
        CompositingFlag compositingOp() const { return compositingOp_; }
        void setCompositingOp(CompositingFlag op) { compositingOp_ = op; }

        // Font available for this style's own text-drawing needs - e.g. a
        // LabelStyle subclass that draws text in its paint() override. See
        // FontManager::createFont() to load one, by system font name or by
        // file path. Default-constructed (invalid/empty) until set; the
        // base ViewStyle::paint() doesn't draw text itself (this toolkit
        // has no automatic text layout/drawing yet), so this is just
        // storage for whoever paints text for this view.
        const Font& font() const { return font_; }
        void setFont(const Font& value) { font_ = value; }

        // Sets backgroundFill to path's image (PNG/BMP/JPEG/QOI - decoded
        // via BLImage::read_from_file(), same codecs Bundle::loadImage()
        // uses) - backgroundFill's own setImagePath() (gfx::Fill,
        // graphics.h) does the actual lazy load/cache; this is just the
        // convenient "load one from disk and mark this fill as an image"
        // one-call wrapper (setImagePath() + setKind(Image)), matching
        // setBackgroundColor()/setBackgroundGradient()'s own shape below.
        // Returns false (backgroundFill left unchanged) if the file can't
        // be read/decoded - checked eagerly here (unlike setImagePath()
        // itself, which only ever discovers a bad path lazily, the next
        // toBLVar() call) since a caller of *this* method is specifically
        // asking "did this succeed", not just "remember this path".
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

        // Sets backgroundFill directly from an already-decoded BLImage -
        // gfx::Fill::setImage() (graphics.h) bypasses its own path-based
        // lazy load for exactly this "already have real pixels in memory,
        // no file to reflect a path to" case; see the path-based overload
        // above for why this takes a plain BLImage rather than a
        // newui::Image.
        void setBackgroundImage(const BLImage& image);

        // Converts the given raw blend2d BLGradient into backgroundFill's
        // own reflectable gfx::Gradient representation (viewstyle.cpp) -
        // BLGradient itself isn't reflectable (same reason BLVar wasn't),
        // so this is a real, lossy-only-in-the-ways-gfx::Gradient itself
        // already is (a radial gradient's second radius, r1, is never
        // preserved - gfx::Gradient's own model has no field for it,
        // matching Gradient::toBLGradient()'s own reverse direction) one-
        // time conversion, not a live reference to gradient.
        void setBackgroundGradient(const BLGradient& gradient);

        void setBackgroundColor(const Color& color);
        void setBackgroundColor(const BLRgba32& color);

        void setHilightColor(const Color& color);
        
		float elevation() const { return elevation_; }        
		void setElevation(float value) { elevation_ = value; }
        

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
        virtual Rect computeClientBounds(const Size& size) const;

        // Phase 1 of 3 - draws an effect that needs to extend *outside*
        // this View's own bounds (a drop shadow, a glow, ...), before
        // paint() below even runs. Called by View::prePaintStyle()
        // (view.h), with ctx translated to this View's own origin but
        // deliberately *not* clipped to its bounds the way paint() is -
        // View::paintChildren() (view.cpp) opens a separate, unclipped
        // save()/restore() scope around this call specifically so an
        // override has real room to paint beyond (0,0)-(size.width,
        // size.height) without anything cutting it off.
        //
        // Base implementation: no-op - nothing in this codebase needs a
        // pre-paint effect yet (YAGNI), this is purely the hook a future
        // one would override. See postPaint() below for the same shape
        // on the other side of paint(), which does have a real user
        // already (the focus ring).
        virtual void prePaint(BLContext& /*ctx*/, const Size& /*size*/, bool /*highlighted*/) const;

        // Phase 2 of 3 - paints this style into ctx, which is already
        // translated/clipped to (0,0)-(size.width,size.height) for the
        // view being styled; highlighted mirrors the view's
        // isHighlighted() at paint time. Base implementation: background
        // (backgroundFill, or highlightFill when highlighted and set)
        // then border.
        //
        // clientBounds is an out parameter, set to computeClientBounds(size)
        // above - since that call is unqualified, it dispatches virtually
        // on this object's real (possibly derived) type even when reached
        // via a subclass's paint() chaining to this base paint() first, so
        // clientBounds already comes out fully correct for the actual
        // style before a subclass's own paint() override has drawn
        // anything further - see ButtonStyle/CheckBoxStyle, which rely on
        // this and don't need to touch clientBounds again themselves.
        virtual void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const;

        // Phase 3 of 3 - draws an effect that, like prePaint() above,
        // needs to extend outside this View's own bounds - called by
        // View::postPaintStyle() (view.h) in its own separate unclipped
        // scope, same shape as prePaint() but on the other side of
        // paint(). clientBounds is computeClientBounds(size) recomputed
        // fresh (paint()'s own out-parameter copy doesn't survive past
        // the clipped scope it was produced in), not necessarily the
        // exact same BLContext state paint() left behind.
        //
        // Called unconditionally, every paint pass - unlike this
        // method's predecessor (paintFocusRing(), gated by the *caller*
        // on View::isFocused() before ever calling in) this is called
        // regardless of focus state, so an override stays free to draw
        // something else here too (a hover glow, say) without fighting a
        // caller-side gate that assumed "post-paint == focus ring only".
        // The default implementation below does its own view()->
        // isFocused() check internally instead, for exactly that reason.
        //
        // Base implementation: when the owning View is focused, a solid
        // rounded-rect outline inset from clientBounds in
        // UIColorRole::HighlightBackground (uicolormanager.h - the
        // user's accent color, same role selection highlights already
        // use). Solid, not the classic dashed DrawFocusRect() look, on
        // purpose - see this method's own definition (viewstyle.cpp) for
        // why (this vendored Blend2D build's path stroker doesn't
        // actually implement dashing at all, confirmed live). Inset, not
        // Fluent's own real outward-offset convention - tried outward
        // and confirmed live to render invisibly: View::paintChildren()
        // clips every child strictly to its own bounds (zero margin)
        // during phase 2, but that constraint is exactly what this
        // 3-phase split (and this method's own unclipped calling scope)
        // now removes - an outward ring is worth retrying here.
        //
        // Override to suppress or customize where the default would look
        // wrong for a specific style. ThemedEditStyle (below) was tried as
        // exactly such an override once - relying on ETS_FOCUSED's own
        // native border instead - and confirmed live to be the wrong call:
        // that border renders pixel-identical to unfocused on at least one
        // real Windows theme, so it now keeps this default instead. Treat
        // that as a cautionary precedent, not a template, before adding
        // another override here.
        virtual void postPaint(BLContext& ctx, const Size& size, bool highlighted, const Rect& clientBounds) const;


        virtual void computePrePaintBounds(Rect& outDirtyBounds) const;

        // Non-owning upward back-reference to the owning View (View owns
        // *this via its own std::unique_ptr<ViewStyle> style_) - reachable
        // downward already via that View's own "style" property, so
        // registering this as a Property too would recurse straight back
        // into the same View ObjectReader/ObjectWriter are already walking
        // to reach this ViewStyle in the first place. Same reasoning
        // View::rootView() (view.h) is ignore-annotated for.
        //@reflect ignore=true
        View* view() {
            return view_;
        }

        // Same ignore reasoning as the non-const overload just above - a
        // reflectgen "@reflect ignore=true" comment only applies to the one
        // declaration it's directly attached to, not to sibling overloads,
        // so the const overload needs its own.
        //@reflect ignore=true
        const View* view() const {
            return view_;
        }

        void setView(View* v) {
            view_ = v;
        }

        void markDirty();

    private:
        Color borderFill_ = Color::null();
        float borderWidth_ = 0.0f;
        Color highlightFill_ = Color::null();
        StrokeStyle strokeStyle_ = StrokeStyle::StrokeColor;
        ImageFillMode imageFillMode_ = ImageFillMode::Tile;
        ImageAlignment imageAlignment_ = ImageAlignment::Center;
        float rectRadius_ = 0.0f;
        float opacity_ = 1.0f;
        CompositingFlag compositingOp_ = CompSrcOver;
        Font font_;
		float elevation_ = 0.0f;  // for drop shadow, etc.

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
        // Explicit, not relying on the implicit compiler-generated one -
        // see ViewStyle's own comment above for why.
        ButtonStyle() = default;

        Edge3DStyle edgeStyle() const { return edgeStyle_; }
        void setEdgeStyle(Edge3DStyle style) { edgeStyle_ = style; }

        float edgeWidth() const { return edgeWidth_; }
        void setEdgeWidth(float width) { edgeWidth_ = width; }

        // Off (Color::null()) by default, not Color's own usual opaque-black default - see
        // Color::isNull()'s own comment for why.
        const Color& edgeHighlightColor() const { return edgeHighlightColor_; }
        void setEdgeHighlightColor(const Color& color) { edgeHighlightColor_ = color; }

        const Color& edgeShadowColor() const { return edgeShadowColor_; }
        void setEdgeShadowColor(const Color& color) { edgeShadowColor_ = color; }

        // Etched/Bump are two nested bevels, so they occupy 2x edgeWidth
        // inward from the outer edge; Raised/Sunken are just the one.
        Rect computeClientBounds(const Size& size) const override {
            Rect bounds = ViewStyle::computeClientBounds(size);
            if (edgeWidth_ <= 0.0f || (edgeHighlightColor_.isNull() && edgeShadowColor_.isNull())) {
                return bounds;
            }
            bool doubled = (edgeStyle_ == Edge3DStyle::Etched || edgeStyle_ == Edge3DStyle::Bump);
            return bounds.deflate(doubled ? edgeWidth_ * 2.0f : edgeWidth_);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override {
            ViewStyle::paint(ctx, size, highlighted, clientBounds);

            if (edgeWidth_ <= 0.0f || (edgeHighlightColor_.isNull() && edgeShadowColor_.isNull())) {
                return;
            }

            ctx.save();
            ctx.set_comp_op(toBLCompOp(compositingOp()));
            paintEdge3D(ctx, size, edgeStyle_, BLVar(edgeHighlightColor_.toBLRgba32()), BLVar(edgeShadowColor_.toBLRgba32()), edgeWidth_);
            ctx.restore();
        }

    private:
        Edge3DStyle edgeStyle_ = Edge3DStyle::Raised;
        float edgeWidth_ = 2.0f;
        Color edgeHighlightColor_ = Color::null();
        Color edgeShadowColor_ = Color::null();
    };

    // ViewStyle plus a single line of text (background/border only besides
    // that), centered both horizontally and vertically within clientBounds.
    // ViewStyle::font resolves the BLFont to draw with (see FontManager);
    // textColor is null (nothing drawn) by default, like the other
    // optional fills in this file - set it to actually see text.
    //
    // No backgroundFill is needed for the text to render cleanly: every
    // RootView::repaint() starts from a blank buffer and redraws the whole
    // tree, so this view's anti-aliased glyph edges always land on a fresh
    // backdrop and reproduce the same pixels each time. (That wasn't always
    // so - repaint() used to fill the root background only inside the dirty
    // rect while still redrawing every child over the whole window, so a
    // transparent text view had its glyph edges re-blended onto themselves
    // on every unrelated repaint and thickened visibly - which is why this
    // comment used to recommend an opaque backgroundFill as a workaround.
    // Set one only if you actually want that look.)
    // Where a Label's text sits across its width.
    enum class TextAlignment { Left, Center, Right };
    // Where a Label's text block sits down its height. Middle is the original behaviour.
    enum class VerticalTextAlignment { Top, Middle, Bottom };

    class LabelStyle : public ViewStyle {
    public:
        const std::string& text() const { return text_; }
        void setText(const std::string& value) { text_ = value; }

        // Off (Color::null()) by default, not Color's own usual opaque-black default - see
        // Color::isNull()'s own comment for why.
        const Color& textColor() const { return textColor_; }
        void setTextColor(const Color& color) { textColor_ = color; }

        // Off by default - the original, single-line-centered behavior
        // every existing Label still gets unchanged. When on, paint()
        // (viewstyle.cpp) greedily breaks text() across as many lines as
        // needed to fit clientBounds' own width (breaking only at a
        // space, never mid-word), vertically centering the whole block
        // instead of just one line.
        bool wordWrap() const { return wordWrap_; }
        void setWordWrap(bool value) { wordWrap_ = value; }

        // Centered by default.
        TextAlignment textAlignment() const { return textAlignment_; }
        void setTextAlignment(TextAlignment value) { textAlignment_ = value; }

        // Middle by default.
        VerticalTextAlignment verticalTextAlignment() const { return verticalTextAlignment_; }
        void setVerticalTextAlignment(VerticalTextAlignment value) { verticalTextAlignment_ = value; }

        LabelStyle() {
			setFont(FontManager::getSystemFont(SystemUIFont::Message));
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;

    private:
        std::string text_;
        Color textColor_ = Color::null();
        bool wordWrap_ = false;
        TextAlignment textAlignment_ = TextAlignment::Center;
        VerticalTextAlignment verticalTextAlignment_ = VerticalTextAlignment::Middle;
    };

    // ViewStyle plus a small sunken-look box (classic checkbox chrome),
    // left-aligned and vertically centered within the view, and a
    // checkmark drawn inside it when checked is true.
    class CheckBoxStyle : public ViewStyle {
    public:
        // Explicit, not relying on the implicit compiler-generated one -
        // see ViewStyle's own comment above for why.
        CheckBoxStyle() = default;

        bool isChecked() const { return checked_; }
        void setChecked(bool value) { checked_ = value; }

        float boxSize() const { return boxSize_; }
        void setBoxSize(float size) { boxSize_ = size; }

        Edge3DStyle boxEdgeStyle() const { return boxEdgeStyle_; }
        void setBoxEdgeStyle(Edge3DStyle style) { boxEdgeStyle_ = style; }

        float boxEdgeWidth() const { return boxEdgeWidth_; }
        void setBoxEdgeWidth(float width) { boxEdgeWidth_ = width; }

        // Off (Color::null()) by default, not Color's own usual opaque-black default - see
        // Color::isNull()'s own comment for why.
        const Color& boxFill() const { return boxFill_; }
        void setBoxFill(const Color& color) { boxFill_ = color; }

        const Color& boxEdgeHighlightColor() const { return boxEdgeHighlightColor_; }
        void setBoxEdgeHighlightColor(const Color& color) { boxEdgeHighlightColor_ = color; }

        const Color& boxEdgeShadowColor() const { return boxEdgeShadowColor_; }
        void setBoxEdgeShadowColor(const Color& color) { boxEdgeShadowColor_ = color; }

        const Color& checkColor() const { return checkColor_; }
        void setCheckColor(const Color& color) { checkColor_ = color; }

        float checkWidth() const { return checkWidth_; }
        void setCheckWidth(float width) { checkWidth_ = width; }

        // Gap left between the box and clientBounds, e.g. for a label
        // drawn by the client to the right of the box.
        float boxLabelSpacing() const { return boxLabelSpacing_; }
        void setBoxLabelSpacing(float spacing) { boxLabelSpacing_ = spacing; }

        // The box occupies the left boxSize + boxLabelSpacing of whatever
        // the border already left clientBounds with; a label drawn by the
        // client goes to the right of that.
        Rect computeClientBounds(const Size& size) const override {
            Rect bounds = ViewStyle::computeClientBounds(size);
            if (boxSize_ <= 0.0f) {
                return bounds;
            }
            return bounds.deflate(boxSize_ + boxLabelSpacing_, 0.0f, 0.0f, 0.0f);
        }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override {
            ViewStyle::paint(ctx, size, highlighted, clientBounds);

            if (boxSize_ <= 0.0f) {
                return;
            }

            float boxTop = (size.height - boxSize_) * 0.5f;
            Size boxViewSize(boxSize_, boxSize_);

            ctx.save();
            ctx.set_comp_op(toBLCompOp(compositingOp()));
            ctx.translate(0.0, boxTop);

            if (!boxFill_.isNull()) {
                ctx.set_fill_style(boxFill_.toBLRgba32());
                ctx.fill_rect(BLRect(0, 0, boxSize_, boxSize_));
            }

            if (boxEdgeWidth_ > 0.0f) {
                paintEdge3D(ctx, boxViewSize, boxEdgeStyle_, BLVar(boxEdgeHighlightColor_.toBLRgba32()), BLVar(boxEdgeShadowColor_.toBLRgba32()), boxEdgeWidth_);
            }

            if (checked_ && !checkColor_.isNull()) {
                ctx.set_stroke_style(checkColor_.toBLRgba32());
                ctx.set_stroke_width(checkWidth_);
                float inset = boxSize_ * 0.2f;
                ctx.stroke_line(inset, boxSize_ * 0.5f, boxSize_ * 0.45f, boxSize_ - inset);
                ctx.stroke_line(boxSize_ * 0.45f, boxSize_ - inset, boxSize_ - inset, inset);
            }

            ctx.restore();
        }

    private:
        bool checked_ = false;
        float boxSize_ = 13.0f;
        Edge3DStyle boxEdgeStyle_ = Edge3DStyle::Sunken;
        float boxEdgeWidth_ = 2.0f;
        Color boxFill_ = Color::null();
        Color boxEdgeHighlightColor_ = Color::null();
        Color boxEdgeShadowColor_ = Color::null();
        Color checkColor_ = Color::null();
        float checkWidth_ = 2.0f;
        float boxLabelSpacing_ = 4.0f;
    };

    // ViewStyle plus a checkerboard backdrop drawn behind an image fill
    // that actually carries an alpha channel (BL_FORMAT_PRGB32 - see
    // blend2d's PNG decoder, pngcodec.cpp, which only ever picks that
    // over BL_FORMAT_XRGB32 when the source file's own PNG color type
    // carries alpha/palette-transparency/grayscale-alpha; a plain RGB PNG
    // decodes as BL_FORMAT_XRGB32 and gets no checkerboard) - the classic
    // image-editor convention (Photoshop, GIMP, ...) for making "this
    // pixel is actually transparent" visually distinct from "this pixel
    // just happens to be whatever plain color is behind the view",
    // especially relevant for ImageFillMode::Align (viewstyle.h), which
    // often only covers part of its own view to begin with. Used by
    // controls::Image (controls.h) as its default style; nothing else in
    // this codebase currently opts into it, so no other image fill
    // anywhere else grows an unexpected checkerboard.
    //
    // Only ever draws anything when not highlighted (highlightFill is solid-color-only, never
    // Image) and backgroundFill().kind() is gfx::PaintKind::Image with an actual alpha channel -
    // every other fill kind (solid color, gradient, no fill at all, or an opaque XRGB32 image)
    // falls straight through to the base paint() unchanged, drawing nothing extra.
    class ImageFillStyle : public ViewStyle {
    public:
        ImageFillStyle() = default;

        // Checkerboard square size, in device pixels - the same rough
        // ballpark image editors use.
        float checkerSize() const { return checkerSize_; }
        void setCheckerSize(float size) { checkerSize_ = size; }

        // Photoshop/GIMP-style light-gray/white pair by default - always
        // drawn fully opaque, regardless of this style's own opacity (see
        // paint()'s own comment): fading the image should reveal more of
        // the checkerboard, not fade the checkerboard itself away too.
        const Color& checkerColorA() const { return checkerColorA_; }
        void setCheckerColorA(const Color& color) { checkerColorA_ = color; }

        const Color& checkerColorB() const { return checkerColorB_; }
        void setCheckerColorB(const Color& color) { checkerColorB_ = color; }

        void paint(BLContext& ctx, const Size& size, bool highlighted, Rect& clientBounds) const override;

    private:
        float checkerSize_ = 8.0f;
        Color checkerColorA_ = Color(0xd0d0d0u, false);
        Color checkerColorB_ = Color(0xffffffu, false);
    };

}

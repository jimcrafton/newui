#pragma once

#include <newui/newui.h>
#include <newui/geometry.h>
#include <newui/color.h>
#include <newui/viewstyle.h>
#include <newui/graphics.h>
#include <newui/font.h>

#include <blend2d/blend2d.h>

#include <vector>

// Declarative graphics elements - a Shape describes its own geometry plus
// a Photoshop-style layer-fx bag (fill, stroke, opacity, composite
// operator, glow, drop shadow) and a 2D transform, and knows how to paint
// all of that into a BLContext via render(). A ShapeLayer groups Shapes
// together with their own opacity/compositingOp, and culls any shape
// whose bounds fall entirely outside the area being drawn.
//
// Everything here only ever touches a BLContext/BLPath/BLImage - the same
// primitives ViewStyle::paint() (viewstyle.h) already draws with - so a
// future View that wants to host a ShapeLayer as an optional way to
// design its own UI can just call ShapeLayer::render() from its own
// paint() the same way any other chrome is drawn there.
//
// Every field in this file is private + a getter/setter pair rather than
// a plain public member - same convention newui::Rect/Point already use
// (geometry.h) - so this hierarchy reflects through reflection.h's
// getter/setter Property path (see the hand-written registration this
// pairs with) and has real accessor methods for a future PropertyManager-
// driven animation to call, rather than a raw field it would have to
// reach past.
namespace newui::shapes {

    // Scale, then rotate (about pivot), then translate - applied to ctx
    // in that order via applyTo(), which composes onto whatever transform
    // ctx already has (its "parent space") rather than replacing it.
    class Transform2D {
    public:
        Transform2D() = default;

        const Point& position() const { return position_; }
        void setPosition(const Point& p) { position_ = p; }

        const Point& scale() const { return scale_; }
        void setScale(const Point& s) { scale_ = s; }

        float rotationRadians() const { return rotationRadians_; }
        void setRotationRadians(float radians) { rotationRadians_ = radians; }

        const Point& pivot() const { return pivot_; }  // rotation origin, in the shape's own local space
        void setPivot(const Point& p) { pivot_ = p; }

        // Radians, along each axis independently - see BLContext::skew()/
        // BLMatrix2D::skew() (blend2d's own primitive this maps straight
        // onto). Unlike rotationRadians(), there's no pivot for skew -
        // same "always about local (0,0)" simplicity scale() already has
        // here, not a limitation blend2d itself has (BLMatrix2D::skew()
        // doesn't take one either - only rotate does).
        float skewX() const { return skewX_; }
        void setSkewX(float radians) { skewX_ = radians; }

        float skewY() const { return skewY_; }
        void setSkewY(float radians) { skewY_ = radians; }

        void applyTo(BLContext& ctx) const {
            ctx.translate(position_.x, position_.y);
            if (rotationRadians_ != 0.0f) {
                ctx.rotate(double(rotationRadians_), double(pivot_.x), double(pivot_.y));
            }
            if (skewX_ != 0.0f || skewY_ != 0.0f) {
                ctx.skew(double(skewX_), double(skewY_));
            }
            if (scale_.x != 1.0f || scale_.y != 1.0f) {
                ctx.scale(scale_.x, scale_.y);
            }
        }

    private:
        Point position_;
        Point scale_{1.0f, 1.0f};
        float rotationRadians_ = 0.0f;
        Point pivot_;
        float skewX_ = 0.0f;
        float skewY_ = 0.0f;
    };

    // A blurred cast of a shape's own fill+stroke silhouette, drawn either
    // around its edges (Glow) or offset behind it (DropShadow) - see
    // Shape::render(). Blend2D has no built-in blur filter, so both are
    // approximated by rendering that silhouette solid into a small
    // offscreen mask and box-blurring the mask in place (boxBlur3(),
    // shapes.cpp) - three box-blur passes read as close enough to a
    // Gaussian for a UI effect.
    class Glow {
    public:
        Glow() = default;

        bool enabled() const { return enabled_; }
        void setEnabled(bool value) { enabled_ = value; }

        const Color& color() const { return color_; }
        void setColor(const Color& color) { color_ = color; }

        CompositingFlag compOp() const { return compOp_; }
        void setCompOp(CompositingFlag op) { compOp_ = op; }

        float amount() const { return amount_; }  // opacity of the blurred mask, [0,1]
        void setAmount(float amount) { amount_ = amount; }

        float softness() const { return softness_; }  // blur radius in pixels
        void setSoftness(float softness) { softness_ = softness; }

    private:
        bool enabled_ = false;
        Color color_{1.0f, 1.0f, 1.0f, 1.0f};
        CompositingFlag compOp_ = CompSrcOver;
        float amount_ = 1.0f;
        float softness_ = 6.0f;
    };

    class DropShadow {
    public:
        DropShadow() = default;

        bool enabled() const { return enabled_; }
        void setEnabled(bool value) { enabled_ = value; }

        const Color& color() const { return color_; }
        void setColor(const Color& color) { color_ = color; }

        CompositingFlag compOp() const { return compOp_; }
        void setCompOp(CompositingFlag op) { compOp_ = op; }

        float amount() const { return amount_; }
        void setAmount(float amount) { amount_ = amount; }

        float softness() const { return softness_; }
        void setSoftness(float softness) { softness_ = softness; }

        const Point& offset() const { return offset_; }
        void setOffset(const Point& offset) { offset_ = offset; }

    private:
        bool enabled_ = false;
        Color color_{0.0f, 0.0f, 0.0f, 0.6f};
        CompositingFlag compOp_ = CompSrcOver;
        float amount_ = 1.0f;
        float softness_ = 6.0f;
        Point offset_{4.0f, 4.0f};
    };

    // Everything a Shape paints itself with, beyond its own geometry.
    // fill()/stroke() are newui::gfx::Fill/Stroke (graphics.h) - a POD-ish
    // stand-in for a live BLVar (color, gradient, or image path), since a
    // raw BLVar can't itself be reflected or animated - see Fill's own
    // class comment.
    class ShapeStyle {
    public:
        ShapeStyle() = default;

        gfx::Fill& fill() { return fill_; }
        const gfx::Fill& fill() const { return fill_; }

        gfx::Stroke& stroke() { return stroke_; }
        const gfx::Stroke& stroke() const { return stroke_; }

        float opacity() const { return opacity_; }
        void setOpacity(float opacity) { opacity_ = opacity; }

        CompositingFlag compositingOp() const { return compositingOp_; }
        void setCompositingOp(CompositingFlag op) { compositingOp_ = op; }

        Glow& glow() { return glow_; }
        const Glow& glow() const { return glow_; }

        DropShadow& dropShadow() { return dropShadow_; }
        const DropShadow& dropShadow() const { return dropShadow_; }

    private:
        gfx::Fill fill_;
        gfx::Stroke stroke_;
        float opacity_ = 1.0f;
        CompositingFlag compositingOp_ = CompSrcOver;
        Glow glow_;
        DropShadow dropShadow_;
    };

    // Base of the Shape hierarchy - Line, Rectangle (+RoundRect), Circle
    // (+Arc), Path (+Curve). A subclass only has to describe its own
    // geometry (localBounds()/buildPath()); render()'s pipeline (this
    // shape's transform, drop shadow, glow, fill, stroke, all in that
    // stacking order - shadow furthest back, glow around the silhouette,
    // fill+stroke on top, matching a Photoshop layer-fx stack) is shared
    // here so every shape gets all of it for free.
    class Shape {
    public:
        Shape() = default;
        virtual ~Shape() = default;

        ShapeStyle& style() { return style_; }
        const ShapeStyle& style() const { return style_; }

        Transform2D& transform() { return transform_; }
        const Transform2D& transform() const { return transform_; }

        // Local-space (pre-transform) bounding box of this shape's own
        // geometry - ignores strokeWidth/glow/dropShadow inflation (see
        // boundsWithEffects()), so subclasses can implement it from just
        // their own fields.
        virtual Rect localBounds() const = 0;

        // localBounds() padded out by however far strokeWidth/glow/
        // dropShadow can actually paint beyond it - what ShapeLayer::
        // render() transforms to world space and tests against its
        // visible rect, so a wide glow near a layer's edge isn't culled
        // just because the shape's bare geometry falls outside it.
        Rect boundsWithEffects() const;

        // localBounds()/boundsWithEffects(), transformed to whatever
        // coordinate space ctx currently has (i.e. this shape's parent
        // space) - an axis-aligned box around the four transformed
        // corners, so it stays a true bound even under rotation.
        Rect worldBoundsWithEffects() const;

        // Paints this shape into ctx: saves ctx's state, applies
        // transform() on top of whatever ctx already has, paints drop
        // shadow/glow/fill/stroke, then restores. A no-op if
        // style().opacity() is 0.
        void render(BLContext& ctx) const;

    protected:
        // Appends this shape's own geometry, in local space, to path -
        // the one thing each subclass actually implements.
        virtual void buildPath(BLPath& path) const = 0;

    private:
        void paintFillAndStroke(BLContext& ctx, const BLPath& path) const;
        void paintEffect(BLContext& ctx, const BLPath& path, const Color& color,
            CompositingFlag compOp, float amount, float softness, const Point& offset) const;

        ShapeStyle style_;
        Transform2D transform_;
    };

    class Line : public Shape {
    public:
        Line() = default;
        ~Line() override = default;

        const Point& p0() const { return p0_; }
        void setP0(const Point& p) { p0_ = p; }
        const Point& p1() const { return p1_; }
        void setP1(const Point& p) { p1_ = p; }

        Rect localBounds() const override;

    protected:
        void buildPath(BLPath& path) const override;

    private:
        Point p0_;
        Point p1_;
    };

    class Rectangle : public Shape {
    public:
        Rectangle() = default;
        ~Rectangle() override = default;

        float x() const { return x_; }
        void setX(float value) { x_ = value; }
        float y() const { return y_; }
        void setY(float value) { y_ = value; }
        float width() const { return width_; }
        void setWidth(float value) { width_ = value; }
        float height() const { return height_; }
        void setHeight(float value) { height_ = value; }

        Rect localBounds() const override {
            return Rect(x_, y_, width_, height_);
        }

    protected:
        void buildPath(BLPath& path) const override;

    private:
        float x_ = 0.0f;
        float y_ = 0.0f;
        float width_ = 0.0f;
        float height_ = 0.0f;
    };

    class RoundRect : public Rectangle {
    public:
        RoundRect() = default;
        ~RoundRect() override = default;

        float radiusX() const { return radiusX_; }
        void setRadiusX(float value) { radiusX_ = value; }
        float radiusY() const { return radiusY_; }
        void setRadiusY(float value) { radiusY_ = value; }

    protected:
        void buildPath(BLPath& path) const override;

    private:
        float radiusX_ = 4.0f;
        float radiusY_ = 4.0f;
    };

    // Builds a rectangle path whose top-left/top-right corners are rounded
    // by radius only if roundTop is true, and whose bottom-left/bottom-
    // right corners are rounded only if roundBottom is true - the shape a
    // "this fill meets a square seam on one side" view needs (FrameProxy's
    // own title bar, meeting its body below; RootViewProxy's own body fill
    // when hosted under a FrameProxy, meeting its title bar above) that a
    // plain BLRoundRect can't express (one uniform radius on all four
    // corners). radius <= 0, or neither flag set, degenerates to a plain
    // rectangle. A free function, not a Shape - both real callers build
    // this directly into their own paint()'s BLPath, not through a
    // ShapeLayer.
    void buildPartiallyRoundedRectPath(BLPath& path, const Rect& r, float radius, bool roundTop, bool roundBottom);

    class Circle : public Shape {
    public:
        Circle() = default;
        ~Circle() override = default;

        float centerX() const { return centerX_; }
        void setCenterX(float value) { centerX_ = value; }
        float centerY() const { return centerY_; }
        void setCenterY(float value) { centerY_ = value; }
        float radius() const { return radius_; }
        void setRadius(float value) { radius_ = value; }

        Rect localBounds() const override {
            return Rect(centerX_ - radius_, centerY_ - radius_, radius_ * 2.0f, radius_ * 2.0f);
        }

    protected:
        void buildPath(BLPath& path) const override;

    private:
        float centerX_ = 0.0f;
        float centerY_ = 0.0f;
        float radius_ = 0.0f;
    };

    // startRadians()/sweepRadians() follow blend2d's own BLArc convention:
    // 0 points along +x, positive sweeps clockwise (screen space, +y
    // down). pie() draws the two radii back to center too (a filled "pie
    // slice" outline) instead of just the open arc stroke.
    class Arc : public Circle {
    public:
        Arc() = default;
        ~Arc() override = default;

        float startRadians() const { return startRadians_; }
        void setStartRadians(float value) { startRadians_ = value; }
        float sweepRadians() const { return sweepRadians_; }
        void setSweepRadians(float value) { sweepRadians_ = value; }
        bool pie() const { return pie_; }
        void setPie(bool value) { pie_ = value; }

    protected:
        void buildPath(BLPath& path) const override;

    private:
        float startRadians_ = 0.0f;
        float sweepRadians_ = 4.71238898f;  // 270 degrees - an arc, not a full circle, by default
        bool pie_ = false;
    };

    class Path : public Shape {
    public:
        Path() = default;
        ~Path() override = default;

        std::vector<Point>& points() { return points_; }
        const std::vector<Point>& points() const { return points_; }

        bool closed() const { return closed_; }
        void setClosed(bool value) { closed_ = value; }

        Rect localBounds() const override;

    protected:
        void buildPath(BLPath& path) const override;

        std::vector<Point> points_;

    private:
        bool closed_ = false;
    };

    // Path, but renders its points() as a smooth curve running through
    // all of them (a Catmull-Rom spline, converted to cubic bezier
    // segments) instead of straight line segments between them.
    class Curve : public Path {
    public:
        Curve() = default;
        ~Curve() override = default;

    protected:
        void buildPath(BLPath& path) const override;
    };

    // Renders as the real glyph outlines of text() shaped with font() -
    // buildPath() calls BLFont::get_glyph_run_outlines() and appends them
    // straight into the path Shape::render() already fills/strokes/glows,
    // so a Text needs no special-casing anywhere else in the pipeline:
    // fill/stroke/glow/dropShadow all work on it exactly like any other
    // shape's silhouette. x()/y() is the text's baseline origin, matching
    // BLContext::fill_utf8_text()'s own convention (see controls.cpp/
    // menus.cpp) rather than a top-left box.
    class Text : public Shape {
    public:
        Text() = default;
        ~Text() override = default;

        const std::string& text() const { return text_; }
        void setText(const std::string& value) { text_ = value; }

        float x() const { return x_; }
        void setX(float value) { x_ = value; }
        float y() const { return y_; }
        void setY(float value) { y_ = value; }

        Font& font() { return font_; }
        const Font& font() const { return font_; }

        Rect localBounds() const override;

    protected:
        void buildPath(BLPath& path) const override;

    private:
        std::string text_;
        float x_ = 0.0f;
        float y_ = 0.0f;
        Font font_;
    };

    // Path, but renders text() flowing along its points() (the same
    // Catmull-Rom curve Curve draws) instead of drawing the curve itself
    // - blend2d has no built-in text-on-a-path layout (nothing like SVG's
    // textPath), so this builds one out of the same lower-level pieces
    // Text already uses (BLFont::shape()/get_glyph_run_outlines()) plus a
    // hand-rolled arc-length walk of the curve (shapes.cpp). Each
    // character is shaped and measured on its own - no ligature/kerning-
    // aware batch shaping across the whole string, no bidi/complex-script
    // support - just enough to place ordinary Latin-ish text along a
    // path, which is what every shapes1.cpp/shapes2.cpp demo actually
    // needs. Like Text, buildPath() emits the placed glyph outlines
    // themselves (not the underlying curve) into the path Shape::render()
    // fills/strokes/glows, so fill/stroke/glow/dropShadow all apply to
    // the placed text exactly like any other shape's silhouette; the
    // curve itself is never drawn - add a separate Curve on top of the
    // same points() if the guide path itself should also be visible.
    class TextOnPath : public Path {
    public:
        TextOnPath() = default;
        ~TextOnPath() override = default;

        const std::string& text() const { return text_; }
        void setText(const std::string& value) { text_ = value; }

        Font& font() { return font_; }
        const Font& font() const { return font_; }

        // Distance, in pixels along the curve from its own start, to
        // where the text begins - lets text slide along a fixed path
        // (e.g. to animate it "flowing" along the curve over time)
        // without moving the path itself. Text past the curve's end
        // simply stops being placed rather than wrapping or extrapolating
        // past it.
        float startOffset() const { return startOffset_; }
        void setStartOffset(float value) { startOffset_ = value; }

        // A padded box around points()'s own bounding box (roughly one
        // font-size in every direction, to allow for glyph ascent/descent
        // and any curve bulge outside the raw control points) - not a
        // tight fit around the actual placed glyphs the way Text::
        // localBounds() is, since that would mean re-shaping the whole
        // string here too. ShapeLayer::render()'s culling test only needs
        // this to not *under*-estimate (see Shape::boundsWithEffects()'s
        // own comment), so a generous approximation is fine.
        Rect localBounds() const override;

    protected:
        void buildPath(BLPath& path) const override;

    private:
        std::string text_;
        Font font_;
        float startOffset_ = 0.0f;
    };

    // A group of Shapes painted together, with their own opacity/
    // compositingOp applied as a unit on top of each shape's own - the
    // layer-vs-shape split Photoshop's own Layers panel has.
    //
    // shapes_ is a plain std::vector<Shape*> (each entry heap-allocated,
    // owned by this layer) rather than std::vector<std::unique_ptr<Shape>> -
    // same raw-pointer-plus-explicit-delete ownership convention
    // View::childViews_ already uses (view.h), so this fits the existing
    // container_traits<std::vector<T>> reflection machinery directly
    // rather than needing a move-only-element specialization for it.
    class ShapeLayer {
    public:
        ShapeLayer() = default;

        // Explicitly non-copyable: shapes_ owns its Shape* pointers (see
        // clear()/~ShapeLayer()), so the implicitly-generated copy
        // constructor's shallow pointer copy would leave two ShapeLayers
        // both deleting the same Shape instances - a double-free waiting
        // to happen. Move is fine as-is (nothing here blocks it - see
        // Fill's own class comment, graphics.h, for the shape of problem
        // this *isn't*: no move-only member here, just an ownership
        // invariant a naive copy would silently violate).
        ShapeLayer(const ShapeLayer&) = delete;
        ShapeLayer& operator=(const ShapeLayer&) = delete;

        ~ShapeLayer() {
            clear();
        }

        float opacity() const { return opacity_; }
        void setOpacity(float opacity) { opacity_ = opacity; }

        CompositingFlag compositingOp() const { return compositingOp_; }
        void setCompositingOp(CompositingFlag op) { compositingOp_ = op; }

        // Takes ownership of shape (must be heap-allocated) - deleted by
        // clear()/~ShapeLayer() along with every other shape already here.
        void addShape(Shape* shape) {
            shapes_.push_back(shape);
        }

        std::vector<Shape*>& shapes() { return shapes_; }
        const std::vector<Shape*>& shapes() const { return shapes_; }

        void clear() {
            for (Shape* shape : shapes_) {
                delete shape;
            }
            shapes_.clear();
        }

        // Paints every shape whose worldBoundsWithEffects() (in ctx's
        // current coordinate space) intersects visibleRect - a shape
        // entirely outside it is skipped without being rendered at all.
        // visibleRect is a culling test only, not a clip: a shape that's
        // only partially inside still paints in full, including whatever
        // part falls outside it - this doesn't clip ctx itself, since a
        // glow/drop shadow legitimately wants to bleed past a shape's own
        // bounds; a caller that wants hard clipping too can still call
        // ctx.clip_to_rect() itself first.
        void render(BLContext& ctx, const Rect& visibleRect) const;

    private:
        std::vector<Shape*> shapes_;
        float opacity_ = 1.0f;
        CompositingFlag compositingOp_ = CompSrcOver;
    };

}

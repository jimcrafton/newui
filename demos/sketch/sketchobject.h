#pragma once

#include "sketchcolor.h"

#include <blend2d/blend2d.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sketch {

    // One color stop of a gradient: offset 0..1 along the gradient line.
    // @reflect ignore=true
    struct GradientStop {
        double offset = 0.0;
        SketchColor color;

        bool operator==(const GradientStop& other) const { return offset == other.offset && color == other.color; }
    };

    enum class GradientSpread { Pad, Repeat, Reflect };

    // A gradient described relative to the object it paints: linear gradients run along `angleDegrees`
    // (0 = left to right, 90 = top to bottom) across the object's bounding box; radial ones are centered on
    // it. Because nothing is stored in document coordinates, a gradient follows its object through moves,
    // resizes and rotations without any bookkeeping. Stops are kept sorted by offset.
    // @reflect ignore=true
    class SketchGradient {
    public:
        std::vector<GradientStop> stops;
        double angleDegrees = 0.0;
        GradientSpread spread = GradientSpread::Pad;

        // An explicit axis, set by the Gradient tool or read from a file: two points kept as fractions of the
        // object's bounding box (0,0 = top-left, 1,1 = bottom-right, may lie outside), so it too follows its
        // object. Linear: where the gradient starts (offset 0) and ends (offset 1). Radial: the center and a
        // point on the outer circle. Without it the axis is derived from angleDegrees and the box (linear:
        // edge to edge through the center; radial: centered, reaching the box's longer half-side).
        bool hasAxis = false;
        double axisX1 = 0.0, axisY1 = 0.5, axisX2 = 1.0, axisY2 = 0.5;
        // Radial only: the radius across the axis as a multiple of the radius along it (1 = a circle). The
        // ellipse is turned so its long/short sides line up with the axis direction.
        double axisAspect = 1.0;
        // How far the lines of equal color lean away from square to the axis, as the tangent of that lean (0 =
        // square; a gradient under a skewing transform has it). Linear: the iso-color lines run along
        // (skew * axis direction + across direction). Radial: the ellipse's across radius leans the same way.
        double axisSkew = 0.0;
        // Stroke paints only: the colors run along the stroke's path (by its length) instead of across the
        // object's bounding box. The axis, angle and radial/linear kind then do not matter.
        bool alongStroke = false;

        // The matrix that turns the plain gradient (a circle of radius |end - start| around `start`, or the
        // linear gradient from `start` to `end`) into the ellipse / leaning gradient described by aspect and
        // skew; identity when there is neither.
        BLMatrix2D axisMatrix(const BLPoint& start, const BLPoint& end, bool radial) const;

        // The effective axis over `box`, in document units.
        void axisPoints(const BLBox& box, bool radial, BLPoint& start, BLPoint& end) const;
        // Fixes the axis to these document points (and, for a linear gradient, the angle to match).
        void setAxis(const BLBox& box, const BLPoint& start, const BLPoint& end);
        // Turns the gradient line to `degrees` about its midpoint, keeping its length (a no-op on the axis
        // itself unless hasAxis); always updates angleDegrees.
        void setAngle(const BLBox& box, double degrees);

        // `color` fading to white - a reasonable starting gradient when a solid fill is switched to one.
        static SketchGradient fromColor(const SketchColor& color);

        // Sorts by offset (stable).
        void sortStops();
        // Interpolated color at offset t (clamped to the first/last stop); transparent black when empty.
        SketchColor colorAt(double t) const;
        // Mirrors every stop about the middle of the gradient.
        void reverse();

        bool operator==(const SketchGradient& other) const {
            return angleDegrees == other.angleDegrees && spread == other.spread && stops == other.stops && hasAxis == other.hasAxis &&
                alongStroke == other.alongStroke &&
                (!hasAxis || (axisX1 == other.axisX1 && axisY1 == other.axisY1 && axisX2 == other.axisX2 && axisY2 == other.axisY2 &&
                    axisAspect == other.axisAspect && axisSkew == other.axisSkew));
        }
        bool operator!=(const SketchGradient& other) const { return !(*this == other); }
    };

    // What fill and stroke hold: nothing, a solid color or a gradient.
    class SketchPaint {
    public:
        enum class Kind { None, Solid, Linear, Radial };

        SketchPaint() = default;
        static SketchPaint none() { return SketchPaint(); }
        static SketchPaint solid(const SketchColor& color);
        static SketchPaint linear(const SketchGradient& gradient);
        static SketchPaint radial(const SketchGradient& gradient);

        Kind kind() const { return kind_; }
        bool isNone() const { return kind_ == Kind::None; }
        bool isGradient() const { return kind_ == Kind::Linear || kind_ == Kind::Radial; }
        // The solid color; for a gradient, its first stop (so color-only callers keep working).
        const SketchColor& color() const { return color_; }
        const SketchGradient& gradient() const { return gradient_; }

        bool operator==(const SketchPaint& other) const {
            if (kind_ != other.kind_) {
                return false;
            }
            if (kind_ == Kind::None) {
                return true;
            }
            return isGradient() ? gradient_ == other.gradient_ : color_ == other.color_;
        }
        bool operator!=(const SketchPaint& other) const { return !(*this == other); }

    private:
        Kind kind_ = Kind::None;
        SketchColor color_;
        SketchGradient gradient_;
    };

    // How the ends and corners of a stroke look (the Stroke panel's Cap / Join / Miter).
    enum class StrokeCap { Butt, Round, Square };
    enum class StrokeJoin { Miter, Round, Bevel };
    // One point of a variable-width stroke (the Width tool): `t` is the distance along the path as a fraction
    // 0..1 (per subpath); `left`/`right` are the half-widths of the two sides as multiples of half the stroke
    // weight (1 = the plain stroke). Between points the width eases from one to the next; outside the first and
    // last point it stays at theirs.
    // @reflect ignore=true
    struct WidthPoint {
        double t = 0.0;
        double left = 1.0;
        double right = 1.0;
        bool operator==(const WidthPoint& o) const { return t == o.t && left == o.left && right == o.right; }
    };
    // The eased multiplier at `t` for one side (1 when there are no points). `points` must be sorted by t.
    double widthScaleAt(const std::vector<WidthPoint>& points, double t, bool leftSide);

    // @reflect ignore=true
    struct StrokeOptions {
        StrokeCap cap = StrokeCap::Butt;
        StrokeJoin join = StrokeJoin::Miter;
        // A miter longer than this many stroke widths is bevelled (Illustrator's default is 10).
        double miterLimit = 10.0;
        // Dash, gap, dash, gap... in points (SVG stroke-dasharray); empty = solid. Odd counts repeat, as in SVG.
        std::vector<double> dashes;
        // Variable width, sorted by t; empty = the plain stroke weight all along. Drawn as a filled outline, so
        // dashes and "gradient along stroke" do not apply while it is set.
        std::vector<WidthPoint> widths;

        bool operator==(const StrokeOptions& other) const {
            return cap == other.cap && join == other.join && miterLimit == other.miterLimit && dashes == other.dashes && widths == other.widths;
        }
        bool operator!=(const StrokeOptions& other) const { return !(*this == other); }
    };

    // How an object's paint combines with what is under it (the Appearance panel's Blend list; the order is the
    // list's order). Normal is plain source-over.
    enum class BlendMode { Normal, Multiply, Screen, Overlay, Darken, Lighten, ColorDodge, ColorBurn, HardLight, SoftLight, Difference, Exclusion };
    // The mode's name as the panel shows it ("Color Dodge") and as CSS mix-blend-mode spells it ("color-dodge").
    const char* blendModeName(BlendMode mode);
    const char* blendModeCss(BlendMode mode);
    // Parses a CSS mix-blend-mode value; unknown values (and "normal") give Normal.
    BlendMode blendModeFromCss(const std::string& value);

    // One entry of an object's appearance stack, painted bottom to top: a fill or a stroke with its own paint, opacity,
    // blend mode and visibility (a stroke also has its weight and options). Every object keeps at least one fill and one
    // stroke - the "primary" ones, which fill()/stroke()/strokeWidth()/strokeOptions() read and the old setters write;
    // either may be a paint of None.
    enum class AppearanceKind { Fill, Stroke, Effect };
    // Live effects, in the order of the Add Effect menu. An effect acts on everything below it in the stack: a drop
    // shadow / outer glow is cast behind that composite, Gaussian Blur blurs it, Round Corners rounds the outline of the
    // fills and strokes below it.
    enum class EffectKind { DropShadow, OuterGlow, GaussianBlur, RoundCorners };
    const char* effectName(EffectKind kind);
    // @reflect ignore=true
    struct AppearanceItem {
        AppearanceKind kind = AppearanceKind::Fill;
        bool visible = true;
        double opacity = 1.0;
        // Normal means "follow the object's blend mode".
        BlendMode blendMode = BlendMode::Normal;
        SketchPaint paint;
        // Strokes only.
        double strokeWidth = 1.0;
        StrokeOptions strokeOptions;
        // Effects only (paint and the stroke fields are unused; `opacity` is the effect's amount and `blendMode` how a
        // shadow / glow is composited).
        EffectKind effect = EffectKind::DropShadow;
        SketchColor color = SketchColor::gray(0.0f);  // shadow / glow tint
        double softness = 6.0;                       // blur radius in document units (shadow, glow, blur)
        double offsetX = 4.0, offsetY = 4.0;         // shadow offset
        double radius = 10.0;                        // Round Corners

        static AppearanceItem makeFill(const SketchPaint& paint) {
            AppearanceItem item;
            item.kind = AppearanceKind::Fill;
            item.paint = paint;
            return item;
        }
        static AppearanceItem makeStroke(const SketchPaint& paint, double width = 1.0, const StrokeOptions& options = StrokeOptions()) {
            AppearanceItem item;
            item.kind = AppearanceKind::Stroke;
            item.paint = paint;
            item.strokeWidth = width;
            item.strokeOptions = options;
            return item;
        }
        // A new effect with the defaults of its kind.
        static AppearanceItem makeEffect(EffectKind effect) {
            AppearanceItem item;
            item.kind = AppearanceKind::Effect;
            item.effect = effect;
            item.paint = SketchPaint::none();
            switch (effect) {
                case EffectKind::DropShadow: item.opacity = 0.75; item.blendMode = BlendMode::Multiply; break;
                case EffectKind::OuterGlow: item.opacity = 0.75; item.color = SketchColor::fromHex("#ffe066"); item.softness = 8.0; break;
                case EffectKind::GaussianBlur: item.softness = 4.0; break;
                case EffectKind::RoundCorners: break;
            }
            return item;
        }
        bool operator==(const AppearanceItem& o) const {
            return kind == o.kind && visible == o.visible && opacity == o.opacity && blendMode == o.blendMode && paint == o.paint &&
                strokeWidth == o.strokeWidth && strokeOptions == o.strokeOptions && effect == o.effect && color == o.color &&
                softness == o.softness && offsetX == o.offsetX && offsetY == o.offsetY && radius == o.radius;
        }
        bool operator!=(const AppearanceItem& o) const { return !(*this == o); }
    };
    // The item `stored` names when it is of the kind, else the first item of the kind, else a none-painted placeholder.
    const AppearanceItem& activeItemOf(const std::vector<AppearanceItem>& items, AppearanceKind kind, std::size_t stored);
    // The index activeItemOf() resolves to (items.size() when there is no item of the kind).
    std::size_t activeIndexOf(const std::vector<AppearanceItem>& items, AppearanceKind kind, std::size_t stored);

    // Draws `content` into an offscreen layer covering `userBounds` (document units) at the context's own device
    // resolution, then composites the layer with `opacity` and the blend mode - so everything in the layer blends as one
    // piece (overlapping fills and strokes do not double up). Used for objects and groups with opacity < 1 or a blend mode.
    void paintIsolated(BLContext& ctx, const BLBox& userBounds, double opacity, BlendMode mode, const std::function<void(BLContext&)>& content);

    enum class SketchObjectType { Line, Rect, RoundRect, Ellipse, Polygon, Polyline, Path };

    // One drawable thing. Geometry is kept as a BLPath in document units (points, y down) so
    // rendering, bounds and hit-testing all share it; the typed factories build that path.
    // Mutating an object is the document's job (undoable commands arrive in S2) - these setters
    // are what those commands call.
    class SketchObject {
    public:
        static SketchObject* makeLine(double x1, double y1, double x2, double y2);
        static SketchObject* makeRect(double x, double y, double width, double height);
        static SketchObject* makeRoundRect(double x, double y, double width, double height, double radius);
        static SketchObject* makeEllipse(double cx, double cy, double rx, double ry);
        static SketchObject* makePolygon(const std::vector<BLPoint>& points);
        static SketchObject* makePolyline(const std::vector<BLPoint>& points);
        static SketchObject* makePath(const BLPath& path);
        // An object of the given type around an existing outline (what the SVG reader uses).
        static SketchObject* make(SketchObjectType type, const BLPath& path);

        SketchObjectType type() const { return type_; }
        std::size_t id() const { return id_; }
        // Assigned by SketchDocument::add(); 0 means "not in a document".
        void setId(std::size_t id) { id_ = id; }

        const std::string& name() const { return name_; }
        void setName(const std::string& name) { name_ = name; }

        bool isVisible() const { return visible_; }
        void setVisible(bool value) { visible_ = value; }
        bool isLocked() const { return locked_; }
        void setLocked(bool value) { locked_ = value; }

        // The appearance stack, bottom to top. One fill and one stroke are "active" (the Appearance panel's selected
        // row of that kind): fill()/stroke()/strokeWidth()/strokeOptions() and their setters - and so the Color and Stroke
        // panels and the gradient / width tools - work on those. setAppearance keeps at least one fill and one stroke
        // (a none-painted one is added when missing).
        const std::vector<AppearanceItem>& appearance() const { return items_; }
        void setAppearance(const std::vector<AppearanceItem>& items);
        std::size_t activeIndex(AppearanceKind kind) const;
        std::size_t activeFillIndex() const { return activeFill_; }
        std::size_t activeStrokeIndex() const { return activeStroke_; }
        std::size_t activeEffectIndex() const { return activeEffect_; }
        // Makes items()[index] the active one of its kind; ignored when index is out of range.
        void setActiveIndex(std::size_t index);
        void setActiveIndices(std::size_t fill, std::size_t stroke, std::size_t effect = 0) { activeFill_ = fill; activeStroke_ = stroke; activeEffect_ = effect; }
        const AppearanceItem& activeItem(AppearanceKind kind) const { return activeItemOf(items_, kind, storedActive(kind)); }
        const SketchPaint& fill() const { return activeItem(AppearanceKind::Fill).paint; }
        void setFill(const SketchPaint& paint) { primary(AppearanceKind::Fill).paint = paint; }
        const SketchPaint& stroke() const { return activeItem(AppearanceKind::Stroke).paint; }
        void setStroke(const SketchPaint& paint) { primary(AppearanceKind::Stroke).paint = paint; }
        double strokeWidth() const { return activeItem(AppearanceKind::Stroke).strokeWidth; }
        void setStrokeWidth(double width);
        const StrokeOptions& strokeOptions() const { return activeItem(AppearanceKind::Stroke).strokeOptions; }
        void setStrokeOptions(const StrokeOptions& options) { primary(AppearanceKind::Stroke).strokeOptions = options; }
        // The active item of the kind, for editing (opacity, blend mode, visibility); null for an effect when there is none.
        AppearanceItem* activeItemPtr(AppearanceKind kind);
        // How far the visible pixel effects (shadow, glow, blur) can reach past bounds(), and bounds() grown by that.
        double effectPad() const;
        BLBox paintBounds() const;

        // Whole-object opacity 0..1 (composited over fill+stroke together).
        double opacity() const { return opacity_; }
        void setOpacity(double opacity);
        // The groups this object is in, outermost first (ids from SketchDocument::newGroupId). Members of a group are
        // adjacent in the document's stacking order; the document keeps it that way. Empty = not grouped.
        const std::vector<std::size_t>& groups() const { return groups_; }
        void setGroups(const std::vector<std::size_t>& groups) { groups_ = groups; }
        // How the whole object (fill and stroke) is blended with what is below it.
        BlendMode blendMode() const { return blendMode_; }
        void setBlendMode(BlendMode mode) { blendMode_ = mode; }

        const BLPath& geometry() const { return path_; }
        // Replaces the outline (undo restores the pre-edit one through this).
        void setGeometry(const BLPath& path) { path_ = path; }
        // Bakes a transform into the outline - move/resize/rotate all go through here, so stroke width
        // stays put and handles are always axis-aligned on geometryBounds().
        void transform(const BLMatrix2D& matrix);
        // The angle (radians) the object's selection frame is turned by - the sum of the rotations
        // applied to it. The outline itself is baked, so this only orients the handles.
        double frameAngle() const { return frameAngle_; }
        void setFrameAngle(double angle) { frameAngle_ = angle; }
        // Bounds of the outline measured in the rotated frame (points un-rotated by -frameAngle()
        // about the origin); equal to geometryBounds() while frameAngle() is 0.
        BLBox frameBounds() const;
        // Moves every outline vertex (curve control points included) through `map` - how a perspective
        // warp is applied. Control points are mapped, not re-fitted, so strong distortion of a curve is approximate.
        void warp(const std::function<BLPoint(const BLPoint&)>& map);
        // Axis-aligned bounds of the outline alone (no stroke) - what the selection frame wraps.
        BLBox geometryBounds() const;
        // A copy with id 0 (the document assigns one on add) and the same appearance.
        SketchObject* clone() const;
        // Closed shapes (everything except Line/Polyline) take a fill; open ones ignore it.
        bool isClosed() const;

        // Axis-aligned bounds of the geometry plus half the stroke width.
        BLBox bounds() const;

        // The point and unit "left" normal at fraction t (0..1 of the length) along the first subpath, flattened;
        // false when the outline has no subpath. The sides of a WidthPoint are offsets along +normal (left) and -normal.
        bool pathSample(double t, BLPoint& point, BLPoint& normal) const;
        // The fraction along the first subpath nearest to `point` and how far away it is (document units).
        bool nearestOnPath(const BLPoint& point, double& t, double& distance) const;

        // True when point (document units) is on the fill or within tolerance of the stroke.
        bool hitTest(const BLPoint& point, double tolerance) const;

        // Fills and strokes in stacking order; with an object opacity below 1 or a blend mode the whole object is drawn
        // through paintIsolated().
        void paint(BLContext& ctx) const;

    private:
        SketchObject(SketchObjectType type, const BLPath& path);
        // The appearance items only (their own opacity and blend mode, no object-level ones).
        void paintItems(BLContext& ctx) const;
        // Everything through an offscreen layer so the pixel effects can act on what is below them.
        void paintWithEffects(BLContext& ctx) const;
        void drawItem(BLContext& ctx, std::size_t index, const BLBox& box) const;
        std::size_t storedActive(AppearanceKind kind) const { return kind == AppearanceKind::Fill ? activeFill_ : (kind == AppearanceKind::Stroke ? activeStroke_ : activeEffect_); }
        // The active item of the kind (the first one, added when missing: fills at the bottom, strokes on top).
        AppearanceItem& primary(AppearanceKind kind);

        SketchObjectType type_;
        BLPath path_;
        std::size_t id_ = 0;
        std::string name_;
        bool visible_ = true;
        bool locked_ = false;
        std::vector<AppearanceItem> items_ = {AppearanceItem::makeFill(SketchPaint::solid(SketchColor::gray(1.0f))),
            AppearanceItem::makeStroke(SketchPaint::solid(SketchColor::gray(0.0f)), 1.0)};
        double opacity_ = 1.0;
        BlendMode blendMode_ = BlendMode::Normal;
        std::vector<std::size_t> groups_;
        std::size_t activeFill_ = 0;    // indices into items_ (validated on use)
        std::size_t activeStroke_ = 1;
        std::size_t activeEffect_ = 0;
        double frameAngle_ = 0.0;
    };

}

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

        // `color` fading to white - a reasonable starting gradient when a solid fill is switched to one.
        static SketchGradient fromColor(const SketchColor& color);

        // Sorts by offset (stable).
        void sortStops();
        // Interpolated color at offset t (clamped to the first/last stop); transparent black when empty.
        SketchColor colorAt(double t) const;
        // Mirrors every stop about the middle of the gradient.
        void reverse();

        bool operator==(const SketchGradient& other) const {
            return angleDegrees == other.angleDegrees && spread == other.spread && stops == other.stops;
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

        const SketchPaint& fill() const { return fill_; }
        void setFill(const SketchPaint& paint) { fill_ = paint; }
        const SketchPaint& stroke() const { return stroke_; }
        void setStroke(const SketchPaint& paint) { stroke_ = paint; }
        double strokeWidth() const { return strokeWidth_; }
        void setStrokeWidth(double width);

        // Whole-object opacity 0..1 (composited over fill+stroke together).
        double opacity() const { return opacity_; }
        void setOpacity(double opacity);

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

        // True when point (document units) is on the fill or within tolerance of the stroke.
        bool hitTest(const BLPoint& point, double tolerance) const;

        void paint(BLContext& ctx) const;

    private:
        SketchObject(SketchObjectType type, const BLPath& path);

        SketchObjectType type_;
        BLPath path_;
        std::size_t id_ = 0;
        std::string name_;
        bool visible_ = true;
        bool locked_ = false;
        SketchPaint fill_ = SketchPaint::solid(SketchColor::gray(1.0f));
        SketchPaint stroke_ = SketchPaint::solid(SketchColor::gray(0.0f));
        double strokeWidth_ = 1.0;
        double opacity_ = 1.0;
        double frameAngle_ = 0.0;
    };

}

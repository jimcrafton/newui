#include "sketchobject.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace sketch {

    SketchPaint SketchPaint::solid(const SketchColor& color) {
        SketchPaint paint;
        paint.kind_ = Kind::Solid;
        paint.color_ = color;
        return paint;
    }

    SketchPaint SketchPaint::linear(const SketchGradient& gradient) {
        SketchPaint paint;
        paint.kind_ = Kind::Linear;
        paint.gradient_ = gradient;
        paint.color_ = gradient.stops.empty() ? SketchColor() : gradient.stops.front().color;
        return paint;
    }

    SketchPaint SketchPaint::radial(const SketchGradient& gradient) {
        SketchPaint paint = linear(gradient);
        paint.kind_ = Kind::Radial;
        return paint;
    }

    SketchGradient SketchGradient::fromColor(const SketchColor& color) {
        SketchGradient gradient;
        gradient.stops.push_back({0.0, color});
        gradient.stops.push_back({1.0, SketchColor::rgb(1.0f, 1.0f, 1.0f, color.alpha())});
        return gradient;
    }

    void SketchGradient::sortStops() {
        std::stable_sort(stops.begin(), stops.end(),
            [](const GradientStop& a, const GradientStop& b) { return a.offset < b.offset; });
    }

    SketchColor SketchGradient::colorAt(double t) const {
        if (stops.empty()) {
            return SketchColor::rgb(0.0f, 0.0f, 0.0f, 0.0f);
        }
        if (t <= stops.front().offset) {
            return stops.front().color;
        }
        if (t >= stops.back().offset) {
            return stops.back().color;
        }
        for (std::size_t i = 1; i < stops.size(); ++i) {
            if (t <= stops[i].offset) {
                const GradientStop& lo = stops[i - 1];
                const GradientStop& hi = stops[i];
                double span = hi.offset - lo.offset;
                float f = span > 1e-12 ? float((t - lo.offset) / span) : 0.0f;
                BLRgba32 a = lo.color.toRgba32();
                BLRgba32 b = hi.color.toRgba32();
                auto mix = [f](std::uint32_t x, std::uint32_t y) { return (float(x) + (float(y) - float(x)) * f) / 255.0f; };
                return SketchColor::rgb(mix(a.r(), b.r()), mix(a.g(), b.g()), mix(a.b(), b.b()), mix(a.a(), b.a()));
            }
        }
        return stops.back().color;
    }

    void SketchGradient::reverse() {
        for (GradientStop& stop : stops) {
            stop.offset = 1.0 - stop.offset;
        }
        std::reverse(stops.begin(), stops.end());
    }

    namespace {
        // Applies a paint to ctx as the fill or stroke style over `box` (the object's geometry bounds),
        // returning false when there is nothing to paint.
        bool setPaintStyle(BLContext& ctx, const SketchPaint& paint, const BLBox& box, bool asStroke) {
            if (paint.isNone()) {
                return false;
            }
            double w = box.x1 - box.x0, h = box.y1 - box.y0;
            if (!paint.isGradient() || paint.gradient().stops.empty() || (w < 1e-6 && h < 1e-6)) {
                BLRgba32 color = paint.color().toRgba32();
                if (asStroke) { ctx.set_stroke_style(color); } else { ctx.set_fill_style(color); }
                return true;
            }

            const SketchGradient& g = paint.gradient();
            double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
            BLGradient gradient;
            if (paint.kind() == SketchPaint::Kind::Linear) {
                double angle = g.angleDegrees * 3.14159265358979323846 / 180.0;
                double dx = std::cos(angle), dy = std::sin(angle);
                // Half the box's extent along the gradient direction, so 0 and 1 land on the box's edges.
                double half = (std::fabs(w * dx) + std::fabs(h * dy)) * 0.5;
                gradient = BLGradient(BLLinearGradientValues(cx - dx * half, cy - dy * half, cx + dx * half, cy + dy * half));
            } else {
                double radius = std::max(w, h) * 0.5;
                gradient = BLGradient(BLRadialGradientValues(cx, cy, cx, cy, radius > 1e-9 ? radius : 1e-9));
            }
            for (const GradientStop& stop : g.stops) {
                gradient.add_stop(std::min(1.0, std::max(0.0, stop.offset)), stop.color.toRgba32());
            }
            gradient.set_extend_mode(g.spread == GradientSpread::Repeat ? BL_EXTEND_MODE_REPEAT
                : (g.spread == GradientSpread::Reflect ? BL_EXTEND_MODE_REFLECT : BL_EXTEND_MODE_PAD));
            if (asStroke) { ctx.set_stroke_style(gradient); } else { ctx.set_fill_style(gradient); }
            return true;
        }
    }

    SketchObject::SketchObject(SketchObjectType type, const BLPath& path) : type_(type), path_(path) {
    }

    SketchObject* SketchObject::makeLine(double x1, double y1, double x2, double y2) {
        BLPath path;
        path.move_to(x1, y1);
        path.line_to(x2, y2);
        SketchObject* object = new SketchObject(SketchObjectType::Line, path);
        object->setFill(SketchPaint::none());
        return object;
    }

    SketchObject* SketchObject::makeRect(double x, double y, double width, double height) {
        BLPath path;
        path.add_rect(BLRect(x, y, width, height));
        return new SketchObject(SketchObjectType::Rect, path);
    }

    SketchObject* SketchObject::makeRoundRect(double x, double y, double width, double height, double radius) {
        BLPath path;
        path.add_round_rect(BLRoundRect(x, y, width, height, radius));
        return new SketchObject(SketchObjectType::RoundRect, path);
    }

    SketchObject* SketchObject::makeEllipse(double cx, double cy, double rx, double ry) {
        BLPath path;
        path.add_ellipse(BLEllipse(cx, cy, rx, ry));
        return new SketchObject(SketchObjectType::Ellipse, path);
    }

    SketchObject* SketchObject::makePolygon(const std::vector<BLPoint>& points) {
        if (points.size() < 3) {
            throw std::runtime_error("SketchObject::makePolygon: need at least 3 points");
        }
        BLPath path;
        path.move_to(points[0]);
        for (std::size_t i = 1; i < points.size(); ++i) {
            path.line_to(points[i]);
        }
        path.close();
        return new SketchObject(SketchObjectType::Polygon, path);
    }

    SketchObject* SketchObject::makePolyline(const std::vector<BLPoint>& points) {
        if (points.size() < 2) {
            throw std::runtime_error("SketchObject::makePolyline: need at least 2 points");
        }
        BLPath path;
        path.move_to(points[0]);
        for (std::size_t i = 1; i < points.size(); ++i) {
            path.line_to(points[i]);
        }
        SketchObject* object = new SketchObject(SketchObjectType::Polyline, path);
        object->setFill(SketchPaint::none());
        return object;
    }

    SketchObject* SketchObject::make(SketchObjectType type, const BLPath& path) {
        return new SketchObject(type, path);
    }

    SketchObject* SketchObject::makePath(const BLPath& path) {
        return new SketchObject(SketchObjectType::Path, path);
    }

    void SketchObject::setStrokeWidth(double width) {
        if (width < 0.0) {
            throw std::runtime_error("SketchObject::setStrokeWidth: width must not be negative");
        }
        strokeWidth_ = width;
    }

    void SketchObject::setOpacity(double opacity) {
        if (opacity < 0.0 || opacity > 1.0) {
            throw std::runtime_error("SketchObject::setOpacity: opacity must be within 0..1");
        }
        opacity_ = opacity;
    }

    bool SketchObject::isClosed() const {
        return type_ != SketchObjectType::Line && type_ != SketchObjectType::Polyline;
    }

    void SketchObject::transform(const BLMatrix2D& matrix) {
        path_.transform(matrix);
    }

    BLBox SketchObject::geometryBounds() const {
        BLBox box(0, 0, 0, 0);
        path_.get_bounding_box(&box);
        return box;
    }

    void SketchObject::warp(const std::function<BLPoint(const BLPoint&)>& map) {
        BLPathView view = path_.view();
        std::vector<uint8_t> commands(view.command_data, view.command_data + view.size);
        std::vector<BLPoint> vertices(view.vertex_data, view.vertex_data + view.size);
        for (std::size_t i = 0; i < commands.size(); ++i) {
            if (commands[i] == BL_PATH_CMD_CLOSE || commands[i] == BL_PATH_CMD_WEIGHT) {
                continue;
            }
            BLPoint mapped = map(vertices[i]);
            path_.set_vertex_at(i, BL_PATH_CMD_PRESERVE, mapped.x, mapped.y);
        }
    }

    BLBox SketchObject::frameBounds() const {
        if (frameAngle_ == 0.0) {
            return geometryBounds();
        }
        BLPath rotated = path_;
        rotated.transform(BLMatrix2D::make_rotation(-frameAngle_));
        BLBox box(0, 0, 0, 0);
        rotated.get_bounding_box(&box);
        return box;
    }

    SketchObject* SketchObject::clone() const {
        SketchObject* copy = new SketchObject(type_, path_);
        copy->name_ = name_;
        copy->visible_ = visible_;
        copy->locked_ = locked_;
        copy->fill_ = fill_;
        copy->stroke_ = stroke_;
        copy->strokeWidth_ = strokeWidth_;
        copy->opacity_ = opacity_;
        copy->frameAngle_ = frameAngle_;
        return copy;
    }

    BLBox SketchObject::bounds() const {
        BLBox box(0, 0, 0, 0);
        path_.get_bounding_box(&box);
        double half = stroke_.isNone() ? 0.0 : strokeWidth_ * 0.5;
        return BLBox(box.x0 - half, box.y0 - half, box.x1 + half, box.y1 + half);
    }

    bool SketchObject::hitTest(const BLPoint& point, double tolerance) const {
        BLBox box = bounds();
        if (point.x < box.x0 - tolerance || point.x > box.x1 + tolerance ||
            point.y < box.y0 - tolerance || point.y > box.y1 + tolerance) {
            return false;
        }
        if (isClosed() && !fill_.isNone() && path_.hit_test(point, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN) {
            return true;
        }
        // Stroke: stroke the outline at (half width + tolerance) and test that area.
        BLStrokeOptions options;
        options.width = (stroke_.isNone() ? 0.0 : strokeWidth_) + tolerance * 2.0;
        if (options.width <= 0.0) {
            return false;
        }
        BLPath outline;
        if (outline.add_stroked_path(path_, options, BLApproximationOptions()) != BL_SUCCESS) {
            return false;
        }
        return outline.hit_test(point, BL_FILL_RULE_NON_ZERO) == BL_HIT_TEST_IN;
    }

    void SketchObject::paint(BLContext& ctx) const {
        if (!visible_) {
            return;
        }
        ctx.save();
        BLBox box = geometryBounds();
        if (isClosed() && setPaintStyle(ctx, fill_, box, false)) {
            ctx.set_global_alpha(opacity_);
            ctx.fill_path(path_);
        }
        if (strokeWidth_ > 0.0 && setPaintStyle(ctx, stroke_, box, true)) {
            ctx.set_stroke_width(strokeWidth_);
            ctx.set_global_alpha(opacity_);
            ctx.stroke_path(path_);
        }
        ctx.restore();
    }

}

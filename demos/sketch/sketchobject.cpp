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

    namespace {
        constexpr double kPi = 3.14159265358979323846;
        // A box side as a divisor: a flat object (a horizontal line) still maps fractions to something.
        double span(double lo, double hi) { return hi - lo < 1e-9 ? 1.0 : hi - lo; }
    }

    void SketchGradient::axisPoints(const BLBox& box, bool radial, BLPoint& start, BLPoint& end) const {
        double w = span(box.x0, box.x1), h = span(box.y0, box.y1);
        if (hasAxis) {
            start = BLPoint(box.x0 + axisX1 * w, box.y0 + axisY1 * h);
            end = BLPoint(box.x0 + axisX2 * w, box.y0 + axisY2 * h);
            return;
        }
        double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        if (radial) {
            start = BLPoint(cx, cy);
            end = BLPoint(cx + std::max(box.x1 - box.x0, box.y1 - box.y0) * 0.5, cy);
            return;
        }
        double angle = angleDegrees * kPi / 180.0;
        double dx = std::cos(angle), dy = std::sin(angle);
        // Half the box's extent along the gradient direction, so 0 and 1 land on the box's edges.
        double half = (std::fabs((box.x1 - box.x0) * dx) + std::fabs((box.y1 - box.y0) * dy)) * 0.5;
        start = BLPoint(cx - dx * half, cy - dy * half);
        end = BLPoint(cx + dx * half, cy + dy * half);
    }

    BLMatrix2D SketchGradient::axisMatrix(const BLPoint& start, const BLPoint& end, bool radial) const {
        BLMatrix2D m = BLMatrix2D::make_identity();
        double aspect = radial ? axisAspect : 1.0;
        if (!hasAxis || (aspect == 1.0 && axisSkew == 0.0)) {
            return m;
        }
        // In the frame of the axis (x along it): squash across it, then lean x by skew * y.
        double angle = std::atan2(end.y - start.y, end.x - start.x);
        m = BLMatrix2D::make_translation(-start.x, -start.y);
        m.post_rotate(-angle);
        m.post_scale(1.0, aspect);
        m.post_transform(BLMatrix2D(1.0, 0.0, axisSkew, 1.0, 0.0, 0.0));
        m.post_rotate(angle);
        m.post_translate(start.x, start.y);
        return m;
    }

    void SketchGradient::setAxis(const BLBox& box, const BLPoint& start, const BLPoint& end) {
        double w = span(box.x0, box.x1), h = span(box.y0, box.y1);
        hasAxis = true;
        axisX1 = (start.x - box.x0) / w;
        axisY1 = (start.y - box.y0) / h;
        axisX2 = (end.x - box.x0) / w;
        axisY2 = (end.y - box.y0) / h;
        double dx = end.x - start.x, dy = end.y - start.y;
        if (std::hypot(dx, dy) > 1e-9) {
            angleDegrees = std::atan2(dy, dx) * 180.0 / kPi;
        }
    }

    void SketchGradient::setAngle(const BLBox& box, double degrees) {
        if (hasAxis) {
            BLPoint a, b;
            axisPoints(box, false, a, b);
            double length = std::hypot(b.x - a.x, b.y - a.y);
            double mx = (a.x + b.x) * 0.5, my = (a.y + b.y) * 0.5;
            double radians = degrees * kPi / 180.0;
            double dx = std::cos(radians) * length * 0.5, dy = std::sin(radians) * length * 0.5;
            setAxis(box, BLPoint(mx - dx, my - dy), BLPoint(mx + dx, my + dy));
        }
        angleDegrees = degrees;
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
            bool radial = paint.kind() != SketchPaint::Kind::Linear;
            BLPoint start, end;
            g.axisPoints(box, radial, start, end);
            BLGradient gradient;
            if (!radial) {
                gradient = BLGradient(BLLinearGradientValues(start.x, start.y, end.x, end.y));
            } else {
                double radius = std::hypot(end.x - start.x, end.y - start.y);
                gradient = BLGradient(BLRadialGradientValues(start.x, start.y, start.x, start.y, radius > 1e-9 ? radius : 1e-9));
            }
            if (g.hasAxis && (g.axisSkew != 0.0 || (radial && g.axisAspect != 1.0))) {
                gradient.set_transform(g.axisMatrix(start, end, radial));
            }
            for (const GradientStop& stop : g.stops) {
                gradient.add_stop(std::min(1.0, std::max(0.0, stop.offset)), stop.color.toRgba32());
            }
            gradient.set_extend_mode(g.spread == GradientSpread::Repeat ? BL_EXTEND_MODE_REPEAT
                : (g.spread == GradientSpread::Reflect ? BL_EXTEND_MODE_REFLECT : BL_EXTEND_MODE_PAD));
            if (asStroke) { ctx.set_stroke_style(gradient); } else { ctx.set_fill_style(gradient); }
            return true;
        }

        BLStrokeCap blCap(StrokeCap cap) {
            return cap == StrokeCap::Round ? BL_STROKE_CAP_ROUND : (cap == StrokeCap::Square ? BL_STROKE_CAP_SQUARE : BL_STROKE_CAP_BUTT);
        }

        BLStrokeJoin blJoin(StrokeJoin join) {
            // SVG's miter falls back to a bevel past the miter limit.
            return join == StrokeJoin::Round ? BL_STROKE_JOIN_ROUND : (join == StrokeJoin::Bevel ? BL_STROKE_JOIN_BEVEL : BL_STROKE_JOIN_MITER_BEVEL);
        }

        // The path as polylines (curves sampled), one per subpath.
        struct Polyline {
            std::vector<BLPoint> points;
            bool closed = false;
        };

        std::vector<Polyline> flattenPath(const BLPath& path) {
            std::vector<Polyline> result;
            const uint8_t* cmd = path.command_data();
            const BLPoint* v = path.vertex_data();
            std::size_t n = path.size();
            auto curveSteps = [](double length) { return std::min(96, std::max(8, int(length / 3.0))); };
            for (std::size_t i = 0; i < n; ++i) {
                switch (cmd[i]) {
                    case BL_PATH_CMD_MOVE:
                        result.emplace_back();
                        result.back().points.push_back(v[i]);
                        break;
                    case BL_PATH_CMD_ON:
                        if (!result.empty()) { result.back().points.push_back(v[i]); }
                        break;
                    case BL_PATH_CMD_QUAD:
                        if (i + 1 < n && !result.empty()) {
                            BLPoint p0 = result.back().points.back(), c = v[i], p1 = v[i + 1];
                            int steps = curveSteps(std::hypot(c.x - p0.x, c.y - p0.y) + std::hypot(p1.x - c.x, p1.y - c.y));
                            for (int s = 1; s <= steps; ++s) {
                                double t = double(s) / steps, u = 1.0 - t;
                                result.back().points.push_back(BLPoint(u * u * p0.x + 2 * u * t * c.x + t * t * p1.x,
                                    u * u * p0.y + 2 * u * t * c.y + t * t * p1.y));
                            }
                        }
                        i += 1;
                        break;
                    case BL_PATH_CMD_CUBIC:
                        if (i + 2 < n && !result.empty()) {
                            BLPoint p0 = result.back().points.back(), c1 = v[i], c2 = v[i + 1], p1 = v[i + 2];
                            int steps = curveSteps(std::hypot(c1.x - p0.x, c1.y - p0.y) + std::hypot(c2.x - c1.x, c2.y - c1.y) +
                                std::hypot(p1.x - c2.x, p1.y - c2.y));
                            for (int s = 1; s <= steps; ++s) {
                                double t = double(s) / steps, u = 1.0 - t;
                                result.back().points.push_back(BLPoint(
                                    u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p1.x,
                                    u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p1.y));
                            }
                        }
                        i += 2;
                        break;
                    case BL_PATH_CMD_CLOSE:
                        if (!result.empty()) {
                            result.back().closed = true;
                            result.emplace_back();  // a path may continue from the start point; MOVE normally follows
                            result.back().points.push_back(result[result.size() - 2].points.front());
                        }
                        break;
                    default: break;
                }
            }
            result.erase(std::remove_if(result.begin(), result.end(), [](const Polyline& p) { return p.points.size() < 2; }), result.end());
            return result;
        }

        // Strokes `path` with the gradient running along its length: the stroke is cut into short butt-ended
        // pieces, each a solid color picked from the gradient at its position. Pieces overlap their successor a
        // little so no seams show, and an opaque stroke gets a disc at every vertex so bends stay filled (which
        // rounds the joins - the other stroke settings have no say here yet).
        void strokeAlongPath(BLContext& ctx, const BLPath& path, double width, const SketchGradient& gradient, bool opaque) {
            std::vector<Polyline> lines = flattenPath(path);
            double total = 0.0;
            for (Polyline& line : lines) {
                if (line.closed) {
                    line.points.push_back(line.points.front());
                }
                for (std::size_t i = 1; i < line.points.size(); ++i) {
                    total += std::hypot(line.points[i].x - line.points[i - 1].x, line.points[i].y - line.points[i - 1].y);
                }
            }
            if (total < 1e-9) {
                return;
            }
            auto colorAtLength = [&](double length) {
                double t = length / total;
                if (gradient.spread == GradientSpread::Repeat) {
                    t -= std::floor(t);
                } else if (gradient.spread == GradientSpread::Reflect) {
                    t = std::fabs(t - 2.0 * std::floor(t * 0.5) - 1.0);
                    t = 1.0 - t;
                }
                return gradient.colorAt(t).toRgba32();
            };
            double step = std::max(total / 400.0, 0.5);
            double overlap = 0.35;
            ctx.set_stroke_width(width);
            double done = 0.0;
            for (const Polyline& line : lines) {
                for (std::size_t i = 1; i < line.points.size(); ++i) {
                    BLPoint a = line.points[i - 1], b = line.points[i];
                    double length = std::hypot(b.x - a.x, b.y - a.y);
                    if (length < 1e-9) {
                        continue;
                    }
                    double ux = (b.x - a.x) / length, uy = (b.y - a.y) / length;
                    int pieces = std::max(1, int(std::ceil(length / step)));
                    for (int k = 0; k < pieces; ++k) {
                        double s0 = length * k / pieces, s1 = length * (k + 1) / pieces;
                        bool last = k == pieces - 1 && i + 1 == line.points.size();
                        double s1x = last ? s1 : s1 + overlap;
                        BLPath piece;
                        piece.move_to(a.x + ux * s0, a.y + uy * s0);
                        piece.line_to(a.x + ux * s1x, a.y + uy * s1x);
                        ctx.set_stroke_style(colorAtLength(done + (s0 + s1) * 0.5));
                        ctx.stroke_path(piece);
                    }
                    if (opaque) {  // the vertex at the start of this segment
                        ctx.set_fill_style(colorAtLength(done));
                        ctx.fill_circle(BLCircle(a.x, a.y, width * 0.5));
                    }
                    done += length;
                }
                if (opaque && !line.points.empty()) {
                    ctx.set_fill_style(colorAtLength(done));
                    if (line.closed) {
                        ctx.fill_circle(BLCircle(line.points.back().x, line.points.back().y, width * 0.5));
                    }
                }
            }
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
        copy->strokeOptions_ = strokeOptions_;
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
        options.start_cap = options.end_cap = uint8_t(blCap(strokeOptions_.cap));
        options.join = uint8_t(blJoin(strokeOptions_.join));
        options.miter_limit = strokeOptions_.miterLimit;
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
        if (strokeWidth_ > 0.0 && stroke_.isGradient() && stroke_.gradient().alongStroke && !stroke_.gradient().stops.empty()) {
            bool opaque = opacity_ >= 1.0;
            for (const GradientStop& stop : stroke_.gradient().stops) {
                opaque = opaque && stop.color.alpha() >= 1.0f;
            }
            ctx.set_global_alpha(opacity_);
            strokeAlongPath(ctx, path_, strokeWidth_, stroke_.gradient(), opaque);
        } else if (strokeWidth_ > 0.0 && setPaintStyle(ctx, stroke_, box, true)) {
            ctx.set_stroke_width(strokeWidth_);
            ctx.set_stroke_caps(blCap(strokeOptions_.cap));
            ctx.set_stroke_join(blJoin(strokeOptions_.join));
            ctx.set_stroke_miter_limit(strokeOptions_.miterLimit);
            ctx.set_global_alpha(opacity_);
            ctx.stroke_path(path_);
        }
        ctx.restore();
    }

}

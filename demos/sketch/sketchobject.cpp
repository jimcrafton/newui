#include "sketchobject.h"

#include <stdexcept>

namespace sketch {

    SketchPaint SketchPaint::solid(const SketchColor& color) {
        SketchPaint paint;
        paint.kind_ = Kind::Solid;
        paint.color_ = color;
        return paint;
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
        if (isClosed() && !fill_.isNone()) {
            ctx.set_fill_style(fill_.color().toRgba32());
            ctx.set_global_alpha(opacity_);
            ctx.fill_path(path_);
        }
        if (!stroke_.isNone() && strokeWidth_ > 0.0) {
            ctx.set_stroke_style(stroke_.color().toRgba32());
            ctx.set_stroke_width(strokeWidth_);
            ctx.set_global_alpha(opacity_);
            ctx.stroke_path(path_);
        }
        ctx.restore();
    }

}

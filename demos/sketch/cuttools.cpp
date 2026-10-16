#include "canvasview.h"

#include <algorithm>
#include <cmath>

// The Scissors, Knife and Eraser tools: the canvas only gathers the pointer's path and shows it; what the cut does to the artwork is the
// document's job (SketchDocument::scissorsCut / knifeCut / erase), as one undo step each.

namespace sketch {

    using namespace newui;

    void CanvasView::setEraserSize(double size) {
        eraserSize_ = std::max(2.0, std::min(200.0, size));
        style().markDirty();
    }

    // The pointer moved with the Knife or Eraser down: another point of its path (not every pixel).
    void CanvasView::cutMove(const BLPoint& point) {
        double gap = 1.5 / double(zoom_);
        if (cutPoints_.empty() || std::hypot(point.x - cutPoints_.back().x, point.y - cutPoints_.back().y) >= gap) {
            cutPoints_.push_back(point);
        }
        dragMoved_ = true;
        style().markDirty();
    }

    // The Knife was let go: the stroke, straightened a little, divides the shapes it crossed.
    void CanvasView::finishKnife() {
        std::vector<BLPoint> stroke = simplifyPolyline(cutPoints_, 1.5 / double(zoom_));
        cutPoints_.clear();
        double length = 0.0;
        for (std::size_t i = 1; i < stroke.size(); ++i) {
            length += std::hypot(stroke[i].x - stroke[i - 1].x, stroke[i].y - stroke[i - 1].y);
        }
        if (stroke.size() < 2 || length * double(zoom_) < 4.0) {
            return;   // a click, not a cut
        }
        document_->knifeCut(stroke);
    }

    // The Eraser was let go: what the pen passed over is gone (a click rubs out a round spot).
    void CanvasView::finishErase() {
        std::vector<BLPoint> stroke = simplifyPolyline(cutPoints_, 0.5 / double(zoom_));
        cutPoints_.clear();
        if (!stroke.empty()) {
            document_->erase(stroke, eraserSize_);
        }
    }

    // While the Knife is down, a thin red line along the cut; while the Eraser is the tool, its pen (a ring at the pointer) and, as it
    // is dragged, the swath it will take.
    void CanvasView::paintCutOverlay(BLContext& ctx) const {
        ctx.save();
        if (drag_ == Drag::Knife && cutPoints_.size() >= 2) {
            BLPath path;
            BLPoint first = toView(cutPoints_[0]);
            path.move_to(first.x, first.y);
            for (std::size_t i = 1; i < cutPoints_.size(); ++i) {
                BLPoint p = toView(cutPoints_[i]);
                path.line_to(p.x, p.y);
            }
            ctx.set_stroke_width(1.5);
            ctx.set_stroke_style(BLRgba32(0xFFD02020u));
            ctx.stroke_path(path);
        }
        if (tool_ == SketchTool::Eraser) {
            const double radius = eraserSize_ * double(zoom_) * 0.5;
            if (drag_ == Drag::Erase && !cutPoints_.empty()) {
                BLPath path;
                BLPoint first = toView(cutPoints_[0]);
                path.move_to(first.x, first.y);
                for (std::size_t i = 1; i < cutPoints_.size(); ++i) {
                    BLPoint p = toView(cutPoints_[i]);
                    path.line_to(p.x, p.y);
                }
                ctx.set_stroke_caps(BL_STROKE_CAP_ROUND);
                ctx.set_stroke_join(BL_STROKE_JOIN_ROUND);
                ctx.set_stroke_width(radius * 2.0);
                ctx.set_stroke_style(BLRgba32(0x40D02020u));
                if (cutPoints_.size() == 1) {
                    ctx.set_fill_style(BLRgba32(0x40D02020u));
                    ctx.fill_circle(BLCircle(first.x, first.y, radius));
                } else {
                    ctx.stroke_path(path);
                }
            }
            // The pen itself: a ring (white inside dark, so it shows on any color).
            ctx.set_stroke_width(1.0);
            ctx.set_stroke_style(BLRgba32(0xFFFFFFFFu));
            ctx.stroke_circle(BLCircle(double(lastPointer_.x), double(lastPointer_.y), radius + 1.0));
            ctx.set_stroke_style(BLRgba32(0xFF000000u));
            ctx.stroke_circle(BLCircle(double(lastPointer_.x), double(lastPointer_.y), radius));
        }
        ctx.restore();
    }

}

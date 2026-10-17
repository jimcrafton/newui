#include "sketchdocument.h"

#include <cmath>

// Numeric transforms of the selection (the Transform panel) and Transform Again.

namespace sketch {

    namespace {
        constexpr double kMinSide = 1e-6;
    }

    // Remembering what the last move / resize / rotate did, so Transform Again can do it once more to whatever is selected.
    void SketchDocument::noteTransform(const BLMatrix2D& matrix, double angleDelta) {
        lastTransform_ = matrix;
        lastTransformAngle_ = angleDelta;
        hasLastTransform_ = true;
    }

    bool SketchDocument::transformAgain() {
        if (!hasLastTransform_) {
            return false;
        }
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty()) {
            return false;
        }
        FrameState frameBefore = frameState();
        for (const GeometryState& entry : before) {
            if (SketchObject* object = findById(entry.id)) {
                object->transform(lastTransform_);
                object->setFrameAngle(entry.angle + lastTransformAngle_);
            }
        }
        quadFrame_.reset();
        groupAngle_.reset();
        commitGeometry(before, "Transform Again", frameBefore);
        return true;
    }

    BLMatrix2D SketchDocument::matrixForBox(const BLBox& from, const BLBox& to) {
        double oldW = from.x1 - from.x0, oldH = from.y1 - from.y0;
        double sx = oldW > kMinSide ? std::max(to.x1 - to.x0, kMinSide) / oldW : 1.0;
        double sy = oldH > kMinSide ? std::max(to.y1 - to.y0, kMinSide) / oldH : 1.0;
        BLMatrix2D m = BLMatrix2D::make_translation(-from.x0, -from.y0);
        m.post_scale(sx, sy);
        m.post_translate(to.x0, to.y0);
        return m;
    }

    void SketchDocument::previewTransform(const std::vector<GeometryState>& before, const BLMatrix2D& matrix, double angleDelta) {
        for (const GeometryState& entry : before) {
            if (SketchObject* object = findById(entry.id)) {
                object->setGeometry(entry.path);
                object->setTextMatrix(entry.textMatrix);
                object->transform(matrix);
                object->setFrameAngle(entry.angle + angleDelta);
            }
        }
        quadFrame_.reset();
        groupAngle_.reset();
        touch();
    }

    bool SketchDocument::setSelectionBox(const BLBox& box) {
        BLBox now;
        if (!selectionBounds(now)) {
            return false;
        }
        double oldW = now.x1 - now.x0, oldH = now.y1 - now.y0;
        double newW = box.x1 - box.x0, newH = box.y1 - box.y0;
        if (newW < 0.0 || newH < 0.0 || !std::isfinite(newW) || !std::isfinite(newH) || !std::isfinite(box.x0) || !std::isfinite(box.y0)) {
            return false;
        }
        // A side that has no length (a straight line, say) cannot be scaled: it only moves.
        double sx = oldW > kMinSide ? std::max(newW, kMinSide) / oldW : 1.0;
        double sy = oldH > kMinSide ? std::max(newH, kMinSide) / oldH : 1.0;
        bool resized = std::fabs(sx - 1.0) > 1e-9 || std::fabs(sy - 1.0) > 1e-9;
        BLMatrix2D m = BLMatrix2D::make_translation(-now.x0, -now.y0);
        m.post_scale(sx, sy);
        m.post_translate(box.x0, box.y0);
        if (!resized && std::fabs(box.x0 - now.x0) < 1e-9 && std::fabs(box.y0 - now.y0) < 1e-9) {
            return false;   // nothing changes
        }
        transformObjects(selection_, m, resized ? "Resize" : "Move");
        return true;
    }

    bool SketchDocument::rotateSelectionBy(double radians) {
        BLBox box;
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty() || !selectionBounds(box) || !std::isfinite(radians) || std::fabs(radians) < 1e-12) {
            return false;
        }
        FrameState frameBefore = frameState();
        double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
        BLMatrix2D m = BLMatrix2D::make_translation(-cx, -cy);
        m.post_rotate(radians);
        m.post_translate(cx, cy);
        for (const GeometryState& entry : before) {
            if (SketchObject* object = findById(entry.id)) {
                object->transform(m);
                object->setFrameAngle(entry.angle + radians);
            }
        }
        quadFrame_.reset();
        groupAngle_.reset();
        noteTransform(m, radians);
        commitGeometry(before, "Rotate", frameBefore);
        return true;
    }

}

#include "canvasview.h"

#include <newui/newui.h>
#include <newui/color.h>
#include <newui/mouse_constants.h>
#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <newui/keyboard_constants.h>
#include <newui/rootview.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace sketch {

    using namespace newui;

    CanvasView::CanvasView() {
        setName("CanvasView");
        setVisible(true);
        setAcceptsFocus(true);
        onMouseDown.add(this, &CanvasView::handleMouseDown);
        onMouseMove.add(this, &CanvasView::handleMouseMove);
        onMouseUp.add(this, &CanvasView::handleMouseUp);
        onMouseWheel.add(this, &CanvasView::handleMouseWheel);
        onKeyDown.add(this, &CanvasView::handleKeyDown);
    }

    void CanvasView::setDocument(SketchDocument* document) {
        if (document_ != nullptr) {
            document_->removeView(this);
        }
        document_ = document;
        if (document_ != nullptr) {
            document_->addView(this);
            pageWidth_ = document_->pageWidth();
            pageHeight_ = document_->pageHeight();
        }
        style().markDirty();
    }

    void CanvasView::setPan(float x, float y) {
        centered_ = false;
        panX_ = x;
        panY_ = y;
        style().markDirty();
    }

    BLPoint CanvasView::toDocument(const Point& viewPoint) const {
        Rect client = getClientBounds();
        double rs = rulerSize();
        return BLPoint((double(viewPoint.x) - client.left() - rs - double(panX_)) / double(zoom_),
            (double(viewPoint.y) - client.top() - rs - double(panY_)) / double(zoom_));
    }

    BLPoint CanvasView::toView(const BLPoint& documentPoint) const {
        Rect client = getClientBounds();
        double rs = rulerSize();
        return BLPoint(client.left() + rs + double(panX_) + documentPoint.x * double(zoom_),
            client.top() + rs + double(panY_) + documentPoint.y * double(zoom_));
    }

    void CanvasView::zoomAt(const Point& anchor, float newZoom) {
        float clamped = newZoom < kMinZoom ? kMinZoom : (newZoom > kMaxZoom ? kMaxZoom : newZoom);
        BLPoint before = toDocument(anchor);
        zoom_ = clamped;
        BLPoint after = toView(before);
        centered_ = false;
        panX_ += float(double(anchor.x) - after.x);
        panY_ += float(double(anchor.y) - after.y);
        style().markDirty();
        onZoomChanged(*this);
    }

    void CanvasView::setTool(SketchTool tool) {
        if (tool == tool_) {
            return;
        }
        tool_ = tool;
        drag_ = Drag::None;
        style().markDirty();
        onToolChanged(*this);
    }

    // ---- Selection frame and handles (view space) ----

    BLPoint CanvasView::handlePosition(int handle, const SelectionFrame& frame) const {
        if (handle == kOriginHandle) {
            return toView(document_->selectionOrigin(frame));
        }
        static const double kFraction[8][2] = {{0, 0}, {0.5, 0}, {1, 0}, {1, 0.5}, {1, 1}, {0.5, 1}, {0, 1}, {0, 0.5}};
        QuadFrame quad = frame.asQuad();
        auto at = [&](double u, double v) { return toView(quad.map(u, v)); };
        if (handle != kRotateHandle) {
            return at(kFraction[handle][0], kFraction[handle][1]);
        }
        // The knob sits a fixed pixel distance outward from the top edge's midpoint, along that edge's normal.
        BLPoint topCenter = at(0.5, 0.0), topLeft = at(0.0, 0.0), topRight = at(1.0, 0.0);
        double dx = topRight.x - topLeft.x, dy = topRight.y - topLeft.y;
        double len = std::hypot(dx, dy);
        double nx = len > 1e-9 ? dy / len : 0.0, ny = len > 1e-9 ? -dx / len : -1.0;
        return BLPoint(topCenter.x + nx * kRotateKnobOffset, topCenter.y + ny * kRotateKnobOffset);
    }

    int CanvasView::hitHandle(const Point& viewPoint) const {
        SelectionFrame frame;
        if (document_ == nullptr || !document_->selectionFrame(frame)) {
            return -1;
        }
        // Rotate knob first so it wins over a coincident edge handle on a very short frame.
        for (int handle : {kOriginHandle, kRotateHandle, 0, 1, 2, 3, 4, 5, 6, 7}) {
            BLPoint pos = handlePosition(handle, frame);
            if (std::fabs(double(viewPoint.x) - pos.x) <= kHandleHit && std::fabs(double(viewPoint.y) - pos.y) <= kHandleHit) {
                return handle;
            }
        }
        return -1;
    }

    // Left button belongs to the tools; middle/right (and the hand tool) pan.
    SyncReturn CanvasView::handleMouseDown(View& /*sender*/, const Point& pt, std::uint32_t buttons, std::uint32_t keys) {
        if (RootView* root = rootView()) {
            root->setFocusedSubView(this);
        }
        bool wantsPan = (buttons & (mbmMiddleButton | mbmRightButton)) != 0 ||
            ((buttons & mbmLeftButton) != 0 && tool_ == SketchTool::Hand);
        if (wantsPan) {
            panning_ = true;
            panAnchor_ = pt;
            panStartX_ = panX_;
            panStartY_ = panY_;
            return SyncReturn::Handled;
        }
        if ((buttons & mbmLeftButton) == 0 || document_ == nullptr) {
            return SyncReturn::Ignored;
        }

        dragStartDoc_ = toDocument(pt);
        dragNowDoc_ = dragStartDoc_;
        dragNowView_ = pt;
        dragMoved_ = false;
        dragAdditive_ = (keys & kmShift) != 0;

        if (tool_ == SketchTool::Zoom) {  // click zooms in about the cursor, Shift-click out
            zoomAt(pt, zoom_ * ((keys & kmShift) != 0 ? 1.0f / 1.5f : 1.5f));
            return SyncReturn::Handled;
        }
        if (tool_ == SketchTool::Rectangle || tool_ == SketchTool::Ellipse || tool_ == SketchTool::Line) {
            drag_ = Drag::Draw;
            return SyncReturn::Handled;
        }
        if (tool_ != SketchTool::Select) {
            return SyncReturn::Ignored;  // palette entries not implemented yet
        }

        int handle = hitHandle(pt);
        if (handle >= 0) {
            beginResize(handle, keys);
            return SyncReturn::Handled;
        }

        double tolerance = 4.0 / double(zoom_);
        SketchObject* hit = document_->hitTest(dragStartDoc_, tolerance);
        if (hit != nullptr) {
            std::vector<std::size_t> selection = document_->selection();
            bool already = document_->isSelected(hit->id());
            if (dragAdditive_) {
                if (already) {
                    selection.erase(std::remove(selection.begin(), selection.end(), hit->id()), selection.end());
                } else {
                    selection.push_back(hit->id());
                }
                document_->setSelection(selection);
            } else if (!already) {
                document_->setSelection({hit->id()});
            }
            if (document_->isSelected(hit->id())) {
                drag_ = Drag::Move;
                dragBefore_ = document_->snapshotSelection();
                dragFrameBefore_ = document_->frameState();
                document_->selectionFrame(dragFrame_);
            }
            return SyncReturn::Handled;
        }

        if (!dragAdditive_) {
            document_->clearSelection();
        }
        drag_ = Drag::Marquee;
        return SyncReturn::Handled;
    }

    void CanvasView::beginResize(int handle, std::uint32_t keys) {
        dragHandle_ = handle;
        dragBefore_ = document_->snapshotSelection();
        dragFrameBefore_ = document_->frameState();
        document_->selectionFrame(dragFrame_);
        dragOrigin_ = document_->selectionOrigin(dragFrame_);
        bool altShift = (keys & kmAlt) != 0 && (keys & kmShift) != 0;
        if (handle == kOriginHandle) {
            drag_ = Drag::Origin;
        } else if (handle == kRotateHandle) {
            drag_ = Drag::Rotate;
            rotateStartAngle_ = std::atan2(dragStartDoc_.y - dragOrigin_.y, dragStartDoc_.x - dragOrigin_.x);
        } else if (dragFrame_.quad.has_value() || (altShift && handle % 2 == 0)) {
            drag_ = Drag::Warp;
            QuadFrame start = dragFrame_.asQuad();
            for (int i = 0; i < 4; ++i) {
                warpQuad_[i] = start.p[i];
            }
        } else if (altShift) {
            drag_ = Drag::Shear;
        } else {
            drag_ = Drag::Resize;
        }
    }

    void CanvasView::applyMapToSelection(const std::function<BLPoint(const BLPoint&)>& map, const QuadFrame& newQuad) {
        for (const GeometryState& entry : dragBefore_) {
            if (SketchObject* object = document_->findById(entry.id)) {
                object->setGeometry(entry.path);
                object->warp(map);
                object->setFrameAngle(entry.angle);
            }
        }
        document_->setQuadFrame(newQuad);
        document_->touch();
    }

    void CanvasView::applyToSelection(const BLMatrix2D& matrix, double angleDelta) {
        for (const GeometryState& entry : dragBefore_) {
            if (SketchObject* object = document_->findById(entry.id)) {
                object->setGeometry(entry.path);
                object->transform(matrix);
                object->setFrameAngle(entry.angle + angleDelta);
            }
        }
        if (document_->selection().size() > 1) {
            // A rotated group keeps its own frame angle even when its members differ.
            document_->setGroupAngle(angleDelta == 0.0 ? dragFrameBefore_.groupAngle : std::optional<double>(dragFrame_.angle + angleDelta));
        }
        // A warped frame rides along with whatever is done to the objects.
        if (dragFrameBefore_.quad.has_value()) {
            document_->setQuadFrame(dragFrameBefore_.quad->transformed(matrix));
        }
        document_->touch();
    }

    void CanvasView::updateDrag(const Point& pt, std::uint32_t keys) {
        dragNowView_ = pt;
        dragNowDoc_ = toDocument(pt);
        bool shift = (keys & kmShift) != 0;
        const double kPi = 3.14159265358979323846;

        switch (drag_) {
            case Drag::Move: {
                double dx = dragNowDoc_.x - dragStartDoc_.x;
                double dy = dragNowDoc_.y - dragStartDoc_.y;
                if (!dragMoved_ && std::fabs(dx * double(zoom_)) < 3.0 && std::fabs(dy * double(zoom_)) < 3.0) {
                    return;  // a click, not yet a drag
                }
                dragMoved_ = true;
                if (shift) {  // constrain to the dominant axis
                    if (std::fabs(dx) > std::fabs(dy)) { dy = 0.0; } else { dx = 0.0; }
                }
                applyToSelection(BLMatrix2D::make_translation(dx, dy));
                break;
            }
            case Drag::Resize: {
                // Everything below happens in the frame's own (rotated) coordinates, then the scale is
                // conjugated back into document space: M = R(a) * Scale * R(-a).
                const BLBox& box = dragFrame_.local;
                BLPoint mouse = dragFrame_.toLocal(dragNowDoc_);
                const int h = dragHandle_;
                bool movesLeft = (h == 0 || h == 6 || h == 7);
                bool movesRight = (h == 2 || h == 3 || h == 4);
                bool movesTop = (h == 0 || h == 1 || h == 2);
                bool movesBottom = (h == 4 || h == 5 || h == 6);
                double ax = movesLeft ? box.x1 : box.x0;
                double ay = movesTop ? box.y1 : box.y0;
                double grabX = movesLeft ? box.x0 : box.x1;
                double grabY = movesTop ? box.y0 : box.y1;
                double sx = 1.0, sy = 1.0;
                if ((movesLeft || movesRight) && std::fabs(grabX - ax) > 1e-6) {
                    sx = (mouse.x - ax) / (grabX - ax);
                }
                if ((movesTop || movesBottom) && std::fabs(grabY - ay) > 1e-6) {
                    sy = (mouse.y - ay) / (grabY - ay);
                }
                if (shift && (movesLeft || movesRight) && (movesTop || movesBottom)) {
                    double s = std::max(std::fabs(sx), std::fabs(sy));
                    sx = sx < 0 ? -s : s;
                    sy = sy < 0 ? -s : s;
                }
                if (std::fabs(sx) < 0.01) { sx = sx < 0 ? -0.01 : 0.01; }
                if (std::fabs(sy) < 0.01) { sy = sy < 0 ? -0.01 : 0.01; }
                BLMatrix2D m = BLMatrix2D::make_rotation(-dragFrame_.angle);
                m.post_translate(-ax, -ay);
                m.post_scale(sx, sy);
                m.post_translate(ax, ay);
                m.post_rotate(dragFrame_.angle);
                dragMoved_ = true;
                applyToSelection(m);
                break;
            }
            case Drag::Rotate: {
                BLPoint c = dragOrigin_;
                double angle = std::atan2(dragNowDoc_.y - c.y, dragNowDoc_.x - c.x) - rotateStartAngle_;
                if (shift) {
                    double step = kPi / 12.0;  // 15 degrees
                    angle = std::round(angle / step) * step;
                }
                dragMoved_ = true;
                // make_rotation(angle, x, y) does not pivot about (x, y) in this Blend2D - build it.
                BLMatrix2D m = BLMatrix2D::make_translation(-c.x, -c.y);
                m.post_rotate(angle);
                m.post_translate(c.x, c.y);
                applyToSelection(m, angle);
                break;
            }
            case Drag::Shear: {
                // Shear in the frame's own axes about the transform origin, the dragged handle following
                // the mouse: a top/bottom handle slides horizontally, a left/right handle vertically.
                const BLBox& box = dragFrame_.local;
                BLPoint mouse = dragFrame_.toLocal(dragNowDoc_);
                BLPoint origin = dragFrame_.toLocal(dragOrigin_);
                const int h = dragHandle_;
                double cx = (box.x0 + box.x1) * 0.5, cy = (box.y0 + box.y1) * 0.5;
                BLMatrix2D shear = BLMatrix2D::make_identity();
                if (h == 1 || h == 5) {
                    double handleY = h == 1 ? box.y0 : box.y1;
                    double lever = handleY - origin.y;
                    if (std::fabs(lever) > 1e-6) {
                        shear = BLMatrix2D(1.0, 0.0, (mouse.x - cx) / lever, 1.0, 0.0, 0.0);
                    }
                } else {
                    double handleX = h == 7 ? box.x0 : box.x1;
                    double lever = handleX - origin.x;
                    if (std::fabs(lever) > 1e-6) {
                        shear = BLMatrix2D(1.0, (mouse.y - cy) / lever, 0.0, 1.0, 0.0, 0.0);
                    }
                }
                BLMatrix2D m = BLMatrix2D::make_rotation(-dragFrame_.angle);
                m.post_translate(-origin.x, -origin.y);
                m.post_transform(shear);
                m.post_translate(origin.x, origin.y);
                m.post_rotate(dragFrame_.angle);
                dragMoved_ = true;
                applyToSelection(m);
                break;
            }
            case Drag::Warp: {
                // Edit the frame quad - a corner follows the mouse, an edge slides along its normal - and carry
                // every point by the projective map taking the old quad onto the new one (straight lines stay straight).
                QuadFrame source = dragFrame_.asQuad();
                QuadFrame dest = source;
                const int h = dragHandle_;
                if (h % 2 == 0) {
                    dest.p[h / 2] = dragNowDoc_;
                } else {
                    int i0 = h / 2, i1 = (i0 + 1) % 4;
                    double ex = source.p[i1].x - source.p[i0].x, ey = source.p[i1].y - source.p[i0].y;
                    double len = std::hypot(ex, ey);
                    if (len < 1e-9) {
                        break;
                    }
                    double nx = ey / len, ny = -ex / len;
                    double k = (dragNowDoc_.x - dragStartDoc_.x) * nx + (dragNowDoc_.y - dragStartDoc_.y) * ny;
                    dest.p[i0] = BLPoint(dest.p[i0].x + nx * k, dest.p[i0].y + ny * k);
                    dest.p[i1] = BLPoint(dest.p[i1].x + nx * k, dest.p[i1].y + ny * k);
                }
                for (int i = 0; i < 4; ++i) {
                    warpQuad_[i] = dest.p[i];
                }
                double su = 0.0, sv = 0.0;
                if (!source.unmap(source.p[0], su, sv)) {
                    break;  // degenerate frame
                }
                auto map = [source, dest](const BLPoint& p) {
                    double u = 0.0, v = 0.0;
                    if (!source.unmap(p, u, v)) {
                        return p;
                    }
                    return dest.map(u, v);
                };
                dragMoved_ = true;
                applyMapToSelection(map, dest);
                break;
            }
            case Drag::Origin:
                document_->setSelectionOrigin(dragFrame_, dragNowDoc_);
                break;
            case Drag::Marquee:
            case Drag::Draw:
                dragMoved_ = true;
                style().markDirty();
                break;
            case Drag::None:
                break;
        }
    }

    namespace {
        BLBox normalizedBox(const BLPoint& a, const BLPoint& b) {
            return BLBox(std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y));
        }
    }

    void CanvasView::finishDrag(const Point& pt, std::uint32_t keys) {
        updateDrag(pt, keys);
        Drag finished = drag_;
        drag_ = Drag::None;
        bool shift = (keys & kmShift) != 0;

        switch (finished) {
            case Drag::Move:
            case Drag::Resize:
            case Drag::Rotate:
            case Drag::Shear:
            case Drag::Warp:
                if (dragMoved_) {
                    const char* what = "Move";
                    switch (finished) {
                        case Drag::Resize: what = "Resize"; break;
                        case Drag::Rotate: what = "Rotate"; break;
                        case Drag::Shear: what = "Shear"; break;
                        case Drag::Warp: what = "Warp"; break;
                        default: break;
                    }
                    document_->commitGeometry(dragBefore_, what, dragFrameBefore_);
                }
                break;
            case Drag::Origin:
                break;
            case Drag::Marquee: {
                BLBox box = normalizedBox(dragStartDoc_, dragNowDoc_);
                std::vector<std::size_t> selection = dragAdditive_ ? document_->selection() : std::vector<std::size_t>();
                for (std::size_t i = 0; i < document_->objectCount(); ++i) {
                    SketchObject& object = document_->object(i);
                    BLBox b = object.geometryBounds();
                    bool hits = b.x1 >= box.x0 && b.x0 <= box.x1 && b.y1 >= box.y0 && b.y0 <= box.y1;
                    if (hits && object.isVisible() && !object.isLocked() &&
                        std::find(selection.begin(), selection.end(), object.id()) == selection.end()) {
                        selection.push_back(object.id());
                    }
                }
                document_->setSelection(selection);
                style().markDirty();
                break;
            }
            case Drag::Draw: {
                BLPoint a = dragStartDoc_;
                BLPoint b = dragNowDoc_;
                double w = b.x - a.x, h = b.y - a.y;
                if (tool_ == SketchTool::Line) {
                    if (shift) {  // snap to 45 degree increments
                        double len = std::hypot(w, h);
                        double step = 3.14159265358979323846 / 4.0;
                        double angle = std::round(std::atan2(h, w) / step) * step;
                        b = BLPoint(a.x + len * std::cos(angle), a.y + len * std::sin(angle));
                    }
                    if (std::hypot(b.x - a.x, b.y - a.y) < 2.0) {
                        break;
                    }
                    SketchObject* line = SketchObject::makeLine(a.x, a.y, b.x, b.y);
                    line->setName("Line");
                    line->setStroke(document_->defaultStroke().isNone() ? SketchPaint::solid(SketchColor::gray(0.0f)) : document_->defaultStroke());
                    line->setStrokeWidth(document_->defaultStrokeWidth());
                    document_->addUndoable(line, "Draw Line");
                    break;
                }
                if (shift) {
                    double side = std::max(std::fabs(w), std::fabs(h));
                    b = BLPoint(a.x + (w < 0 ? -side : side), a.y + (h < 0 ? -side : side));
                }
                BLBox box = normalizedBox(a, b);
                double bw = box.x1 - box.x0, bh = box.y1 - box.y0;
                if (bw < 2.0 || bh < 2.0) {
                    break;
                }
                SketchObject* shape = tool_ == SketchTool::Ellipse
                    ? SketchObject::makeEllipse(box.x0 + bw * 0.5, box.y0 + bh * 0.5, bw * 0.5, bh * 0.5)
                    : SketchObject::makeRect(box.x0, box.y0, bw, bh);
                shape->setName(tool_ == SketchTool::Ellipse ? "Ellipse" : "Rectangle");
                shape->setFill(document_->defaultFill());
                shape->setStroke(document_->defaultStroke());
                shape->setStrokeWidth(document_->defaultStrokeWidth());
                document_->addUndoable(shape, tool_ == SketchTool::Ellipse ? "Draw Ellipse" : "Draw Rectangle");
                break;
            }
            case Drag::None:
                break;
        }
        dragBefore_.clear();
        style().markDirty();
    }

    SyncReturn CanvasView::handleMouseMove(View& /*sender*/, const Point& pt, std::uint32_t /*buttons*/, std::uint32_t keys) {
        if (panning_) {
            setPan(panStartX_ + (pt.x - panAnchor_.x), panStartY_ + (pt.y - panAnchor_.y));
            return SyncReturn::Handled;
        }
        if (drag_ != Drag::None) {
            updateDrag(pt, keys);
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    SyncReturn CanvasView::handleMouseUp(View& /*sender*/, const Point& pt, std::uint32_t /*buttons*/, std::uint32_t keys) {
        if (panning_) {
            panning_ = false;
            return SyncReturn::Handled;
        }
        if (drag_ != Drag::None) {
            finishDrag(pt, keys);
            return SyncReturn::Handled;
        }
        return SyncReturn::Ignored;
    }

    SyncReturn CanvasView::handleKeyDown(View& /*sender*/, std::uint32_t keyMask, int /*keyChar*/, int /*repeatCount*/, std::uint32_t vkCode) {
        if (document_ == nullptr) {
            return SyncReturn::Ignored;
        }
        bool ctrl = (keyMask & kmCtrl) != 0;
        bool shift = (keyMask & kmShift) != 0;
        if (ctrl && onControlKey && onControlKey(vkCode, shift)) {
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterZ) {
            document_->undoStack().undo();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterY) {
            document_->undoStack().redo();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterA) {
            if (shift) { document_->clearSelection(); } else { document_->selectAll(); }
            return SyncReturn::Handled;
        }
        if (ctrl && shift && vkCode == vkLetterB) {
            document_->resetBoundingBox();
            return SyncReturn::Handled;
        }
        if (ctrl && vkCode == vkLetterD) {
            document_->duplicateSelection(12.0, 12.0);
            return SyncReturn::Handled;
        }
        if (vkCode == vkDelete) {
            document_->deleteSelection();
            return SyncReturn::Handled;
        }
        if (vkCode == vkEscape) {
            if (drag_ != Drag::None) {  // cancel an in-progress drag, restoring the outlines
                if (!dragBefore_.empty()) {
                    applyToSelection(BLMatrix2D::make_identity());
                }
                drag_ = Drag::None;
                dragBefore_.clear();
                style().markDirty();
            } else {
                document_->clearSelection();
            }
            return SyncReturn::Handled;
        }
        double step = shift ? 10.0 : 1.0;
        double dx = 0.0, dy = 0.0;
        switch (vkCode) {
            case vkLeftArrow: dx = -step; break;
            case vkRightArrow: dx = step; break;
            case vkUpArrow: dy = -step; break;
            case vkDownArrow: dy = step; break;
            default: return SyncReturn::Ignored;
        }
        if (!document_->selection().empty()) {
            document_->transformObjects(document_->selection(), BLMatrix2D::make_translation(dx, dy), "Nudge");
        }
        return SyncReturn::Handled;
    }

    void CanvasView::paintOverlay(BLContext& ctx) const {
        if (document_ == nullptr) {
            return;
        }
        const BLRgba32 accent(0xFF1E6FE0u);
        ctx.save();
        ctx.set_stroke_width(1.0);
        ctx.set_stroke_style(accent);

        // A closed outline through the four corners of a frame (rectangle or warped quad), in view pixels.
        auto frameOutline = [this](const SelectionFrame& f) {
            QuadFrame quad = f.asQuad();
            BLPath path;
            for (int i = 0; i < 4; ++i) {
                BLPoint p = toView(quad.p[i]);
                if (i == 0) { path.move_to(p.x, p.y); } else { path.line_to(p.x, p.y); }
            }
            path.close();
            return path;
        };

        // With several objects selected, each gets its own (oriented) outline under the group frame.
        if (document_->selection().size() > 1) {
            for (std::size_t id : document_->selection()) {
                const SketchObject* object = document_->findById(id);
                if (object == nullptr) {
                    continue;
                }
                SelectionFrame own;
                own.angle = object->frameAngle();
                own.local = object->frameBounds();
                ctx.stroke_path(frameOutline(own));
            }
        }

        // The group frame with its handles.
        SelectionFrame frame;
        if (document_->selectionFrame(frame)) {
            ctx.stroke_path(frameOutline(frame));
            if (drag_ != Drag::Marquee && drag_ != Drag::Draw) {
                BLPoint knob = handlePosition(kRotateHandle, frame);
                BLPoint topCenter = handlePosition(1, frame);
                BLPath stem;
                stem.move_to(topCenter.x, topCenter.y);
                stem.line_to(knob.x, knob.y);
                ctx.stroke_path(stem);
                for (int handle = 0; handle <= kRotateHandle; ++handle) {
                    BLPoint pos = handlePosition(handle, frame);
                    if (handle == kRotateHandle) {
                        BLPath circle;
                        circle.add_circle(BLCircle(pos.x, pos.y, 4.5));
                        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                        ctx.fill_path(circle);
                        ctx.stroke_path(circle);
                    } else {
                        BLRect box(std::floor(pos.x) - 3.0, std::floor(pos.y) - 3.0, 7.0, 7.0);
                        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
                        ctx.fill_rect(box);
                        ctx.stroke_rect(BLRect(box.x + 0.5, box.y + 0.5, box.w - 1.0, box.h - 1.0));
                    }
                }
            }
        }

        // The transform origin: a ringed dot (drag it to move the rotate/shear pivot).
        SelectionFrame originFrame;
        if (drag_ != Drag::Marquee && drag_ != Drag::Draw && document_->selectionFrame(originFrame)) {
            BLPoint o = handlePosition(kOriginHandle, originFrame);
            BLPath ring;
            ring.add_circle(BLCircle(o.x, o.y, 4.5));
            ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
            ctx.fill_path(ring);
            ctx.stroke_path(ring);
            BLPath cross;
            cross.move_to(o.x - 7.0, o.y);
            cross.line_to(o.x + 7.0, o.y);
            cross.move_to(o.x, o.y - 7.0);
            cross.line_to(o.x, o.y + 7.0);
            ctx.stroke_path(cross);
            ctx.set_fill_style(accent);
            ctx.fill_circle(BLCircle(o.x, o.y, 1.6));
        }

        // Rubber band for marquee / shape being drawn.
        if (drag_ == Drag::Marquee || drag_ == Drag::Draw) {
            BLPoint a = toView(dragStartDoc_);
            BLPoint b(double(dragNowView_.x), double(dragNowView_.y));
            if (drag_ == Drag::Draw && tool_ == SketchTool::Line) {
                BLPath line;
                line.move_to(a.x, a.y);
                line.line_to(b.x, b.y);
                ctx.stroke_path(line);
            } else {
                BLRect band(std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
                if (drag_ == Drag::Marquee) {
                    ctx.set_fill_style(BLRgba32(0x261E6FE0u));
                    ctx.fill_rect(band);
                }
                if (drag_ == Drag::Draw && tool_ == SketchTool::Ellipse) {
                    BLPath oval;
                    oval.add_ellipse(BLEllipse(band.x + band.w * 0.5, band.y + band.h * 0.5, band.w * 0.5, band.h * 0.5));
                    ctx.stroke_path(oval);
                } else {
                    ctx.stroke_rect(band);
                }
            }
        }
        ctx.restore();
    }

    // delta follows WM_MOUSEWHEEL (120 per notch, positive = away from the user).
    SyncReturn CanvasView::handleMouseWheel(View& /*sender*/, const Point& pt, float delta) {
        float notches = delta / 120.0f;
        if ((::GetKeyState(VK_CONTROL) & 0x8000) != 0) {
            zoomAt(pt, zoom_ * std::pow(1.15f, notches));
        } else if ((::GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            setPan(panX_ + notches * 40.0f, panY_);
        } else {
            setPan(panX_, panY_ + notches * 40.0f);
        }
        return SyncReturn::Handled;
    }

    void CanvasView::setPageWidth(float width) {
        if (width <= 0.0f) {
            throw std::runtime_error("CanvasView::setPageWidth: width must be positive");
        }
        pageWidth_ = width;
        style().markDirty();
    }

    void CanvasView::setPageHeight(float height) {
        if (height <= 0.0f) {
            throw std::runtime_error("CanvasView::setPageHeight: height must be positive");
        }
        pageHeight_ = height;
        style().markDirty();
    }

    void CanvasView::setZoom(float zoom) {
        if (zoom <= 0.0f) {
            throw std::runtime_error("CanvasView::setZoom: zoom must be positive");
        }
        zoom_ = zoom;
        style().markDirty();
        onZoomChanged(*this);
    }

    void CanvasView::zoomToFit() {
        Rect client = getClientBounds();
        float availW = client.width() - rulerSize() - 40.0f;
        float availH = client.height() - rulerSize() - 40.0f;
        if (availW <= 0.0f || availH <= 0.0f) {
            return;
        }
        float fitW = availW / pageWidth_;
        float fitH = availH / pageHeight_;
        float fit = fitW < fitH ? fitW : fitH;
        centered_ = true;
        setZoom(fit < kMinZoom ? kMinZoom : (fit > kMaxZoom ? kMaxZoom : fit));
    }

    void CanvasView::setShowGrid(bool value) {
        showGrid_ = value;
        style().markDirty();
    }

    void CanvasView::setShowRulers(bool value) {
        showRulers_ = value;
        style().markDirty();
    }

    void CanvasView::setGridSpacing(float spacing) {
        if (spacing <= 0.0f) {
            throw std::runtime_error("CanvasView::setGridSpacing: spacing must be positive");
        }
        gridSpacing_ = spacing;
        style().markDirty();
    }

    void CanvasView::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }

        float rulerSize = this->rulerSize();

        ctx.save();
        ctx.clip_to_rect(BLRect(client.left() + rulerSize, client.top() + rulerSize,
            client.width() - rulerSize, client.height() - rulerSize));

        // Pasteboard.
        ctx.set_fill_style(BLRgba32(0xFF7A7A7Au));
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), client.height()));

        // Until the user pans or wheel-zooms, the page stays centered in the drawing area
        // (vertically only when it fits, else a fixed top margin).
        if (centered_) {
            float availW = client.width() - rulerSize;
            float availH = client.height() - rulerSize;
            float w = pageWidth_ * zoom_;
            float h = pageHeight_ * zoom_;
            panX_ = (availW - w) * 0.5f;
            panY_ = h < availH - 40.0f ? (availH - h) * 0.5f : 20.0f;
        }

        // Page, with its top-left at the pan offset from the drawing area's corner.
        double pageLeft = client.left() + rulerSize + double(panX_);
        double pageTop = client.top() + rulerSize + double(panY_);
        double pageW = double(pageWidth_) * double(zoom_);
        double pageH = double(pageHeight_) * double(zoom_);
        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
        ctx.fill_rect(BLRect(pageLeft, pageTop, pageW, pageH));

        if (showGrid_) {
            double step = double(gridSpacing_) * double(zoom_);
            // Below ~4px a grid is just noise.
            if (step >= 4.0) {
                BLPath grid;
                for (double x = step; x < pageW; x += step) {
                    double px = std::floor(pageLeft + x) + 0.5;
                    grid.move_to(px, pageTop);
                    grid.line_to(px, pageTop + pageH);
                }
                for (double y = step; y < pageH; y += step) {
                    double py = std::floor(pageTop + y) + 0.5;
                    grid.move_to(pageLeft, py);
                    grid.line_to(pageLeft + pageW, py);
                }
                ctx.set_stroke_style(BLRgba32(0xFFE7E7E7u));
                ctx.set_stroke_width(1.0);
                ctx.stroke_path(grid);
            }
        }

        // Objects, clipped to the page-and-beyond drawing area; document units are points.
        if (document_ != nullptr) {
            ctx.save();
            ctx.translate(pageLeft, pageTop);
            ctx.scale(double(zoom_));
            document_->paint(ctx);
            ctx.restore();
            paintOverlay(ctx);
        }

        ctx.set_stroke_style(BLRgba32(0x59000000u));
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(pageLeft - 0.5, pageTop - 0.5, pageW + 1.0, pageH + 1.0));
        ctx.restore();

        if (showRulers_) {
            paintRulers(ctx, client);
        }
    }

    // Tick labels every 100 document units at the current zoom - enough for the scaffold; the
    // real ruler (adaptive tick spacing, cursor marker, guide dragging) comes with S7.
    void CanvasView::paintRulers(BLContext& ctx, const Rect& client) const {
        BLRgba32 face = UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32();
        BLRgba32 text = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
        BLRgba32 edge = UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32();

        ctx.save();
        ctx.set_fill_style(face);
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), kRulerThickness));
        ctx.fill_rect(BLRect(client.left(), client.top(), kRulerThickness, client.height()));
        ctx.set_stroke_style(edge);
        ctx.set_stroke_width(1.0);
        BLPath edges;
        edges.move_to(client.left(), client.top() + kRulerThickness - 0.5);
        edges.line_to(client.left() + client.width(), client.top() + kRulerThickness - 0.5);
        edges.move_to(client.left() + kRulerThickness - 0.5, client.top());
        edges.line_to(client.left() + kRulerThickness - 0.5, client.top() + client.height());
        ctx.stroke_path(edges);

        Font font = FontManager::getSystemFont(SystemUIFont::Status);
        font.setSize(9.0f);  // rulers are chrome, not content - keep the numerals small
        BLFont* blFont = font.blFont();
        double originX = client.left() + kRulerThickness + double(panX_);
        double originY = client.top() + kRulerThickness + double(panY_);
        double step = 100.0 * double(zoom_);
        if (blFont != nullptr && blFont->is_valid() && step >= 20.0) {
            ctx.set_fill_style(text);
            BLPath ticks;
            int firstX = int(std::ceil((client.left() + kRulerThickness - originX) / step));
            for (int i = firstX; ; ++i) {
                double x = originX + double(i) * step;
                if (x > client.left() + client.width()) {
                    break;
                }
                std::string label = std::to_string(i * 100);
                ticks.move_to(std::floor(x) + 0.5, client.top() + 12.0);
                ticks.line_to(std::floor(x) + 0.5, client.top() + kRulerThickness);
                ctx.fill_utf8_text(BLPoint(x + 3.0, client.top() + 11.0), *blFont, label.c_str(), label.size());
            }
            int firstY = int(std::ceil((client.top() + kRulerThickness - originY) / step));
            for (int i = firstY; ; ++i) {
                double y = originY + double(i) * step;
                if (y > client.top() + client.height()) {
                    break;
                }
                std::string label = std::to_string(i * 100);
                ticks.move_to(client.left() + 12.0, std::floor(y) + 0.5);
                ticks.line_to(client.left() + kRulerThickness, std::floor(y) + 0.5);
                ctx.fill_utf8_text(BLPoint(client.left() + 2.0, y + 11.0), *blFont, label.c_str(), label.size());
            }
            ctx.set_stroke_style(edge);
            ctx.stroke_path(ticks);
        }
        ctx.restore();
    }

}

#include "sketchdocument.h"
#include "sketchsvg.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace sketch {

    void SketchDocument::setPageSize(float width, float height) {
        if (width <= 0.0f || height <= 0.0f) {
            throw std::runtime_error("SketchDocument::setPageSize: width and height must be positive");
        }
        pageWidth_ = width;
        pageHeight_ = height;
        changed();
    }

    void SketchDocument::setPpi(float ppi) {
        if (ppi <= 0.0f) {
            throw std::runtime_error("SketchDocument::setPpi: ppi must be positive");
        }
        ppi_ = ppi;
        changed();
    }

    SketchObject& SketchDocument::object(std::size_t index) const {
        if (index >= objects_.size()) {
            throw std::runtime_error("SketchDocument::object: index out of range");
        }
        return *objects_[index];
    }

    SketchObject& SketchDocument::add(SketchObject* object) {
        if (object == nullptr) {
            throw std::runtime_error("SketchDocument::add: object is null");
        }
        std::unique_ptr<SketchObject> owned(object);
        owned->setId(nextId_++);
        objects_.push_back(std::move(owned));
        changed();
        return *objects_.back();
    }

    bool SketchDocument::remove(std::size_t id) {
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            if (objects_[i]->id() == id) {
                objects_.erase(objects_.begin() + std::ptrdiff_t(i));
                pruneSelection();
                changed();
                return true;
            }
        }
        return false;
    }

    SketchObject* SketchDocument::findById(std::size_t id) const {
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->id() == id) {
                return object.get();
            }
        }
        return nullptr;
    }

    SketchObject* SketchDocument::hitTest(const BLPoint& point, double tolerance) const {
        for (std::size_t i = objects_.size(); i > 0; --i) {
            SketchObject* object = objects_[i - 1].get();
            if (object->isVisible() && !object->isLocked() && object->hitTest(point, tolerance)) {
                return object;
            }
        }
        return nullptr;
    }

    void SketchDocument::clearObjects() {
        objects_.clear();
        nextId_ = 1;
        selection_.clear();
        undoStack_.clear();
        updateAllViews();
    }

    void SketchDocument::paint(BLContext& ctx) const {
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            object->paint(ctx);
        }
    }

    // ---- Selection ----

    bool SketchDocument::isSelected(std::size_t id) const {
        return std::find(selection_.begin(), selection_.end(), id) != selection_.end();
    }

    void SketchDocument::setSelection(const std::vector<std::size_t>& ids) {
        std::vector<std::size_t> valid;
        for (std::size_t id : ids) {
            if (findById(id) != nullptr && std::find(valid.begin(), valid.end(), id) == valid.end()) {
                valid.push_back(id);
            }
        }
        if (valid == selection_) {
            return;
        }
        selection_ = valid;
        groupAngle_.reset();
        quadFrame_.reset();
        originU_ = originV_ = 0.5;
        onSelectionChanged(*this);
        updateAllViews();
    }

    void SketchDocument::selectAll() {
        std::vector<std::size_t> ids;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (object->isVisible() && !object->isLocked()) {
                ids.push_back(object->id());
            }
        }
        setSelection(ids);
    }

    bool SketchDocument::selectionBounds(BLBox& out) const {
        bool any = false;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            BLBox box = object->geometryBounds();
            if (!any) {
                out = box;
                any = true;
            } else {
                out.x0 = std::min(out.x0, box.x0);
                out.y0 = std::min(out.y0, box.y0);
                out.x1 = std::max(out.x1, box.x1);
                out.y1 = std::max(out.y1, box.y1);
            }
        }
        return any;
    }

    namespace {
        // Heckbert's unit-square -> quad coefficients: X = (a u + b v + c) / (g u + h v + 1), Y likewise with d, e, f.
        struct Projective {
            double a, b, c, d, e, f, g, h;
        };

        Projective projectiveFor(const BLPoint* q) {
            double dx1 = q[1].x - q[2].x, dx2 = q[3].x - q[2].x, dx3 = q[0].x - q[1].x + q[2].x - q[3].x;
            double dy1 = q[1].y - q[2].y, dy2 = q[3].y - q[2].y, dy3 = q[0].y - q[1].y + q[2].y - q[3].y;
            double g = 0.0, h = 0.0;
            double den = dx1 * dy2 - dx2 * dy1;
            if ((std::fabs(dx3) > 1e-12 || std::fabs(dy3) > 1e-12) && std::fabs(den) > 1e-12) {
                g = (dx3 * dy2 - dx2 * dy3) / den;
                h = (dx1 * dy3 - dx3 * dy1) / den;
            }
            return Projective{q[1].x - q[0].x + g * q[1].x, q[3].x - q[0].x + h * q[3].x, q[0].x,
                q[1].y - q[0].y + g * q[1].y, q[3].y - q[0].y + h * q[3].y, q[0].y, g, h};
        }
    }

    BLPoint QuadFrame::map(double u, double v) const {
        Projective m = projectiveFor(p);
        double denom = m.g * u + m.h * v + 1.0;
        if (std::fabs(denom) < 1e-9) {
            denom = denom < 0 ? -1e-9 : 1e-9;
        }
        return BLPoint((m.a * u + m.b * v + m.c) / denom, (m.d * u + m.e * v + m.f) / denom);
    }

    bool QuadFrame::unmap(const BLPoint& world, double& u, double& v) const {
        Projective m = projectiveFor(p);
        // Invert the 3x3 [[a b c] [d e f] [g h 1]] with the adjugate.
        double A = m.e - m.f * m.h, B = m.c * m.h - m.b, C = m.b * m.f - m.c * m.e;
        double D = m.f * m.g - m.d, E = m.a - m.c * m.g, F = m.c * m.d - m.a * m.f;
        double G = m.d * m.h - m.e * m.g, H = m.b * m.g - m.a * m.h, I = m.a * m.e - m.b * m.d;
        double det = m.a * A + m.b * D + m.c * G;
        if (std::fabs(det) < 1e-12) {
            return false;
        }
        double uu = A * world.x + B * world.y + C;
        double vv = D * world.x + E * world.y + F;
        double ww = G * world.x + H * world.y + I;
        if (std::fabs(ww) < 1e-12) {
            return false;
        }
        u = uu / ww;
        v = vv / ww;
        return true;
    }

    QuadFrame QuadFrame::transformed(const BLMatrix2D& matrix) const {
        QuadFrame result;
        for (int i = 0; i < 4; ++i) {
            result.p[i] = matrix.map_point(p[i]);
        }
        return result;
    }

    QuadFrame SelectionFrame::asQuad() const {
        if (quad.has_value()) {
            return *quad;
        }
        QuadFrame result;
        result.p[0] = toWorld(local.x0, local.y0);
        result.p[1] = toWorld(local.x1, local.y0);
        result.p[2] = toWorld(local.x1, local.y1);
        result.p[3] = toWorld(local.x0, local.y1);
        return result;
    }

    BLPoint SelectionFrame::toWorld(double lx, double ly) const {
        double c = std::cos(angle), s = std::sin(angle);
        return BLPoint(lx * c - ly * s, lx * s + ly * c);
    }

    BLPoint SelectionFrame::toLocal(const BLPoint& world) const {
        double c = std::cos(angle), s = std::sin(angle);
        return BLPoint(world.x * c + world.y * s, -world.x * s + world.y * c);
    }

    bool SketchDocument::selectionFrame(SelectionFrame& out) const {
        double angle = 0.0;
        bool any = false;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            if (!any) {
                angle = object->frameAngle();
                any = true;
            } else if (std::fabs(object->frameAngle() - angle) > 1e-9) {
                angle = groupAngle_.value_or(0.0);  // mixed orientations: the group's own angle
                break;
            }
        }
        if (!any) {
            return false;
        }
        out.angle = angle;
        out.quad = quadFrame_;
        bool first = true;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object == nullptr) {
                continue;
            }
            BLPath path = object->geometry();
            if (angle != 0.0) {
                path.transform(BLMatrix2D::make_rotation(-angle));
            }
            BLBox box(0, 0, 0, 0);
            path.get_bounding_box(&box);
            if (first) {
                out.local = box;
                first = false;
            } else {
                out.local.x0 = std::min(out.local.x0, box.x0);
                out.local.y0 = std::min(out.local.y0, box.y0);
                out.local.x1 = std::max(out.local.x1, box.x1);
                out.local.y1 = std::max(out.local.y1, box.y1);
            }
        }
        return true;
    }

    BLPoint SketchDocument::selectionOrigin(const SelectionFrame& frame) const {
        return frame.pointAt(originU_, originV_);
    }

    void SketchDocument::setSelectionOrigin(const SelectionFrame& frame, const BLPoint& world) {
        double u = 0.5, v = 0.5;
        if (frame.unmapPoint(world, u, v)) {
            originU_ = u;
            originV_ = v;
        }
        updateAllViews();
    }

    void SketchDocument::pruneSelection() {
        std::size_t before = selection_.size();
        selection_.erase(std::remove_if(selection_.begin(), selection_.end(),
            [this](std::size_t id) { return findById(id) == nullptr; }), selection_.end());
        if (selection_.size() != before) {
            groupAngle_.reset();
            quadFrame_.reset();
            originU_ = originV_ = 0.5;
            onSelectionChanged(*this);
        }
    }

    // ---- Ownership moves used by undo ----

    std::unique_ptr<SketchObject> SketchDocument::detach(std::size_t id, std::size_t* index) {
        for (std::size_t i = 0; i < objects_.size(); ++i) {
            if (objects_[i]->id() == id) {
                std::unique_ptr<SketchObject> taken = std::move(objects_[i]);
                objects_.erase(objects_.begin() + std::ptrdiff_t(i));
                if (index != nullptr) {
                    *index = i;
                }
                pruneSelection();
                changed();
                return taken;
            }
        }
        return nullptr;
    }

    void SketchDocument::attach(std::unique_ptr<SketchObject> object, std::size_t index) {
        if (index > objects_.size()) {
            index = objects_.size();
        }
        objects_.insert(objects_.begin() + std::ptrdiff_t(index), std::move(object));
        changed();
    }

    // ---- Undoable edits ----

    namespace {
        // std::function needs a copyable callable, so an object that undo/redo hands back and forth
        // lives in a shared slot.
        struct Slot {
            std::unique_ptr<SketchObject> object;
            std::size_t id = 0;
            std::size_t index = 0;
        };
    }

    SketchObject& SketchDocument::addUndoable(SketchObject* object, const std::string& description) {
        if (object == nullptr) {
            throw std::runtime_error("SketchDocument::addUndoable: object is null");
        }
        auto slot = std::make_shared<Slot>();
        slot->object.reset(object);
        slot->index = objects_.size();
        slot->object->setId(nextId_++);
        slot->id = slot->object->id();
        SketchObject* raw = object;
        undoStack_.push({description,
            [this, slot]() {
                attach(std::move(slot->object), slot->index);
                setSelection({slot->id});
            },
            [this, slot]() {
                slot->object = detach(slot->id, &slot->index);
            }});
        return *raw;
    }

    void SketchDocument::deleteSelection() {
        if (selection_.empty()) {
            return;
        }
        std::vector<std::shared_ptr<Slot>> slots;
        for (std::size_t id : selection_) {
            auto slot = std::make_shared<Slot>();
            slot->id = id;
            slots.push_back(slot);
        }
        std::vector<std::size_t> ids = selection_;
        undoStack_.push({"Delete",
            [this, slots]() {
                // Highest stack position first, so every recorded index is its original one.
                for (std::size_t i = objects_.size(); i > 0; --i) {
                    std::size_t id = objects_[i - 1]->id();
                    for (const std::shared_ptr<Slot>& slot : slots) {
                        if (slot->id == id) {
                            slot->object = detach(id, &slot->index);
                            break;
                        }
                    }
                }
            },
            [this, slots, ids]() {
                // Reinsert lowest index first so each stored index is valid again.
                std::vector<std::shared_ptr<Slot>> ordered = slots;
                std::sort(ordered.begin(), ordered.end(),
                    [](const std::shared_ptr<Slot>& a, const std::shared_ptr<Slot>& b) { return a->index < b->index; });
                for (const std::shared_ptr<Slot>& slot : ordered) {
                    if (slot->object) {
                        attach(std::move(slot->object), slot->index);
                    }
                }
                setSelection(ids);
            }});
    }

    void SketchDocument::duplicateSelection(double dx, double dy) {
        std::vector<std::shared_ptr<Slot>> slots;
        std::vector<std::size_t> ids;
        std::size_t index = objects_.size();
        for (std::size_t id : selection_) {
            const SketchObject* source = findById(id);
            if (source == nullptr) {
                continue;
            }
            auto slot = std::make_shared<Slot>();
            slot->object.reset(source->clone());
            slot->object->transform(BLMatrix2D::make_translation(dx, dy));
            slot->object->setId(nextId_++);
            slot->id = slot->object->id();
            slot->index = index++;
            ids.push_back(slot->id);
            slots.push_back(slot);
        }
        if (slots.empty()) {
            return;
        }
        undoStack_.push({"Duplicate",
            [this, slots, ids]() {
                for (const std::shared_ptr<Slot>& slot : slots) {
                    attach(std::move(slot->object), slot->index);
                }
                setSelection(ids);
            },
            [this, slots]() {
                for (std::size_t i = slots.size(); i > 0; --i) {
                    slots[i - 1]->object = detach(slots[i - 1]->id, &slots[i - 1]->index);
                }
            }});
    }

    bool SketchDocument::resetBoundingBox() {
        std::vector<GeometryState> before = snapshotSelection();
        if (before.empty()) {
            return false;
        }
        FrameState frameBefore = frameState();
        bool needed = groupAngle_.has_value() || quadFrame_.has_value();
        for (const GeometryState& entry : before) {
            needed = needed || std::fabs(entry.angle) > 1e-12;
        }
        if (!needed) {
            return false;
        }
        for (const GeometryState& entry : before) {
            if (SketchObject* object = findById(entry.id)) {
                object->setFrameAngle(0.0);
            }
        }
        groupAngle_.reset();
        quadFrame_.reset();
        originU_ = originV_ = 0.5;
        commitGeometry(before, "Reset Bounding Box", frameBefore);
        return true;
    }

    std::vector<GeometryState> SketchDocument::snapshotSelection() const {
        std::vector<GeometryState> result;
        for (std::size_t id : selection_) {
            if (const SketchObject* object = findById(id)) {
                result.push_back({id, object->geometry(), object->frameAngle()});
            }
        }
        return result;
    }

    void SketchDocument::commitGeometry(const std::vector<GeometryState>& before, const std::string& description,
        const FrameState& frameBefore) {
        FrameState frameAfter = frameState();
        std::vector<GeometryState> after;
        for (const GeometryState& entry : before) {
            if (const SketchObject* object = findById(entry.id)) {
                after.push_back({entry.id, object->geometry(), object->frameAngle()});
            }
        }
        auto apply = [this](const std::vector<GeometryState>& state, const FrameState& frame) {
            for (const GeometryState& entry : state) {
                if (SketchObject* object = findById(entry.id)) {
                    object->setGeometry(entry.path);
                    object->setFrameAngle(entry.angle);
                }
            }
            groupAngle_ = frame.groupAngle;
            quadFrame_ = frame.quad;
            changed();
        };
        undoStack_.push({description,
            [apply, after, frameAfter]() { apply(after, frameAfter); },
            [apply, before, frameBefore]() { apply(before, frameBefore); }});
    }

    void SketchDocument::transformObjects(const std::vector<std::size_t>& ids, const BLMatrix2D& matrix, const std::string& description) {
        std::vector<GeometryState> before;
        FrameState frameBefore = frameState();
        for (std::size_t id : ids) {
            if (SketchObject* object = findById(id)) {
                before.push_back({id, object->geometry(), object->frameAngle()});
                object->transform(matrix);
            }
        }
        if (!before.empty()) {
            if (quadFrame_.has_value()) {
                quadFrame_ = quadFrame_->transformed(matrix);
            }
            commitGeometry(before, description, frameBefore);
        }
    }

    bool SketchDocument::readFromFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            return false;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        return documentFromSvg(buffer.str(), *this);
    }

    bool SketchDocument::writeToFile(const std::string& path) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            return false;
        }
        out << svgFromDocument(*this);
        return static_cast<bool>(out);
    }

    void SketchDocument::changed() {
        markModified();
        updateAllViews();
        onChanged(*this);
    }

    // ---- Style ----

    std::vector<StyleState> SketchDocument::snapshotStyle() const {
        std::vector<StyleState> result;
        for (std::size_t id : selection_) {
            if (const SketchObject* object = findById(id)) {
                result.push_back({id, object->fill(), object->stroke(), object->strokeWidth(), object->opacity()});
            }
        }
        return result;
    }

    void SketchDocument::setSelectionFill(const SketchPaint& paint) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setFill(paint);
            }
        }
        changed();
    }

    void SketchDocument::setSelectionStroke(const SketchPaint& paint) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setStroke(paint);
            }
        }
        changed();
    }

    void SketchDocument::setSelectionStrokeWidth(double width) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setStrokeWidth(width);
            }
        }
        changed();
    }

    void SketchDocument::setSelectionOpacity(double opacity) {
        for (std::size_t id : selection_) {
            if (SketchObject* object = findById(id)) {
                object->setOpacity(opacity);
            }
        }
        changed();
    }

    bool SketchDocument::commitStyle(const std::vector<StyleState>& before, const std::string& description) {
        std::vector<StyleState> after;
        bool differs = false;
        for (const StyleState& old : before) {
            const SketchObject* object = findById(old.id);
            if (object == nullptr) {
                continue;
            }
            StyleState now{old.id, object->fill(), object->stroke(), object->strokeWidth(), object->opacity()};
            differs = differs || now.fill != old.fill || now.stroke != old.stroke ||
                now.strokeWidth != old.strokeWidth || now.opacity != old.opacity;
            after.push_back(now);
        }
        if (!differs) {
            return false;
        }
        auto apply = [this](const std::vector<StyleState>& states) {
            for (const StyleState& state : states) {
                if (SketchObject* object = findById(state.id)) {
                    object->setFill(state.fill);
                    object->setStroke(state.stroke);
                    object->setStrokeWidth(state.strokeWidth);
                    object->setOpacity(state.opacity);
                }
            }
            changed();
        };
        undoStack_.push({description,
            [apply, after]() { apply(after); },
            [apply, before]() { apply(before); }});
        return true;
    }

    namespace {
        SketchPaint paintInSpace(const SketchPaint& paint, ColorSpace space) {
            if (paint.isNone()) {
                return paint;
            }
            if (!paint.isGradient()) {
                return SketchPaint::solid(paint.color().convertedTo(space));
            }
            SketchGradient gradient = paint.gradient();
            for (GradientStop& stop : gradient.stops) {
                stop.color = stop.color.convertedTo(space);
            }
            return paint.kind() == SketchPaint::Kind::Radial ? SketchPaint::radial(gradient) : SketchPaint::linear(gradient);
        }
    }

    void SketchDocument::setColorMode(ColorSpace mode) {
        if (mode != ColorSpace::RGB && mode != ColorSpace::CMYK) {
            throw std::runtime_error("SketchDocument::setColorMode: a document is RGB or CMYK");
        }
        colorMode_ = mode;
        defaultFill_ = paintInSpace(defaultFill_, mode);
        defaultStroke_ = paintInSpace(defaultStroke_, mode);
        changed();
    }

    bool SketchDocument::convertToColorMode(ColorSpace mode) {
        if (mode != ColorSpace::RGB && mode != ColorSpace::CMYK) {
            throw std::runtime_error("SketchDocument::convertToColorMode: a document is RGB or CMYK");
        }
        if (mode == colorMode_) {
            return false;
        }
        // Everything the conversion touches, before and after.
        struct State {
            ColorSpace mode;
            SketchPaint defaultFill, defaultStroke;
            std::vector<StyleState> styles;
        };
        auto capture = [this]() {
            State state{colorMode_, defaultFill_, defaultStroke_, {}};
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                state.styles.push_back({object->id(), object->fill(), object->stroke(), object->strokeWidth(), object->opacity()});
            }
            return state;
        };
        State before = capture();
        colorMode_ = mode;
        defaultFill_ = paintInSpace(defaultFill_, mode);
        defaultStroke_ = paintInSpace(defaultStroke_, mode);
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            object->setFill(paintInSpace(object->fill(), mode));
            object->setStroke(paintInSpace(object->stroke(), mode));
        }
        State after = capture();
        auto apply = [this](const State& state) {
            colorMode_ = state.mode;
            defaultFill_ = state.defaultFill;
            defaultStroke_ = state.defaultStroke;
            for (const StyleState& style : state.styles) {
                if (SketchObject* object = findById(style.id)) {
                    object->setFill(style.fill);
                    object->setStroke(style.stroke);
                }
            }
            changed();
        };
        undoStack_.push({mode == ColorSpace::CMYK ? "Convert to CMYK" : "Convert to RGB",
            [apply, after]() { apply(after); },
            [apply, before]() { apply(before); }});
        changed();
        return true;
    }

    void SketchDocument::setDefaultFill(const SketchPaint& paint) {
        defaultFill_ = paint;
        onChanged(*this);
    }

    void SketchDocument::setDefaultStroke(const SketchPaint& paint) {
        defaultStroke_ = paint;
        onChanged(*this);
    }

    void SketchDocument::setDefaultStrokeWidth(double width) {
        defaultStrokeWidth_ = width < 0.0 ? 0.0 : width;
        onChanged(*this);
    }

}

#include "sketchdocument.h"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <unordered_map>

// Layers: a tree of named buckets (see Layer in sketchdocument.h). The document keeps its flat object list sorted by layer after every
// change (syncLayers, called from changed()), so painting, hit testing and the file formats need not know about the tree beyond each
// object's layer id. Each tree edit is one undo step made from a snapshot of the tree.

namespace sketch {

    const Layer* SketchDocument::findLayer(std::size_t id) const {
        for (const Layer& layer : layers_) {
            if (layer.id == id) {
                return &layer;
            }
        }
        return nullptr;
    }

    std::vector<std::size_t> SketchDocument::layerChildren(std::size_t parent) const {
        std::vector<std::size_t> out;
        for (const Layer& layer : layers_) {
            if (layer.parent == parent) {
                out.push_back(layer.id);
            }
        }
        return out;
    }

    std::size_t SketchDocument::layerDepth(std::size_t id) const {
        std::size_t depth = 0;
        const Layer* layer = findLayer(id);
        while (layer != nullptr && layer->parent != 0 && depth < layers_.size()) {
            layer = findLayer(layer->parent);
            ++depth;
        }
        return depth;
    }

    bool SketchDocument::layerContains(std::size_t ancestor, std::size_t id) const {
        const Layer* layer = findLayer(id);
        for (std::size_t guard = 0; layer != nullptr && guard <= layers_.size(); ++guard) {
            if (layer->id == ancestor) {
                return true;
            }
            layer = layer->parent != 0 ? findLayer(layer->parent) : nullptr;
        }
        return false;
    }

    bool SketchDocument::layerShown(std::size_t id) const {
        const Layer* layer = findLayer(id);
        for (std::size_t guard = 0; layer != nullptr && guard <= layers_.size(); ++guard) {
            if (!layer->visible) {
                return false;
            }
            layer = layer->parent != 0 ? findLayer(layer->parent) : nullptr;
        }
        return true;
    }

    bool SketchDocument::layerEditable(std::size_t id) const {
        const Layer* layer = findLayer(id);
        for (std::size_t guard = 0; layer != nullptr && guard <= layers_.size(); ++guard) {
            if (layer->locked) {
                return false;
            }
            layer = layer->parent != 0 ? findLayer(layer->parent) : nullptr;
        }
        return true;
    }

    std::size_t SketchDocument::layerObjectCount(std::size_t id, bool withSublayers) const {
        std::size_t count = 0;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (withSublayers ? layerContains(id, object->layer()) : object->layer() == id) {
                ++count;
            }
        }
        return count;
    }

    std::vector<std::size_t> SketchDocument::objectsInLayer(std::size_t id) const {
        std::vector<std::size_t> out;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            if (layerContains(id, object->layer())) {
                out.push_back(object->id());
            }
        }
        return out;
    }

    void SketchDocument::setActiveLayer(std::size_t id) {
        if (findLayer(id) != nullptr && id != activeLayer_) {
            activeLayer_ = id;
            onChanged(*this);
        }
    }

    std::size_t SketchDocument::selectionLayer() const {
        if (selection_.empty()) {
            return 0;
        }
        const SketchObject* object = findById(selection_.front());
        return object != nullptr ? object->layer() : 0;
    }

    void SketchDocument::setLayerExpanded(std::size_t id, bool expanded) {
        for (Layer& layer : layers_) {
            if (layer.id == id && layer.expanded != expanded) {
                layer.expanded = expanded;
                onChanged(*this);
            }
        }
    }

    std::size_t SketchDocument::nextLayerId() const {
        std::size_t next = 1;
        for (const Layer& layer : layers_) {
            next = std::max(next, layer.id + 1);
        }
        return next;
    }

    // ---- order ----

    // Post-order of the tree: a layer's sublayers (back first) and then the layer itself, so its own objects lie above its sublayers.
    std::vector<int> SketchDocument::layerRanks() const {
        std::size_t top = 0;
        for (const Layer& layer : layers_) {
            top = std::max(top, layer.id);
        }
        std::vector<int> rank(top + 1, -1);
        int counter = 0;
        std::vector<std::pair<std::size_t, std::size_t>> stack;   // (layer, next child index)
        std::vector<std::vector<std::size_t>> kids;
        auto enter = [&](std::size_t id) {
            stack.push_back({id, 0});
            kids.push_back(layerChildren(id));
        };
        for (std::size_t root : layerChildren(0)) {
            enter(root);
            while (!stack.empty()) {
                auto& [id, next] = stack.back();
                if (next < kids.back().size()) {
                    std::size_t child = kids.back()[next++];
                    if (rank[child] < 0 && stack.size() <= layers_.size()) {
                        enter(child);
                    }
                } else {
                    rank[id] = counter++;
                    stack.pop_back();
                    kids.pop_back();
                }
            }
        }
        return rank;
    }

    void SketchDocument::syncLayers() {
        std::vector<int> rank = layerRanks();
        std::vector<char> hidden(rank.size(), 0), locked(rank.size(), 0);
        for (const Layer& layer : layers_) {
            hidden[layer.id] = layerShown(layer.id) ? 0 : 1;
            locked[layer.id] = layerEditable(layer.id) ? 0 : 1;
        }
        bool sorted = true;
        int last = -1;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            std::size_t layer = object->layer();
            if (layer >= rank.size() || rank[layer] < 0) {
                layer = activeLayer_;
                object->setLayer(layer);
            }
            object->setLayerState(hidden[layer] != 0, locked[layer] != 0);
            if (rank[layer] < last) {
                sorted = false;
            }
            last = rank[layer];
        }
        if (!sorted) {
            std::stable_sort(objects_.begin(), objects_.end(), [&rank](const std::unique_ptr<SketchObject>& a, const std::unique_ptr<SketchObject>& b) {
                return rank[a->layer()] < rank[b->layer()];
            });
        }
    }

    std::vector<std::size_t> SketchDocument::layerSorted(const std::vector<std::size_t>& ids) const {
        std::vector<int> rank = layerRanks();
        std::unordered_map<std::size_t, int> of;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            std::size_t layer = object->layer();
            of[object->id()] = layer < rank.size() && rank[layer] >= 0 ? rank[layer] : 0;
        }
        std::vector<std::size_t> out = ids;
        std::stable_sort(out.begin(), out.end(), [&of](std::size_t a, std::size_t b) { return of[a] < of[b]; });
        return out;
    }

    // A hidden or locked object cannot stay selected.
    void SketchDocument::dropUnselectable() {
        syncLayers();
        std::vector<std::size_t> keep;
        for (std::size_t id : selection_) {
            const SketchObject* object = findById(id);
            if (object != nullptr && object->isVisible() && !object->isLocked()) {
                keep.push_back(id);
            }
        }
        if (keep.size() != selection_.size()) {
            setSelection(keep);
        }
    }

    void SketchDocument::resetLayers() {
        layers_ = {Layer{1, 0, "Layer 1", true, false, true}};
        activeLayer_ = 1;
    }

    void SketchDocument::setLayers(std::vector<Layer> layers) {
        layers_ = std::move(layers);
        if (layers_.empty()) {
            resetLayers();
        }
        std::vector<std::size_t> top = layerChildren(0);
        activeLayer_ = top.empty() ? layers_.back().id : top.back();
        syncLayers();
        updateAllViews();
        onChanged(*this);
    }

    // ---- undo ----

    SketchDocument::LayerState SketchDocument::snapshotLayers(bool withObjects) const {
        LayerState state;
        state.layers = layers_;
        state.activeLayer = activeLayer_;
        if (withObjects) {
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                state.objectLayers.push_back({object->id(), object->layer()});
                state.order.push_back(object->id());
            }
        }
        return state;
    }

    void SketchDocument::applyLayers(const LayerState& state) {
        layers_ = state.layers;
        activeLayer_ = state.activeLayer;
        if (!state.objectLayers.empty()) {
            std::unordered_map<std::size_t, SketchObject*> byId;
            for (const std::unique_ptr<SketchObject>& object : objects_) {
                byId[object->id()] = object.get();
            }
            for (const auto& [id, layer] : state.objectLayers) {
                auto it = byId.find(id);
                if (it != byId.end()) {
                    it->second->setLayer(layer);
                }
            }
        }
        dropUnselectable();
        if (!state.order.empty()) {
            reorder(state.order);
        } else {
            changed();
        }
    }

    void SketchDocument::pushLayerStep(const std::string& description, const LayerState& before, const LayerState& after) {
        undoStack_.push({description, [this, after]() { applyLayers(after); }, [this, before]() { applyLayers(before); }});
    }

    // ---- edits ----

    std::size_t SketchDocument::addLayer(std::size_t parent, const std::string& name) {
        if (parent != 0 && findLayer(parent) == nullptr) {
            return 0;
        }
        LayerState before = snapshotLayers(false), after = before;
        Layer layer;
        layer.id = nextLayerId();
        layer.parent = parent;
        layer.name = name.empty() ? "Layer " + std::to_string(layer.id) : name;
        after.layers.push_back(layer);
        for (Layer& p : after.layers) {
            if (p.id == parent) {
                p.expanded = true;
            }
        }
        after.activeLayer = layer.id;
        pushLayerStep(parent != 0 ? "New Sublayer" : "New Layer", before, after);
        return layer.id;
    }

    bool SketchDocument::renameLayer(std::size_t id, const std::string& name) {
        const Layer* layer = findLayer(id);
        if (layer == nullptr || name.empty() || layer->name == name) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        for (Layer& l : after.layers) {
            if (l.id == id) {
                l.name = name;
            }
        }
        pushLayerStep("Rename Layer", before, after);
        return true;
    }

    bool SketchDocument::setLayerVisible(std::size_t id, bool visible) {
        const Layer* layer = findLayer(id);
        if (layer == nullptr || layer->visible == visible) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        for (Layer& l : after.layers) {
            if (l.id == id) {
                l.visible = visible;
            }
        }
        pushLayerStep(visible ? "Show Layer" : "Hide Layer", before, after);
        return true;
    }

    bool SketchDocument::setLayerLocked(std::size_t id, bool locked) {
        const Layer* layer = findLayer(id);
        if (layer == nullptr || layer->locked == locked) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        for (Layer& l : after.layers) {
            if (l.id == id) {
                l.locked = locked;
            }
        }
        pushLayerStep(locked ? "Lock Layer" : "Unlock Layer", before, after);
        return true;
    }

    namespace {
        struct DeletedObject {
            std::size_t id = 0;
            std::size_t index = 0;
            std::unique_ptr<SketchObject> object;
        };
    }

    bool SketchDocument::deleteLayer(std::size_t id) {
        if (findLayer(id) == nullptr) {
            return false;
        }
        std::vector<Layer> remaining;
        for (const Layer& layer : layers_) {
            if (!layerContains(id, layer.id)) {
                remaining.push_back(layer);
            }
        }
        if (remaining.empty()) {
            return false;   // the last layer stays
        }
        std::vector<Layer> allBefore = layers_;
        std::size_t activeBefore = activeLayer_;
        std::size_t activeAfter = activeLayer_;
        if (layerContains(id, activeLayer_)) {
            activeAfter = remaining.back().id;
            for (const Layer& layer : remaining) {
                if (layer.parent == 0) {
                    activeAfter = layer.id;
                }
            }
        }
        std::vector<std::shared_ptr<DeletedObject>> slots;
        for (std::size_t objectId : objectsInLayer(id)) {
            auto slot = std::make_shared<DeletedObject>();
            slot->id = objectId;
            slots.push_back(slot);
        }
        undoStack_.push({"Delete Layer",
            [this, slots, remaining, activeAfter]() {
                for (std::size_t i = objects_.size(); i > 0; --i) {   // highest position first, so each recorded index is the original
                    std::size_t objectId = objects_[i - 1]->id();
                    for (const std::shared_ptr<DeletedObject>& slot : slots) {
                        if (slot->id == objectId) {
                            slot->object = detach(objectId, &slot->index);
                            break;
                        }
                    }
                }
                layers_ = remaining;
                activeLayer_ = activeAfter;
                dropUnselectable();
                changed();
            },
            [this, slots, allBefore, activeBefore]() {
                layers_ = allBefore;
                activeLayer_ = activeBefore;
                std::vector<std::shared_ptr<DeletedObject>> ordered = slots;
                std::sort(ordered.begin(), ordered.end(), [](const std::shared_ptr<DeletedObject>& a, const std::shared_ptr<DeletedObject>& b) {
                    return a->index < b->index;
                });
                for (const std::shared_ptr<DeletedObject>& slot : ordered) {
                    if (slot->object) {
                        attach(std::move(slot->object), slot->index);
                    }
                }
                changed();
            }});
        return true;
    }

    bool SketchDocument::moveLayer(std::size_t id, std::size_t parent, std::size_t position) {
        const Layer* found = findLayer(id);
        if (found == nullptr || (parent != 0 && (findLayer(parent) == nullptr || layerContains(id, parent)))) {
            return false;
        }
        LayerState before = snapshotLayers(false), after = before;
        Layer moved = *found;
        moved.parent = parent;
        after.layers.erase(std::remove_if(after.layers.begin(), after.layers.end(), [id](const Layer& l) { return l.id == id; }), after.layers.end());
        std::vector<std::size_t> siblings;
        for (const Layer& l : after.layers) {
            if (l.parent == parent) {
                siblings.push_back(l.id);
            }
        }
        std::size_t at = after.layers.size();   // after everything: the front of its siblings
        if (position < siblings.size()) {
            for (std::size_t i = 0; i < after.layers.size(); ++i) {
                if (after.layers[i].id == siblings[position]) {
                    at = i;
                    break;
                }
            }
        }
        after.layers.insert(after.layers.begin() + std::ptrdiff_t(at), moved);
        for (Layer& l : after.layers) {
            if (l.id == parent) {
                l.expanded = true;
            }
        }
        bool same = after.layers.size() == before.layers.size();
        for (std::size_t i = 0; same && i < after.layers.size(); ++i) {
            same = after.layers[i].id == before.layers[i].id && after.layers[i].parent == before.layers[i].parent;
        }
        if (same) {
            return false;
        }
        pushLayerStep("Move Layer", before, after);
        return true;
    }

    bool SketchDocument::moveSelectionToLayer(std::size_t id) {
        if (findLayer(id) == nullptr || selection_.empty()) {
            return false;
        }
        bool any = false;
        for (std::size_t objectId : selection_) {
            const SketchObject* object = findById(objectId);
            any = any || (object != nullptr && object->layer() != id);
        }
        if (!any) {
            return false;
        }
        LayerState before = snapshotLayers(true);
        std::vector<std::size_t> others, moved;
        for (const std::unique_ptr<SketchObject>& object : objects_) {
            (isSelected(object->id()) ? moved : others).push_back(object->id());
        }
        for (std::size_t objectId : moved) {
            findById(objectId)->setLayer(id);
        }
        others.insert(others.end(), moved.begin(), moved.end());   // the moved ones land on top of their new layer
        std::vector<std::size_t> selected = selection_;
        reorder(others);
        LayerState after = snapshotLayers(true);
        undoStack_.push({"Move to Layer",
            [this, after]() { applyLayers(after); },
            [this, before]() { applyLayers(before); }});
        setSelection(selected);
        dropUnselectable();
        return true;
    }

    void SketchDocument::selectLayerObjects(std::size_t id) {
        std::vector<std::size_t> ids;
        for (std::size_t objectId : objectsInLayer(id)) {
            const SketchObject* object = findById(objectId);
            if (object != nullptr && object->isVisible() && !object->isLocked()) {
                ids.push_back(objectId);
            }
        }
        setSelection(ids);
    }

}

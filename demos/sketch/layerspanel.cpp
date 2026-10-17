#include "layerspanel.h"

#include <newui/utils.h>

#include <algorithm>

namespace sketch {

    LayersPanel::LayersPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        tree_ = dynamic_cast<LayerTree*>(root_.findView("layersTree"));
        name_ = dynamic_cast<newui::TextField*>(root_.findView("layerNameField"));
        if (tree_ != nullptr) {
            LayerTree::Actions actions;
            actions.toggleVisible = [this](std::size_t id) {
                if (const Layer* layer = document_.findLayer(id)) { document_.setLayerVisible(id, !layer->visible); }
            };
            actions.toggleLock = [this](std::size_t id) {
                if (const Layer* layer = document_.findLayer(id)) { document_.setLayerLocked(id, !layer->locked); }
            };
            actions.toggleOpen = [this](std::size_t id) {
                if (const Layer* layer = document_.findLayer(id)) { document_.setLayerExpanded(id, !layer->expanded); }
            };
            actions.activate = [this](std::size_t id, bool selectObjects) {
                document_.setActiveLayer(id);
                if (selectObjects) { document_.selectLayerObjects(id); }
                refresh();
            };
            actions.moveSelectionHere = [this](std::size_t id) { document_.moveSelectionToLayer(id); };
            actions.drop = [this](std::size_t source, std::size_t target, LayerTree::Drop where) { drop(source, target, where); };
            tree_->setActions(actions);
        }
        auto onButton = [this](const char* name, std::function<void()> action) {
            if (auto* button = dynamic_cast<newui::Control*>(root_.findView(name))) {
                button->onClick += [action](newui::Control&) {
                    action();
                    return newui::SyncReturn::Handled;
                };
            }
        };
        onButton("addLayerButton", [this]() { document_.addLayer(0); });
        onButton("addSublayerButton", [this]() { document_.addLayer(document_.activeLayer()); });
        onButton("deleteLayerButton", [this]() { document_.deleteLayer(document_.activeLayer()); });
        if (name_ != nullptr) {
            name_->onReturnPressed += [this](newui::TextField&) {
                if (!updating_) { document_.renameLayer(document_.activeLayer(), newui::wideToUtf8(name_->text())); }
                return newui::SyncReturn::Handled;
            };
        }
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    // Top of the stack first: a layer's sublayers are listed below it, the frontmost first.
    void LayersPanel::addRows(std::size_t parent, std::vector<LayerRow>& rows) const {
        std::vector<std::size_t> ids = document_.layerChildren(parent);
        for (std::size_t i = ids.size(); i > 0; --i) {
            const Layer& layer = *document_.findLayer(ids[i - 1]);
            LayerRow row;
            row.id = layer.id;
            row.depth = document_.layerDepth(layer.id);
            row.name = layer.name;
            row.visible = layer.visible;
            row.locked = layer.locked;
            row.shown = document_.layerShown(layer.id);
            row.editable = document_.layerEditable(layer.id);
            row.hasChildren = !document_.layerChildren(layer.id).empty();
            row.expanded = layer.expanded;
            row.active = layer.id == document_.activeLayer();
            for (std::size_t id : document_.selection()) {
                const SketchObject* object = document_.findById(id);
                if (object != nullptr && object->layer() == layer.id) {
                    row.holdsSelection = true;
                }
            }
            rows.push_back(row);
            if (layer.expanded && row.hasChildren) {
                addRows(layer.id, rows);
            }
        }
    }

    void LayersPanel::refresh() {
        std::vector<LayerRow> rows;
        addRows(0, rows);
        if (tree_ != nullptr) {
            tree_->setRows(std::move(rows));
        }
        if (name_ != nullptr) {
            const Layer* layer = document_.findLayer(document_.activeLayer());
            updating_ = true;
            name_->setText(newui::utf8ToWide(layer != nullptr ? layer->name : std::string()));
            updating_ = false;
        }
        if (auto* button = dynamic_cast<newui::Control*>(root_.findView("deleteLayerButton"))) {
            button->setEnabled(document_.layers().size() > 1);
        }
    }

    // A row dropped on another: above it, below it, or (the middle) inside it as its front-most sublayer.
    void LayersPanel::drop(std::size_t source, std::size_t target, LayerTree::Drop where) {
        const Layer* t = document_.findLayer(target);
        if (t == nullptr || source == target) {
            return;
        }
        if (where == LayerTree::Drop::Inside) {
            document_.moveLayer(source, target, document_.layerChildren(target).size());
            return;
        }
        std::vector<std::size_t> siblings = document_.layerChildren(t->parent);
        siblings.erase(std::remove(siblings.begin(), siblings.end(), source), siblings.end());
        std::size_t at = std::size_t(std::find(siblings.begin(), siblings.end(), target) - siblings.begin());
        document_.moveLayer(source, t->parent, where == LayerTree::Drop::Above ? at + 1 : at);
    }

}

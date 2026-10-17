#pragma once

#include "layertree.h"
#include "sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

namespace sketch {

    // The Layers panel of sketch.newui: the layer tree (LayerTree) with New Layer / New Sublayer / Delete buttons and a field for the
    // name of the active layer. The tree reports clicks and drags; this turns them into the document's undoable layer edits, and
    // rebuilds the tree's rows whenever the document or the selection changes.
    //
    // A newui::Controller that, like the other controllers here, does not call Controller::setModel(): the document is observed
    // through a plain reference and must outlive it.
    // @reflect ignore=true
    class LayersPanel : public newui::Controller {
    public:
        LayersPanel(newui::RootView& root, SketchDocument& document);
        ~LayersPanel() override = default;
        LayersPanel(const LayersPanel&) = delete;
        LayersPanel& operator=(const LayersPanel&) = delete;

        // Rebuilds the rows and the name field from the document.
        void refresh();

    private:
        void addRows(std::size_t parent, std::vector<LayerRow>& rows) const;
        void drop(std::size_t source, std::size_t target, LayerTree::Drop where);

        newui::RootView& root_;
        SketchDocument& document_;
        LayerTree* tree_ = nullptr;
        newui::TextField* name_ = nullptr;
        bool updating_ = false;
    };

}

#pragma once

#include <newui/subview.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

namespace sketch {

    // One line of the Layers panel.
    // @reflect ignore=true
    struct LayerRow {
        std::size_t id = 0;
        std::size_t depth = 0;         // 0 for a top-level layer, 1 for its sublayers...
        std::string name;
        bool visible = true;           // the layer's own eye / lock (the row is drawn dimmer when a layer above hides or locks it)
        bool locked = false;
        bool shown = true;             // effective: no layer above hides it
        bool editable = true;
        bool hasChildren = false;
        bool expanded = true;
        bool active = false;           // the layer new objects go to
        bool holdsSelection = false;   // some selected object is in it
    };

    // The tree of the Layers panel: one 20 px row per layer, indented by depth, each with an eye, a lock, an open / close triangle,
    // the name, and at the right a square that is filled when the selection is in the layer (click it to move the selection there).
    // Dragging a row onto another puts it above, below or (the middle) inside it. The tree only draws and reports what was done
    // through `actions`; LayersPanel does the editing. Shows the rows that fit and scrolls to keep the active one in sight.
    // @reflect category=basic
    class LayerTree : public newui::SubView {
    public:
        LayerTree();
        virtual ~LayerTree() {}

        static constexpr float kRowHeight = 20.0f;
        enum class Drop { Above, Inside, Below };

        struct Actions {
            std::function<void(std::size_t)> toggleVisible;
            std::function<void(std::size_t)> toggleLock;
            std::function<void(std::size_t)> toggleOpen;
            std::function<void(std::size_t, bool)> activate;                // (layer, with Ctrl: select its objects too)
            std::function<void(std::size_t)> moveSelectionHere;
            std::function<void(std::size_t, std::size_t, Drop)> drop;       // (dragged layer, target layer, where)
        };
        void setActions(Actions actions) { actions_ = std::move(actions); }

        void setRows(std::vector<LayerRow> rows);
        const std::vector<LayerRow>& rows() const { return rows_; }
        // How many rows are showing at the current height.
        std::size_t visibleRows() const;
        // Where the parts of row `index` (a position in rows()) are, in the view's own coordinates - what a click hits.
        newui::Rect eyeRect(std::size_t index) const;
        newui::Rect lockRect(std::size_t index) const;
        newui::Rect triangleRect(std::size_t index) const;
        newui::Rect targetRect(std::size_t index) const;
        newui::Rect nameRect(std::size_t index) const;

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn mouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn mouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn mouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        // The row (an index into rows()) under the point, or rows().size().
        std::size_t rowAt(const newui::Point& pt) const;
        Drop zoneAt(const newui::Point& pt, std::size_t row) const;
        newui::Rect rowRect(std::size_t index) const;

        std::vector<LayerRow> rows_;
        Actions actions_;
        std::size_t scroll_ = 0;           // the first row showing
        std::size_t pressed_ = 0;          // the row a press began on (rows_.size() = none)
        bool dragging_ = false;
        std::size_t dropRow_ = 0;
        Drop dropZone_ = Drop::Inside;
        bool ctrl_ = false;
    };

}

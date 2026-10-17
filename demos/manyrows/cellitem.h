#pragma once

#include <newui/items.h>

namespace newui {
    class Model;
}


namespace manyrows {

    // Paints one cell of the manyrows table with column-aware typography (the cell text itself
    // comes from the model, as for any TableItem):
    //   - Integer / Number columns: right-aligned, so digits line up at the right edge;
    //   - Date: centered;
    //   - Text: left-aligned;
    //   - Status: a colored dot + colored text (Pending amber, Shipped blue, Delivered green,
    //     Returned orange, Cancelled red);
    //   - Rating: colored by value (low red, middling amber, high green);
    //   - Order (row id): dimmed, like a spreadsheet's key column.
    // A selected cell drops all colors and uses the highlight text color, so it stays readable.
    //
    // Pooled by TableController like any TableItem - ManyRowsModel's controller names this class
    // through setDefaultItemClassName("CellItem"), so it must be reflected.
    // @reflect category=basic
    class CellItem : public newui::TableItem {
    public:
        // Declared (not implicit) so reflectgen registers it - the controller builds items by name.
        CellItem() {}

        void paint(BLContext& ctx, const newui::Rect& rect, std::size_t row, std::size_t col,
            newui::TableController& controller) override;

        // Performance: looking up the system font and the theme colors costs more than painting a cell, and
        // newui does not cache them. The view that paints a pass of cells (TimedTableView) calls beginPass()
        // once before it and endPass() after, and every CellItem in between shares the result. Painting
        // outside a pass still works - each cell then resolves its own (slow, but correct).
        // @reflect ignore=true
        static void beginPass(const newui::Model* model);
        // @reflect ignore=true
        static void endPass();
    };

}

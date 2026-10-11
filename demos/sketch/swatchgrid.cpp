#include "swatchgrid.h"

#include <newui/color.h>
#include <newui/uicolormanager.h>

#include <algorithm>
#include <stdexcept>

namespace sketch {

    using namespace newui;

    SwatchGrid::SwatchGrid() {
        setName("SwatchGrid");
        setVisible(true);
        selectedIndex_ = 0;
    }

    void SwatchGrid::addSwatch(const std::string& color) {
        swatches_.push_back(color);
        style().markDirty();
    }

    void SwatchGrid::removeSwatch(const std::string& color) {
        auto it = std::find(swatches_.begin(), swatches_.end(), color);
        if (it != swatches_.end()) {
            swatches_.erase(it);
            style().markDirty();
        }
    }

    void SwatchGrid::setColumns(std::size_t columns) {
        if (columns == 0) {
            throw std::runtime_error("SwatchGrid::setColumns: columns must be at least 1");
        }
        columns_ = columns;
        style().markDirty();
    }

    void SwatchGrid::setGap(float gap) {
        gap_ = gap < 0.0f ? 0.0f : gap;
        style().markDirty();
    }

    void SwatchGrid::setSelectedIndex(std::size_t index) {
        selectedIndex_ = index;
        style().markDirty();
    }

    // Tiles are square, sized so `columns` of them (plus gaps) exactly span the view's width.
    void SwatchGrid::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f || swatches_.empty()) {
            return;
        }

        double tile = (double(client.width()) - double(gap_) * double(columns_ - 1)) / double(columns_);
        if (tile < 2.0) {
            return;
        }

        ctx.save();
        for (std::size_t i = 0; i < swatches_.size(); ++i) {
            Color color;
            if (!Color::fromString(swatches_[i], color)) {
                continue;
            }
            double x = client.left() + double(i % columns_) * (tile + double(gap_));
            double y = client.top() + double(i / columns_) * (tile + double(gap_));
            if (y > double(client.top() + client.height())) {
                break;
            }
            ctx.set_fill_style(color.toBLRgba32());
            ctx.fill_rect(BLRect(x, y, tile, tile));
            bool selected = (i == selectedIndex_);
            ctx.set_stroke_style(selected
                ? UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32()
                : BLRgba32(0x59000000u));
            ctx.set_stroke_width(selected ? 2.0 : 1.0);
            ctx.stroke_rect(BLRect(x + 0.5, y + 0.5, tile - 1.0, tile - 1.0));
        }
        ctx.restore();
    }

}

#pragma once

#include <newui/subview.h>

#include <cstddef>
#include <string>
#include <vector>

namespace sketch {

    // A wrapping grid of small color tiles - used for the Color panel's swatch row and (with
    // larger tiles) the Graphic Styles panel. Each swatch is any newui::Color::fromString() text.
    //
    // Paints the tiles, tracks which one is selected and fires onSwatchPicked on a click; the style panel
    // applies the picked color.
    // @reflect category=basic
    class SwatchGrid : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        SwatchGrid();
        virtual ~SwatchGrid() {}

        // @reflect collection add=addSwatch remove=removeSwatch
        const std::vector<std::string>& swatches() const { return swatches_; }
        void addSwatch(const std::string& color);
        void removeSwatch(const std::string& color);

        // Tiles per row. Always at least 1.
        std::size_t columns() const { return columns_; }
        void setColumns(std::size_t columns);

        float gap() const { return gap_; }
        void setGap(float gap);

        // Index into swatches(); swatches().size() (the default) means none.
        std::size_t selectedIndex() const { return selectedIndex_; }
        void setSelectedIndex(std::size_t index);

        // Fired when the user clicks a tile (selectedIndex() already points at it).
        newui::Delegate<SwatchGrid> onSwatchPicked;

        void paint(BLContext& ctx) override;

    private:
        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);

        std::vector<std::string> swatches_;
        std::size_t columns_ = 12;
        float gap_ = 3.0f;
        std::size_t selectedIndex_ = 0;
    };

}

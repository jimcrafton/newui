#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "newui/controllers.h"
#include "newui/items.h"

namespace newui {

    // A row that only labels the rows after it: muted small text, no selection or hover chrome. Painted from the Model's text for its index.
    class HeadingListItem : public ListItem {
    public:
        HeadingListItem() = default;
        void paint(BLContext& ctx, const Rect& rect, std::size_t index, ListController& controller) override;
    };

    // A ListController for a list whose Model has heading rows among the ordinary ones (the Model's text for a heading row is its title).
    // Headings are shorter than ordinary rows (headingHeight()) and are painted by HeadingListItem. ListView itself does not know they are
    // special, so a heading can still be clicked: the owner of the ListView answers onSelectionChanged by calling nextSelectable() and
    // selecting what it returns instead. Ordinary rows are made like a plain ListController's (setDefaultItemClassName(), so a custom row item is a
    // reflected ListItem subclass named there); headings are HeadingListItems, kept in a pool of their own because the base pool holds one class.
    //
    //   auto controller = std::make_shared<GroupedListController>();
    //   controller->setHeadings({true, false, false, true, false});   // one flag per Model row
    //   listView->setController(controller);
    //   listView->setModel(std::move(model));
    // @reflect ignore=true
    class GroupedListController : public ListController {
    public:
        void setHeadings(std::vector<bool> isHeading) { isHeading_ = std::move(isHeading); }
        bool isHeading(std::size_t index) const { return index < isHeading_.size() && isHeading_[index]; }

        float headingHeight() const { return headingHeight_; }
        void setHeadingHeight(float height) { headingHeight_ = height; }

        // The row to select instead of `index` when `index` is a heading: the first ordinary row from there going away from `from` (the row that
        // was selected before), or `from` itself when there is none. An ordinary `index` is returned unchanged.
        std::size_t nextSelectable(std::size_t index, std::size_t from) const;

        ListItem* createItem(std::size_t index) override;
        void releaseItem(Item* item) override;
        float itemHeight(std::size_t index) const override;

    private:
        std::vector<bool> isHeading_;
        std::vector<std::unique_ptr<HeadingListItem>> headingPool_;
        float headingHeight_ = 26.0f;
    };

}

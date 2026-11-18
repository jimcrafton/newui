#include "newui/groupedlist.h"

#include <any>
#include <string>

#include "newui/fontmanager.h"

namespace newui {

    namespace {
        constexpr float kHeadingFontSize = 10.0f;
        const BLRgba32 kHeadingColor(0xFFA0A0A0u);
    }

    void HeadingListItem::paint(BLContext& ctx, const Rect& rect, std::size_t index, ListController& controller) {
        // No Item::paint: a heading has no background, selection or hover look of its own.
        Model* model = controller.model();
        const std::any value = model != nullptr ? model->value(index) : std::any();
        const std::string* title = std::any_cast<std::string>(&value);
        if (title == nullptr || title->empty()) {
            return;
        }
        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        BLFont* base = font.blFont();
        if (base == nullptr || !base->is_valid()) {
            return;
        }
        BLFont headingFont = *base;   // not named "small": rpcndr.h defines that as a macro
        headingFont.set_size(kHeadingFontSize);
        const BLFontMetrics& metrics = headingFont.metrics();
        const double y = rect.top() + rect.size().height - 5.0 - metrics.descent;
        ctx.save();
        ctx.set_fill_style(kHeadingColor);
        ctx.fill_utf8_text(BLPoint(rect.left() + 8.0, y), headingFont, title->c_str(), title->size());
        ctx.restore();
    }

    std::size_t GroupedListController::nextSelectable(std::size_t index, std::size_t from) const {
        const std::size_t count = itemCount();
        const bool forward = index >= from;
        std::size_t at = index;
        while (at < count && isHeading(at)) {
            if (forward) {
                ++at;
            } else if (at == 0) {
                return from;
            } else {
                --at;
            }
        }
        return at < count ? at : from;
    }

    ListItem* GroupedListController::createItem(std::size_t index) {
        if (!isHeading(index)) {
            return ListController::createItem(index);
        }
        if (!headingPool_.empty()) {
            ListItem* item = headingPool_.back().release();
            headingPool_.pop_back();
            return item;
        }
        return static_cast<ListItem*>(instantiateItem("HeadingListItem"));
    }

    void GroupedListController::releaseItem(Item* item) {
        if (auto* heading = dynamic_cast<HeadingListItem*>(item)) {
            headingPool_.emplace_back(heading);
            return;
        }
        ListController::releaseItem(item);
    }

    float GroupedListController::itemHeight(std::size_t index) const {
        return isHeading(index) ? headingHeight_ : defaultItemHeight();
    }

}

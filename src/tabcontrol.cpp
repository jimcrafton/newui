#include "newui/tabcontrol.h"
#include "newui/layout.h"
#include "newui/fontmanager.h"
#include "newui/font.h"

#include <blend2d/blend2d.h>

// Internal to TabControl - not declared in tabcontrol.h, same "keep
// implementation machinery out of the public header" convention
// cursor.cpp/menus.cpp already use.
namespace {

constexpr float kHorizontalStripThickness = 28.0f;  // strip height when the strip is a horizontal row (Top/Bottom)
constexpr float kVerticalStripThickness = 100.0f;    // strip width when the strip is a vertical column (Left/Right)
constexpr float kButtonMainAxisPadding = 24.0f;      // extra width/height beyond the raw text extent

class TabItemButtonView : public newui::SubView {
public:
    std::string text;

    // Non-owning - points into the owning TabControl's own tree, which
    // outlives this button (rebuilt/destroyed together in
    // TabControl::addTab()/its own destroy() cascade).
    newui::TabControl* owner = nullptr;
    std::size_t tabIndex = 0;

    void paint(BLContext& ctx) override {
        newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr) {
            return;
        }

        newui::TextMetrics tm = font.measureText(text);

        newui::Rect bounds = getClientBounds();
        float x = bounds.left() + (bounds.size().width - tm.width) * 0.5f;
        float textHeight = tm.ascent + tm.descent;
        float baselineY = bounds.top() + (bounds.size().height - textHeight) * 0.5f + tm.ascent;

        COLORREF textColor = ::GetSysColor(COLOR_WINDOWTEXT);
        ctx.set_fill_style(BLRgba32(GetRValue(textColor), GetGValue(textColor), GetBValue(textColor), 255));
        ctx.fill_utf8_text(BLPoint(x, baselineY), *blFont, text.c_str());
    }
};

// Needs no live window at all (unlike MenuBar's button click handler) -
// selecting a tab just switches which already-built page is visible via
// CardLayout, entirely in-process.
newui::SyncReturn TabItemButtonClicked(newui::View& sender, const newui::Point&, std::uint32_t, std::uint32_t) {
    auto& button = static_cast<TabItemButtonView&>(sender);
    if (button.owner != nullptr) {
        button.owner->selectTab(button.tabIndex);
    }
    return newui::SyncReturn::Handled;
}

bool IsHorizontalStrip(newui::ThemedTabItemStyle::TabAlignment alignment) {
    return alignment == newui::ThemedTabItemStyle::TabAlignment::Top
        || alignment == newui::ThemedTabItemStyle::TabAlignment::Bottom;
}

}  // namespace

namespace newui {

TabControl::TabControl(ThemedTabItemStyle::TabAlignment alignment) : alignment_(alignment) {
    setName("TabControl");
    setVisible(true);
    setStyle(std::make_unique<ThemedTabPaneStyle>());

    const bool stripHorizontal = IsHorizontalStrip(alignment_);

    // Top/Bottom: strip above/below the pages, stacked vertically.
    // Left/Right: strip beside the pages, side by side horizontally.
    setLayout(std::make_unique<FlexLayout>(stripHorizontal ? Orientation::Vertical : Orientation::Horizontal));

    stripRow_ = new SubView();
    stripRow_->setName("TabControlStrip");
    stripRow_->setVisible(true);
    stripRow_->setLayout(std::make_unique<FlexLayout>(stripHorizontal ? Orientation::Horizontal : Orientation::Vertical));
    stripRow_->setLayoutParams(std::make_unique<FlexLayoutParams>(0.0f));
    stripRow_->setDesiredSize(stripHorizontal
        ? Size(0.0f, kHorizontalStripThickness)
        : Size(kVerticalStripThickness, 0.0f));

    pagesArea_ = new SubView();
    pagesArea_->setName("TabControlPages");
    pagesArea_->setVisible(true);
    pagesArea_->setLayout(std::make_unique<CardLayout>());
    pagesArea_->setLayoutParams(std::make_unique<FlexLayoutParams>(1.0f));

    // Top/Left: strip comes first (visually above/left of the pages).
    // Bottom/Right: pages come first.
    const bool stripFirst = (alignment_ == ThemedTabItemStyle::TabAlignment::Top
        || alignment_ == ThemedTabItemStyle::TabAlignment::Left);
    if (stripFirst) {
        addChild(stripRow_);
        addChild(pagesArea_);
    } else {
        addChild(pagesArea_);
        addChild(stripRow_);
    }
}

SubView* TabControl::addTab(const std::string& text, SubView* page) {
    const bool stripHorizontal = IsHorizontalStrip(alignment_);
    const bool wasEmpty = stripRow_->childViews().empty();

    auto* button = new TabItemButtonView();
    button->setName(text);
    button->setVisible(true);
    button->text = text;
    button->owner = this;
    button->tabIndex = stripRow_->childViews().size();
    button->setStyle(std::make_unique<ThemedTabItemStyle>());
    static_cast<ThemedTabItemStyle&>(button->style()).alignment = alignment_;

    Font font = FontManager::getSystemFont(SystemUIFont::Message);
    TextMetrics tm = font.measureText(text);
    button->setDesiredSize(stripHorizontal
        ? Size(tm.width + kButtonMainAxisPadding, kHorizontalStripThickness)
        : Size(kVerticalStripThickness, tm.ascent + tm.descent + kButtonMainAxisPadding * 0.5f));

    button->onMouseDown.add(&TabItemButtonClicked);

    stripRow_->addChild(button);
    pagesArea_->addChild(page);

    updateTabPositions();

    if (wasEmpty) {
        selectTab(0);
    }

    return page;
}

std::size_t TabControl::tabCount() const {
    return stripRow_->childViews().size();
}

SubView* TabControl::tabButton(std::size_t index) const {
    const auto& buttons = stripRow_->childViews();
    return index < buttons.size() ? buttons[index] : nullptr;
}

SubView* TabControl::page(std::size_t index) const {
    const auto& pages = pagesArea_->childViews();
    return index < pages.size() ? pages[index] : nullptr;
}

void TabControl::selectTab(std::size_t index) {
    const auto& buttons = stripRow_->childViews();
    if (index >= buttons.size()) {
        return;
    }

    for (std::size_t i = 0; i < buttons.size(); ++i) {
        auto* style = dynamic_cast<ThemedTabItemStyle*>(&buttons[i]->style());
        if (style == nullptr) {
            continue;
        }
        style->selected = (i == index);
        buttons[i]->style().markDirty();
    }

    selectedIndex_ = index;

    auto* cardLayout = dynamic_cast<CardLayout*>(pagesArea_->layout());
    if (cardLayout != nullptr) {
        cardLayout->show(index);
    }

    onTabChanged.syncCall(*this, index);
}

void TabControl::updateTabPositions() {
    const auto& buttons = stripRow_->childViews();
    const std::size_t count = buttons.size();

    for (std::size_t i = 0; i < count; ++i) {
        auto* style = dynamic_cast<ThemedTabItemStyle*>(&buttons[i]->style());
        if (style == nullptr) {
            continue;
        }

        if (count == 1) {
            style->position = ThemedTabItemStyle::Position::Only;
        } else if (i == 0) {
            style->position = ThemedTabItemStyle::Position::Left;
        } else if (i == count - 1) {
            style->position = ThemedTabItemStyle::Position::Right;
        } else {
            style->position = ThemedTabItemStyle::Position::Middle;
        }
    }
}

}  // namespace newui

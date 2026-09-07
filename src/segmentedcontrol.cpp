#include "newui/segmentedcontrol.h"
#include "newui/fontmanager.h"
#include "newui/uicolormanager.h"

namespace newui {

namespace {
    constexpr double kHorizontalPadding = 12.0;
    constexpr float kHeight = 24.0f;
    constexpr double kCornerRadius = 5.0;

    double measureTextWidth(BLFont& font, const std::string& text) {
        if (text.empty()) {
            return 0.0;
        }
        BLGlyphBuffer glyphBuffer;
        glyphBuffer.set_utf8_text(text.c_str(), text.size());
        font.shape(glyphBuffer);
        BLTextMetrics textMetrics;
        font.get_text_metrics(glyphBuffer, textMetrics);
        return textMetrics.advance.x;
    }
}

SegmentedControl::SegmentedControl() {
    setVisible(true);
    onMouseDown.add(this, &SegmentedControl::handleMouseDown);
}

void SegmentedControl::setSegments(std::vector<std::string> labels) {
    labels_ = std::move(labels);
    enabled_.assign(labels_.size(), true);
    selectedIndex_ = 0;
    redraw();
}

void SegmentedControl::setSelectedIndex(std::size_t index) {
    if (index == selectedIndex_ || index >= labels_.size() || !isSegmentEnabled(index)) {
        return;
    }
    selectedIndex_ = index;
    redraw();
    onSelectionChanged(*this);
}

bool SegmentedControl::isSegmentEnabled(std::size_t index) const {
    return index < enabled_.size() && enabled_[index];
}

void SegmentedControl::setSegmentEnabled(std::size_t index, bool enabled) {
    if (index >= enabled_.size() || enabled_[index] == enabled) {
        return;
    }
    enabled_[index] = enabled;
    redraw();
}

float SegmentedControl::segmentWidth(std::size_t index) const {
    if (index >= labels_.size()) {
        return 0.0f;
    }
    Font font = FontManager::getSystemFont(SystemUIFont::Message);
    BLFont* blFont = font.blFont();
    if (blFont == nullptr || !blFont->is_valid()) {
        return 0.0f;
    }
    return static_cast<float>(measureTextWidth(*blFont, labels_[index]) + kHorizontalPadding * 2.0);
}

float SegmentedControl::segmentLeft(std::size_t index) const {
    float left = 0.0f;
    for (std::size_t i = 0; i < index && i < labels_.size(); ++i) {
        left += segmentWidth(i);
    }
    return left;
}

Size SegmentedControl::naturalSize() const {
    float width = 0.0f;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        width += segmentWidth(i);
    }
    return Size(width, kHeight);
}

std::optional<std::size_t> SegmentedControl::segmentAt(const Point& localPt) const {
    float left = 0.0f;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        float width = segmentWidth(i);
        if (localPt.x >= left && localPt.x < left + width) {
            return i;
        }
        left += width;
    }
    return std::nullopt;
}

SyncReturn SegmentedControl::handleMouseDown(View& /*sender*/, const Point& pt,
    std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
    std::optional<std::size_t> index = segmentAt(pt);
    if (!index.has_value()) {
        return SyncReturn::Ignored;
    }
    setSelectedIndex(*index);
    return SyncReturn::Handled;
}

void SegmentedControl::paint(BLContext& ctx) {
    Rect bounds = getClientBounds();
    if (bounds.size().width <= 0.0f || bounds.size().height <= 0.0f || labels_.empty()) {
        return;
    }

    Font uiFont = FontManager::getSystemFont(SystemUIFont::Message);
    BLFont* blFont = uiFont.blFont();
    if (blFont == nullptr || !blFont->is_valid()) {
        return;
    }

    BLRgba32 borderColor = UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32();
    BLRgba32 accentColor = UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32();
    BLRgba32 accentText = UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32();
    BLRgba32 normalText = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
    BLRgba32 disabledText = UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32();

    double totalWidth = 0.0;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        totalWidth += segmentWidth(i);
    }

    const BLFontMetrics& fontMetrics = blFont->metrics();
    double textHeight = fontMetrics.ascent + fontMetrics.descent;

    for (std::size_t i = 0; i < labels_.size(); ++i) {
        double left = bounds.left() + segmentLeft(i);
        double width = segmentWidth(i);
        bool isSelected = (i == selectedIndex_) && isSegmentEnabled(i);

        if (isSelected) {
            ctx.save();
            ctx.set_fill_style(accentColor);
            ctx.fill_rect(BLRect(left, bounds.top(), width, bounds.size().height));
            ctx.restore();
        }

        if (i > 0) {
            ctx.save();
            ctx.set_stroke_style(borderColor);
            ctx.set_stroke_width(1.0);
            ctx.stroke_line(BLPoint(left, bounds.top()), BLPoint(left, bounds.top() + bounds.size().height));
            ctx.restore();
        }

        double textWidth = measureTextWidth(*blFont, labels_[i]);
        double x = left + (width - textWidth) * 0.5;
        double y = bounds.top() + (double(bounds.size().height) - textHeight) * 0.5 + fontMetrics.ascent;

        BLRgba32 textColor = !isSegmentEnabled(i) ? disabledText
            : isSelected ? accentText
            : normalText;

        ctx.save();
        ctx.set_fill_style(textColor);
        ctx.fill_utf8_text(BLPoint(x, y), *blFont, labels_[i].c_str(), labels_[i].size());
        ctx.restore();
    }

    ctx.save();
    ctx.set_stroke_style(borderColor);
    ctx.set_stroke_width(1.0);
    ctx.stroke_round_rect(BLRoundRect(bounds.left(), bounds.top(), totalWidth, bounds.size().height, kCornerRadius));
    ctx.restore();
}

}

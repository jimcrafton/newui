#include "newui/frameproxy.h"
#include "newui/uicolormanager.h"

namespace newui {

FrameProxy::FrameProxy() {
    titleFont_ = FontManager::getSystemFont(SystemUIFont::Caption);
    style().setBackgroundColor(UIColorManager::colorFor(UIColorRole::ControlBackground));
}

void FrameProxy::paint(BLContext& ctx) {
    Rect clientBounds = getClientBounds();
    if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
        return;
    }

    Rect barBounds(clientBounds.left(), clientBounds.top(), clientBounds.size().width, kTitleBarHeight);

    ctx.save();
    ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32());
    ctx.fill_rect(BLRect(barBounds));
    ctx.restore();

    if (title_.empty()) {
        return;
    }

    BLFont* blFont = titleFont_.blFont();
    if (blFont == nullptr || !blFont->is_valid()) {
        return;
    }

    const BLFontMetrics& fontMetrics = blFont->metrics();
    double x = barBounds.left() + 8.0;
    double y = barBounds.top() + (kTitleBarHeight - (fontMetrics.ascent + fontMetrics.descent)) * 0.5 + fontMetrics.ascent;

    ctx.save();
    ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32());
    ctx.fill_utf8_text(BLPoint(x, y), *blFont, title_.c_str(), title_.size());
    ctx.restore();
}

}

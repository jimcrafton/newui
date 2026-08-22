#include "newui/items.h"

#include <any>
#include <string>
#include <utility>

#include "newui/controllers.h"
#include "newui/fontmanager.h"
#include "newui/uicolormanager.h"

namespace newui {

    namespace {
        // Shared by ListItem/TreeItem/TableItem::paint() below - draws
        // text left-aligned, vertically centered in rect, same BLFont/
        // glyph-buffer/fill_utf8_text idiom LabelStyle::paint() already
        // uses (viewstyle.h) for a Control's own label text.
        void paintItemText(BLContext& ctx, const Rect& rect, const std::string& text) {
            if (text.empty() || rect.size().width <= 0.0f || rect.size().height <= 0.0f) {
                return;
            }

            Font font = FontManager::getSystemFont(SystemUIFont::Message);
            BLFont* blFont = font.blFont();
            if (blFont == nullptr || !blFont->is_valid()) {
                return;
            }

            BLGlyphBuffer glyphBuffer;
            glyphBuffer.set_utf8_text(text.c_str(), text.size());
            blFont->shape(glyphBuffer);

            const BLFontMetrics& fontMetrics = blFont->metrics();
            double textHeight = fontMetrics.ascent + fontMetrics.descent;

            double x = rect.left();
            double y = rect.top() + (rect.size().height - textHeight) * 0.5 + fontMetrics.ascent;

            ctx.save();
            ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32());
            ctx.fill_utf8_text(BLPoint(x, y), *blFont, text.c_str(), text.size());
            ctx.restore();
        }

        // Best-effort std::any -> std::string for whatever Model::value()
        // handed back - a concrete Model is free to store its data however
        // it likes; this is just the one shape Item's own MVP paint()
        // knows how to render.
        std::string valueToString(const std::any& value) {
            if (!value.has_value()) {
                return std::string();
            }
            if (const std::string* s = std::any_cast<std::string>(&value)) {
                return *s;
            }
            return std::string();
        }
    }

    Item::Item() : style_(std::make_unique<ViewStyle>()) {
    }

    void Item::paint(BLContext& ctx, const Rect& rect) {
        if (rect.size().width <= 0.0f || rect.size().height <= 0.0f) {
            clientBounds_ = rect;
            return;
        }

        Rect localClientBounds;
        ctx.save();
        ctx.translate(rect.left(), rect.top());
        style_->paint(ctx, rect.size(), highlighted_, localClientBounds);
        ctx.restore();

        clientBounds_ = Rect(rect.left() + localClientBounds.left(), rect.top() + localClientBounds.top(),
                              localClientBounds.size().width, localClientBounds.size().height);
    }

    void ListItem::paint(BLContext& ctx, const Rect& rect, std::size_t index, ItemController& controller) {
        Item::paint(ctx, rect);
        Model* model = controller.model();
        std::any value = model != nullptr ? model->value(index) : std::any();
        paintItemText(ctx, clientBounds(), valueToString(value));
    }

    void TreeItem::paint(BLContext& ctx, const Rect& rect, const std::vector<std::size_t>& path, ItemController& controller) {
        Item::paint(ctx, rect);
        Model* model = controller.model();
        std::any value = model != nullptr ? model->value(path) : std::any();
        paintItemText(ctx, clientBounds(), valueToString(value));
    }

    void TableItem::paint(BLContext& ctx, const Rect& rect, std::size_t row, std::size_t col, ItemController& controller) {
        Item::paint(ctx, rect);
        Model* model = controller.model();
        std::any value = model != nullptr ? model->value(std::make_pair(row, col)) : std::any();
        paintItemText(ctx, clientBounds(), valueToString(value));
    }

}

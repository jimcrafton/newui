#include "cellitem.h"

#include "manyrowsmodel.h"

#include <newui/controllers.h>
#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <cstdlib>
#include <string>

namespace manyrows {

    using namespace newui;

    namespace {
        constexpr float kPadding = 8.0f;     // space between a cell edge and its text
        constexpr float kDotSize = 8.0f;     // status dot diameter
        constexpr float kDotGap = 6.0f;      // space between the dot and the status text

        BLRgba32 rgb(unsigned r, unsigned g, unsigned b) {
            return BLRgba32(r, g, b, 255);
        }

        BLRgba32 statusColor(const std::string& status) {
            if (status == "Pending") return rgb(0xB7, 0x79, 0x1F);
            if (status == "Shipped") return rgb(0x2B, 0x6C, 0xB0);
            if (status == "Delivered") return rgb(0x2F, 0x85, 0x5A);
            if (status == "Returned") return rgb(0xC0, 0x56, 0x21);
            if (status == "Cancelled") return rgb(0xC5, 0x30, 0x30);
            return rgb(0x60, 0x60, 0x60);
        }

        BLRgba32 ratingColor(const std::string& text) {
            double rating = std::atof(text.c_str());
            if (rating < 2.5) return rgb(0xC5, 0x30, 0x30);
            if (rating < 3.8) return rgb(0xB7, 0x79, 0x1F);
            return rgb(0x2F, 0x85, 0x5A);
        }
    }

    void CellItem::paint(BLContext& ctx, const Rect& rect, std::size_t row, std::size_t col, TableController& controller) {
        // Selection / hover chrome; also records clientBounds().
        Item::paint(ctx, rect);

        Model* model = controller.model();
        if (model == nullptr || rect.width() <= 0.0f || rect.height() <= 0.0f) {
            return;
        }
        const std::any value = model->value(std::make_pair(row, col));
        const std::string* text = std::any_cast<std::string>(&value);
        if (text == nullptr || text->empty()) {
            return;
        }

        Font font = FontManager::getSystemFont(SystemUIFont::Message);
        BLFont* blFont = font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }
        BLGlyphBuffer glyphs;
        glyphs.set_utf8_text(text->c_str(), text->size());
        blFont->shape(glyphs);
        BLTextMetrics metrics;
        blFont->get_text_metrics(glyphs, metrics);
        double textWidth = metrics.advance.x;

        const BLFontMetrics& fm = blFont->metrics();
        double baseline = rect.top() + (rect.height() - (fm.ascent + fm.descent)) * 0.5 + fm.ascent;
        double centerY = rect.top() + rect.height() * 0.5;
        double left = rect.left() + kPadding;
        double right = rect.left() + rect.width() - kPadding;

        BLRgba32 color = UIColorManager::colorFor(!isEnabled() ? UIColorRole::DisabledText
            : isSelected() ? UIColorRole::HighlightText : UIColorRole::ControlText).toBLRgba32();
        bool plain = isSelected() || !isEnabled();

        // Loaded files get inferred column types and none of the generated schema's special columns.
        const auto* many = dynamic_cast<const ManyRowsModel*>(model);
        const bool generated = many != nullptr && many->isGenerated();
        ManyRowsModel::ColumnType type = many != nullptr ? many->columnTypeOf(col) : ManyRowsModel::ColumnType::Text;
        double x = left;
        if (type == ManyRowsModel::ColumnType::Integer || type == ManyRowsModel::ColumnType::Number) {
            x = right - textWidth;
        } else if (type == ManyRowsModel::ColumnType::Date) {
            x = rect.left() + (rect.width() - textWidth) * 0.5;
        }

        if (!plain) {
            if (generated && col == ManyRowsModel::kOrderColumn) {
                color = rgb(0x80, 0x80, 0x80);
            } else if (generated && col == ManyRowsModel::kRatingColumn) {
                color = ratingColor(*text);
            }
        }

        if (generated && col == ManyRowsModel::kStatusColumn) {
            BLRgba32 dot = plain ? color : statusColor(*text);
            if (!plain) {
                color = dot;
            }
            ctx.save();
            ctx.set_fill_style(dot);
            ctx.fill_circle(left + kDotSize * 0.5, centerY, kDotSize * 0.5);
            ctx.restore();
            x = left + kDotSize + kDotGap;
        }

        if (x < rect.left()) {
            x = rect.left();   // too narrow to right-align: clip at the left edge instead of spilling out
        }
        ctx.save();
        ctx.clip_to_rect(BLRect(rect.left(), rect.top(), rect.width(), rect.height()));
        ctx.set_fill_style(color);
        ctx.fill_utf8_text(BLPoint(x, baseline), *blFont, text->c_str(), text->size());
        ctx.restore();

        // A small triangle in the top-right corner marks a cell the user has edited.
        if (!isSelected()) {
            if (many != nullptr && many->isEdited(row, col)) {
                double rightEdge = rect.left() + rect.width();
                BLPath mark;
                mark.move_to(rightEdge - 7.0, rect.top() + 1.0);
                mark.line_to(rightEdge - 1.0, rect.top() + 1.0);
                mark.line_to(rightEdge - 1.0, rect.top() + 7.0);
                mark.close();
                ctx.save();
                ctx.set_fill_style(rgb(0x2B, 0x6C, 0xB0));
                ctx.fill_path(mark);
                ctx.restore();
            }
        }
    }

}

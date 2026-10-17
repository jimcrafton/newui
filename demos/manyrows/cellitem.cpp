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

        // What every cell of one paint pass shares. The system font and the theme colors are not cached by
        // newui (they can change at runtime) and cost microseconds each to look up - more than painting a
        // cell - so they are resolved once per pass instead of once per cell. See CellItem::beginPass().
        struct Pass {
            const Model* model = nullptr;
            const ManyRowsModel* many = nullptr;   // model as a ManyRowsModel, null for any other model
            bool generated = false;
            Font font;
            BLRgba32 text;
            BLRgba32 highlightText;
            BLRgba32 disabledText;

            void build(const Model* forModel) {
                model = forModel;
                many = dynamic_cast<const ManyRowsModel*>(forModel);
                generated = many != nullptr && many->isGenerated();
                font = FontManager::getSystemFont(SystemUIFont::Message);
                text = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
                highlightText = UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32();
                disabledText = UIColorManager::colorFor(UIColorRole::DisabledText).toBLRgba32();
            }
        };

        Pass sharedPass;
        bool sharedPassValid = false;
    }

    void CellItem::beginPass(const Model* model) {
        sharedPass.build(model);
        sharedPassValid = true;
    }

    void CellItem::endPass() {
        sharedPassValid = false;
    }

    void CellItem::paint(BLContext& ctx, const Rect& rect, std::size_t row, std::size_t col, TableController& controller) {
        // Selection / hover chrome; also records clientBounds().
        Item::paint(ctx, rect);

        Model* model = controller.model();
        if (model == nullptr || rect.width() <= 0.0f || rect.height() <= 0.0f) {
            return;
        }
        // Inside a pass (TimedTableView) everything is shared; anywhere else it is resolved for this one cell.
        Pass local;
        const Pass* pass = &sharedPass;
        if (!sharedPassValid || sharedPass.model != model) {
            local.build(model);
            pass = &local;
        }

        std::string text;
        if (pass->many != nullptr) {
            text = pass->many->cellText(row, col);   // no std::any box
        } else {
            const std::any value = model->value(std::make_pair(row, col));
            if (const std::string* asString = std::any_cast<std::string>(&value)) {
                text = *asString;
            }
        }
        if (text.empty()) {
            return;
        }

        BLFont* blFont = pass->font.blFont();
        if (blFont == nullptr || !blFont->is_valid()) {
            return;
        }
        // Shaped once: the width places the text, and the same glyphs are drawn.
        BLGlyphBuffer glyphs;
        glyphs.set_utf8_text(text.c_str(), text.size());
        blFont->shape(glyphs);
        BLTextMetrics metrics;
        blFont->get_text_metrics(glyphs, metrics);
        double textWidth = metrics.advance.x;

        const BLFontMetrics& fm = blFont->metrics();
        double baseline = rect.top() + (rect.height() - (fm.ascent + fm.descent)) * 0.5 + fm.ascent;
        double centerY = rect.top() + rect.height() * 0.5;
        double left = rect.left() + kPadding;
        double right = rect.left() + rect.width() - kPadding;

        BLRgba32 color = !isEnabled() ? pass->disabledText : isSelected() ? pass->highlightText : pass->text;
        bool plain = isSelected() || !isEnabled();

        // Loaded files get inferred column types and none of the generated schema's special columns.
        const bool generated = pass->generated;
        ManyRowsModel::ColumnType type = pass->many != nullptr ? pass->many->columnTypeOf(col) : ManyRowsModel::ColumnType::Text;
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
                color = ratingColor(text);
            }
        }

        if (generated && col == ManyRowsModel::kStatusColumn) {
            BLRgba32 dot = plain ? color : statusColor(text);
            if (!plain) {
                color = dot;
            }
            ctx.fill_circle(BLCircle(left + kDotSize * 0.5, centerY, kDotSize * 0.5), dot);
            x = left + kDotSize + kDotGap;
        }

        if (x < rect.left()) {
            x = rect.left();   // too narrow to right-align: clip at the left edge instead of spilling out
        }
        if (x + textWidth > rect.left() + rect.width()) {
            // Only text that really overflows its cell needs a clip (a save / clip / restore is the dearest
            // part of painting a cell).
            ctx.save();
            ctx.clip_to_rect(BLRect(rect.left(), rect.top(), rect.width(), rect.height()));
            ctx.fill_glyph_run(BLPoint(x, baseline), *blFont, glyphs.glyph_run(), color);
            ctx.restore();
        } else {
            ctx.fill_glyph_run(BLPoint(x, baseline), *blFont, glyphs.glyph_run(), color);
        }

        // A small triangle in the top-right corner marks a cell the user has edited.
        if (!isSelected() && pass->many != nullptr && pass->many->isEdited(row, col)) {
            double rightEdge = rect.left() + rect.width();
            BLPath mark;
            mark.move_to(rightEdge - 7.0, rect.top() + 1.0);
            mark.line_to(rightEdge - 1.0, rect.top() + 1.0);
            mark.line_to(rightEdge - 1.0, rect.top() + 7.0);
            mark.close();
            ctx.fill_path(mark, rgb(0x2B, 0x6C, 0xB0));
        }
    }

}

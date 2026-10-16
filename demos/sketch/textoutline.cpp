#include "textoutline.h"

#include <newui/font.h>
#include <newui/fontmanager.h>

#include <algorithm>

namespace sketch {

    namespace {
        // The BLFont for the named font and size; null when it cannot be resolved.
        BLFont* fontFor(const std::string& fontName, double fontSize, newui::Font& holder) {
            holder = newui::Font(fontName.empty() ? defaultTextFontName() : fontName, float(std::max(1.0, fontSize)));
            BLFont* font = holder.blFont();
            return font != nullptr && font->is_valid() ? font : nullptr;
        }
    }

    std::string defaultTextFontName() {
        return newui::FontManager::getSystemFont(newui::SystemUIFont::Message).name();
    }

    double textLineHeight(const std::string& fontName, double fontSize) {
        newui::Font holder;
        BLFont* font = fontFor(fontName, fontSize, holder);
        if (font == nullptr) {
            return fontSize * 1.2;
        }
        const BLFontMetrics& m = font->metrics();
        return m.ascent + m.descent + m.line_gap;
    }

    double textAdvance(const std::string& line, const std::string& fontName, double fontSize) {
        newui::Font holder;
        BLFont* font = fontFor(fontName, fontSize, holder);
        if (font == nullptr || line.empty()) {
            return 0.0;
        }
        BLGlyphBuffer glyphs;
        glyphs.set_utf8_text(line.c_str(), line.size());
        font->shape(glyphs);
        BLTextMetrics metrics;
        font->get_text_metrics(glyphs, metrics);
        return metrics.advance.x;
    }

    BLPath textOutline(const std::string& text, const std::string& fontName, double fontSize) {
        BLPath result;
        newui::Font holder;
        BLFont* font = fontFor(fontName, fontSize, holder);
        if (font == nullptr) {
            return result;
        }
        const double lineHeight = textLineHeight(fontName, fontSize);
        std::size_t start = 0;
        for (int line = 0;; ++line) {
            std::size_t end = text.find('\n', start);
            std::string piece = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
            if (!piece.empty()) {
                BLGlyphBuffer glyphs;
                glyphs.set_utf8_text(piece.c_str(), piece.size());
                font->shape(glyphs);
                BLPath one;
                font->get_glyph_run_outlines(glyphs.glyph_run(), one);
                one.transform(BLMatrix2D::make_translation(0.0, line * lineHeight));
                result.add_path(one);
            }
            if (end == std::string::npos) {
                break;
            }
            start = end + 1;
        }
        return result;
    }

}

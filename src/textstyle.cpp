#include "newui/textstyle.h"

#include <algorithm>
#include <unordered_map>

namespace newui {

    TextStyleSheet::~TextStyleSheet() {
        for (TextStyle* style : styles_) {
            delete style;
        }
    }

    void TextStyleSheet::addStyle(TextStyle* style) {
        if (style != nullptr && std::find(styles_.begin(), styles_.end(), style) == styles_.end()) {
            styles_.push_back(style);
        }
    }

    void TextStyleSheet::removeStyle(TextStyle* style) {
        auto it = std::find(styles_.begin(), styles_.end(), style);
        if (it != styles_.end()) {
            styles_.erase(it);
        }
    }

    TextStyle* TextStyleSheet::style(const std::string& name) const {
        for (TextStyle* style : styles_) {
            if (style->name() == name) {
                return style;
            }
        }
        return nullptr;
    }

    namespace text {

        ExpandedTextStyles expandTextStyles(const TextStyleSheet& sheet, const std::vector<TextStyleRange>& ranges) {
            ExpandedTextStyles out;
            out.colorRuns.reserve(ranges.size());
            // A document has thousands of ranges but a handful of style names - look each up once.
            std::unordered_map<std::string, const TextStyle*> resolved;
            for (const TextStyleRange& range : ranges) {
                auto found = resolved.find(range.style);
                if (found == resolved.end()) {
                    found = resolved.emplace(range.style, sheet.style(range.style)).first;
                }
                const TextStyle* style = found->second;
                if (style == nullptr || range.length == 0) {
                    continue;
                }
                if (!style->color().isNull()) {
                    out.colorRuns.push_back(TextColorRun{ range.start, range.length, style->color() });
                }
                if (style->isBold() || style->isItalic() || style->isUnderline() || style->isStrikethrough()
                    || !style->fontName().empty() || style->fontSize() > 0.0f) {
                    TextFontRun run;
                    run.start = range.start;
                    run.length = range.length;
                    run.bold = style->isBold();
                    run.italic = style->isItalic();
                    run.underline = style->isUnderline();
                    run.strikethrough = style->isStrikethrough();
                    run.fontName = style->fontName();
                    run.fontSize = style->fontSize();
                    out.fontRuns.push_back(run);
                }
                if (!style->backgroundColor().isNull()) {
                    TextDecoration background;
                    background.start = range.start;
                    background.length = range.length;
                    background.kind = TextDecorationKind::Background;
                    background.color = style->backgroundColor();
                    out.decorations.push_back(background);
                }
                if (style->decoration() != TextDecorationKind::None) {
                    TextDecoration decoration;
                    decoration.start = range.start;
                    decoration.length = range.length;
                    decoration.kind = style->decoration();
                    decoration.color = style->decorationColor();
                    out.decorations.push_back(decoration);
                }
            }
            return out;
        }
    }
}

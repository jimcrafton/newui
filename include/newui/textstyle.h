#pragma once

#include "newui/color.h"
#include "newui/component.h"
#include "newui/text.h"

#include <cstddef>
#include <string>
#include <vector>

namespace newui {

    // A reusable look for ranges of text - ViewStyle's counterpart for text, the way a CSS class
    // is. A TextControl's styled ranges name a style; the style says how those ranges look.
    // Every property that isn't set leaves the control's own value alone: an empty fontName, a
    // fontSize of 0 and a null color all mean "inherit". Its name (Component) is how ranges and
    // TextStyleSheet find it.
    class TextStyle : public Component {
    public:
        TextStyle() = default;
        explicit TextStyle(const std::string& name) { setName(name); }

        const std::string& fontName() const { return fontName_; }
        void setFontName(const std::string& fontName) { fontName_ = fontName; }

        float fontSize() const { return fontSize_; }
        void setFontSize(float fontSize) { fontSize_ = fontSize; }

        bool isBold() const { return bold_; }
        void setBold(bool bold) { bold_ = bold; }

        bool isItalic() const { return italic_; }
        void setItalic(bool italic) { italic_ = italic; }

        bool isUnderline() const { return underline_; }
        void setUnderline(bool underline) { underline_ = underline; }

        bool isStrikethrough() const { return strikethrough_; }
        void setStrikethrough(bool strikethrough) { strikethrough_ = strikethrough; }

        const Color& color() const { return color_; }
        void setColor(const Color& color) { color_ = color; }

        // Null: no fill behind the text.
        const Color& backgroundColor() const { return backgroundColor_; }
        void setBackgroundColor(const Color& color) { backgroundColor_ = color; }

        // None: no squiggle, box, ... (see text::TextDecoration).
        text::TextDecorationKind decoration() const { return decoration_; }
        void setDecoration(text::TextDecorationKind decoration) { decoration_ = decoration; }

        const Color& decorationColor() const { return decorationColor_; }
        void setDecorationColor(const Color& color) { decorationColor_ = color; }

    private:
        std::string fontName_;
        float fontSize_ = 0.0f;
        bool bold_ = false;
        bool italic_ = false;
        bool underline_ = false;
        bool strikethrough_ = false;
        Color color_ = Color::null();
        Color backgroundColor_ = Color::null();
        text::TextDecorationKind decoration_ = text::TextDecorationKind::None;
        Color decorationColor_ = Color::null();
    };

    // Named TextStyles - a theme, say ("keyword", "string", "comment", ...). Owns its styles the
    // way a View owns its children.
    class TextStyleSheet : public Component {
    public:
        TextStyleSheet() = default;
        ~TextStyleSheet() override;
        TextStyleSheet(const TextStyleSheet&) = delete;
        TextStyleSheet& operator=(const TextStyleSheet&) = delete;

        // @reflect collection add=addStyle remove=removeStyle
        const std::vector<TextStyle*>& styles() const { return styles_; }
        // Takes ownership.
        void addStyle(TextStyle* style);
        // Detaches without deleting - the caller owns style again.
        void removeStyle(TextStyle* style);

        // The style named name, or nullptr (the first, if several share it).
        TextStyle* style(const std::string& name) const;

    private:
        std::vector<TextStyle*> styles_;
    };

    namespace text {

        // A range of text and the name of the TextStyle it's drawn in.
        struct TextStyleRange {
            std::size_t start = 0;
            std::size_t length = 0;
            std::string style;
        };

        // What a TextControl draws for a set of styled ranges.
        struct ExpandedTextStyles {
            std::vector<TextColorRun> colorRuns;
            std::vector<TextFontRun> fontRuns;
            std::vector<TextDecoration> decorations;
        };

        // Resolves each range's style in sheet (ranges naming no style are skipped) into color runs,
        // font runs and decorations - a background color becomes a Background decoration.
        ExpandedTextStyles expandTextStyles(const TextStyleSheet& sheet, const std::vector<TextStyleRange>& ranges);
    }
}

#pragma once

#include <newui/controls.h>

namespace newui {

    // A TextController with outlining: regions of the text (text::TextFold) that collapse to an
    // inline placeholder, the way Visual Studio's editor folds a block to "{ ... }".
    //
    // Folds are kept in step with edits: a fold moves with text inserted before it, and grows or
    // shrinks with an edit inside it (which also expands it); an edit across its edge, or over
    // all of it, drops it. The caret never rests inside a collapsed fold - Left/Right step over
    // one, Backspace/Delete against one expand it instead of deleting hidden text, and clicking
    // its placeholder expands it.
    class TextFoldingController : public TextController {
    public:
        explicit TextFoldingController(Control& owner);

        // Sorted by start (outermost first where folds start together).
        //@reflect ignore=true
        const std::vector<text::TextFold>& folds() const { return folds_; }
        //@reflect ignore=true
        void setFolds(std::vector<text::TextFold> folds);
        void setFoldCollapsed(std::size_t index, bool collapsed);
        // Expands every collapsed fold hiding offset (strictly inside it). Whether any did.
        bool expandFoldsAt(std::size_t offset);

        // A box around each collapsed fold's placeholder within the visible height; ctx as for
        // drawSelection().
        void drawFoldPlaceholders(BLContext& ctx, float visibleHeight) const;

        // A margin left of the text: line numbers, and a [+]/[-] on each line a fold starts on -
        // clicking one collapses or expands it; clicking a line number selects that line.
        bool gutterVisible() const { return gutterVisible_; }
        void setGutterVisible(bool visible);
        float gutterWidth() const;
        // The client bounds less the gutter.
        Rect textArea() const override;
        // Draws the gutter, in the owner's local space.
        void drawGutter(BLContext& ctx) const;
        // The fold whose marker is on visual line (layoutEngine()'s), or kNoFold.
        static constexpr std::size_t kNoFold = static_cast<std::size_t>(-1);
        std::size_t markerFold(std::size_t line) const;

        SyncReturn handleMouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) override;
        SyncReturn handleKeyDown(std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) override;

    protected:
        const std::vector<text::TextFold>& layoutFolds() const override { return folds_; }
        void modelAttached(text::TextModel& model) override;

    private:
        SyncReturn handleModelAfterChar(text::TextModel& sender, size_t offset, wchar_t ch, text::CharChangeKind kind);
        SyncReturn handleModelAfterRangeChanged(text::TextModel& sender, const text::TextRange& range, const std::wstring& replacement);
        // Moves folds_ through an edit replacing removed characters at start with inserted ones.
        void adjustFolds(std::size_t start, std::size_t removed, std::size_t inserted);
        // The far edge of a collapsed fold starting (forward) or ending (back) at offset, or
        // offset itself if there's none.
        std::size_t stepOver(std::size_t offset, bool forward) const;
        void foldsChanged();
        void countTextLines();

        // x of the line numbers' right edge and the markers' column, in the owner's local space.
        struct GutterColumns {
            float numbersRight = 0.0f;
            float markerLeft = 0.0f;
        };
        GutterColumns gutterColumns() const;

        std::vector<text::TextFold> folds_;
        bool gutterVisible_ = true;
        std::size_t textLineCount_ = 1;   // sizes the line-number column
    };

    // A TextControl with outlining - see TextFoldingController. Starts in
    // FontManager::monospaceFont(), as a control for source code.
    // @reflect category=textinput
    class TextFoldingControl : public TextControl {
    public:
        TextFoldingControl();

        // Null only if setController() swapped in a controller without folding.
        TextFoldingController* foldingController() { return dynamic_cast<TextFoldingController*>(&controller()); }
        const TextFoldingController* foldingController() const { return dynamic_cast<const TextFoldingController*>(&controller()); }

        //@reflect ignore=true
        const std::vector<text::TextFold>& folds() const;
        //@reflect ignore=true
        void setFolds(std::vector<text::TextFold> folds);
        void setFoldCollapsed(std::size_t index, bool collapsed);

        // See TextFoldingController::setGutterVisible().
        bool gutterVisible() const;
        void setGutterVisible(bool visible);

        // TextControl's, then the placeholders' boxes and the gutter.
        void paint(BLContext& ctx) override;
    };

}

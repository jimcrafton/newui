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

        std::vector<text::TextFold> folds_;
    };

    // A TextControl with outlining - see TextFoldingController.
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

        // TextControl's, then the placeholders' boxes.
        void paint(BLContext& ctx) override;
    };

}

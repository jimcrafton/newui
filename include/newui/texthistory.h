#pragma once

#include <newui/text.h>

#include <cstddef>
#include <string>
#include <vector>

namespace newui::text {

    // A TextModel that remembers its edits so they can be undone and redone.
    //
    // A step is (the text before, the text after, where it changed): two PieceTree snapshots (O(1)
    // each, sharing everything the edit didn't touch) and the changed span, so history costs a few
    // words per step however large the text. Undo and redo put the text back with an ordinary
    // replace(), so every listener (the controller, the layout engine, folds, the highlighter)
    // sees them as the edits they are.
    //
    // Typing is coalesced: consecutive typed characters of the same kind (a run of letters, of
    // spaces, of punctuation) are one step, and so are runs of Backspace or of Delete, and
    // Backspace over what was just typed. Line breaks, pasted text and everything else that isn't
    // one character are steps of their own. breakCoalescing() (a caret move, a click, losing
    // focus) ends the current run. beginGroup()/endGroup() make everything in between one step.
    //
    // setText() and clear() replace the whole content and forget the history - what a load or a
    // reset wants. markClean() (on save) remembers the current state; isClean() says whether
    // undo/redo has brought it back to that state.
    //
    // UI thread only.
    //@reflect ignore=true
    class HistoryTextModel : public TextModel {
    public:
        HistoryTextModel() = default;
        explicit HistoryTextModel(const std::wstring& text) : TextModel(text) {}

        void setText(const std::wstring& text) override;
        void insert(size_t offset, const std::wstring& text) override;
        void remove(const TextRange& range) override;
        void replace(const TextRange& range, const std::wstring& replacement) override;
        void clear() override;

        bool canUndo() const override { return !undo_.empty() && groupDepth_ == 0; }
        bool canRedo() const override { return !redo_.empty() && groupDepth_ == 0; }
        // Both put back the step's text and return true (false: nothing to do, or a listener
        // vetoed it). *affected is the span the restored text now occupies.
        bool undo(TextRange* affected = nullptr) override;
        bool redo(TextRange* affected = nullptr) override;

        // The next edit starts a new step even if it could continue the last one.
        void breakCoalescing() { mergeable_ = false; }
        // Everything until the matching endGroup() is one step. Groups nest; only the outermost
        // counts.
        void beginGroup();
        void endGroup();

        void clearHistory();
        void markClean();
        bool isClean() const;

        std::size_t undoStepCount() const { return undo_.size(); }
        std::size_t redoStepCount() const { return redo_.size(); }
        // The oldest steps are dropped beyond this many (default 10000).
        void setMaxUndoSteps(std::size_t steps);

    private:
        // The text at start..start+inserted was start..start+removed in before.
        struct Step {
            PieceTree before;
            PieceTree after;
            std::size_t start = 0;
            std::size_t removed = 0;
            std::size_t inserted = 0;
        };
        struct Item {
            std::vector<Step> steps;
            wchar_t lastTyped = 0;   // for continuing a typing run
        };

        void record(const PieceTree& before, std::size_t start, std::size_t removed, const std::wstring& inserted);
        bool tryMerge(const PieceTree& before, const PieceTree& after, std::size_t start, std::size_t removed, const std::wstring& inserted);
        void push(Item item);
        bool apply(const Step& step, bool undoing);

        std::vector<Item> undo_;
        std::vector<Item> redo_;
        Item group_;
        std::size_t groupDepth_ = 0;
        bool mergeable_ = false;   // the top of undo_ can take the next edit
        bool replaying_ = false;
        std::size_t cleanDepth_ = 0;   // undo_.size() at the clean state; npos when it's gone
        std::size_t maxSteps_ = 10000;
        static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    };

}

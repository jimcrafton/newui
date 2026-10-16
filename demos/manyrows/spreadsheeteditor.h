#pragma once

#include "manyrowsmodel.h"

#include <newui/controls.h>
#include <newui/undostack.h>

#include <functional>
#include <optional>
#include <string>

namespace manyrows {

    // Edits to a ManyRowsModel with undo/redo: the one place that changes cells on the user's behalf
    // (typing in a cell or the formula bar, Delete on a range; clipboard/fill/sort join in later phases).
    //
    // Every edit is one undo step holding only the cells it touched (before/after overlay state - see
    // CellEdit), never a copy of the table, so a step costs memory in proportion to its size and a
    // million-row table stays cheap. Undo/redo report the bounding block of the cells they changed
    // through onReplayed, so the view can move the selection there (what a spreadsheet does).
    class SpreadsheetEditor {
    public:
        // Biggest block clearRange() will take in one step (an undo step holds a CellEdit per cell).
        static constexpr std::size_t kMaxRangeCells = 100000;

        explicit SpreadsheetEditor(ManyRowsModel& model) : model_(&model) {}

        // Checks text typed into a cell of `column` and normalizes it in place (trimmed; numbers and
        // statuses canonical). Returns "" when acceptable, else a message for the status bar. Empty text
        // is always acceptable (it blanks the cell).
        static std::string validate(std::size_t column, std::string& text);

        // Sets one cell, as one undo step titled "Edit C4". Returns false (nothing pushed) when the
        // text equals what the cell already shows. `text` must already have passed validate().
        bool setCell(std::size_t row, std::size_t column, const std::string& text);

        // Blanks every cell of the block as ONE undo step ("Clear C4:D6"). Returns false when the block
        // is larger than kMaxRangeCells (nothing happens) or when every cell is already blank.
        bool clearRange(const newui::CellRange& range);

        bool canUndo() const { return stack_.canUndo(); }
        bool canRedo() const { return stack_.canRedo(); }
        const std::string& undoDescription() const { return stack_.undoDescription(); }
        const std::string& redoDescription() const { return stack_.redoDescription(); }
        void undo();
        void redo();
        // The document was saved (or freshly loaded) with the history as it is now: remember this step.
        void markSaved() { saved_ = position_; }
        // True when the history is exactly where it was at the last markSaved() / reset() - e.g. every edit
        // since the save has been undone - so the cells equal what is on disk. False once an edit made after
        // undoing past the saved step has discarded it.
        bool isAtSavedState() const { return saved_.has_value() && *saved_ == position_; }
        // Forgets the whole history (the content was replaced by New / Open).
        void reset();

        // Fired after undo()/redo() with the bounding block of the cells that step changed.
        std::function<void(const newui::CellRange&)> onReplayed;
        // Fired after any push/undo/redo, so menus can refresh their Undo/Redo items.
        std::function<void()> onHistoryChanged;

        // "A1", "AA10" - the spreadsheet name of a cell.
        static std::string cellName(std::size_t row, std::size_t column);

    private:
        void push(std::string description, std::vector<CellEdit> edits);
        void replay(bool undoing);

        ManyRowsModel* model_;
        newui::UndoStack stack_;
        std::size_t position_ = 0;               // steps currently applied (the undo depth)
        std::optional<std::size_t> saved_ = 0;   // position_ at the last save; none once it was discarded
        newui::CellRange lastBlock_;   // set by the action closures; read by replay()
    };

}

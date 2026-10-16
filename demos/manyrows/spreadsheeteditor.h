#pragma once

#include "manyrowsmodel.h"

#include <newui/controls.h>
#include <newui/undostack.h>

#include <functional>
#include <memory>
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

        // What paste() did.
        // @reflect ignore=true
        struct PasteOutcome {
            std::string error;            // non-empty: nothing was pasted (the message is for the status bar)
            std::size_t changed = 0;      // cells whose text actually changed
            bool clipped = false;         // part of the clipboard fell past the last row / column and was dropped
            newui::CellRange block;       // the cells the paste covered (valid when error is empty)
        };

        // Pastes rows of cell text (TableClipText::parse) with its top-left cell at the top-left of `target`,
        // as ONE undo step ("Paste B2:D5"). Exception, as in Excel: ONE clipboard cell pasted over a larger
        // selection fills that whole selection. Anything past the table's last row or column is dropped
        // (`clipped`). With `typed` (the generated table) every cell is validated first and one bad cell
        // refuses the whole paste. More than kMaxRangeCells cells is refused.
        PasteOutcome paste(const newui::CellRange& target, const std::vector<std::vector<std::string>>& cells, bool typed);

        // Ctrl+D / Ctrl+R. The top row (left column) of the selection is copied over the rest; a selection of
        // one row (column) copies from the row above (column to the left) instead. One undo step ("Fill down
        // A2:A9"). Returns false when nothing changed or the block exceeds kMaxRangeCells.
        bool fillDown(const newui::CellRange& range);
        bool fillRight(const newui::CellRange& range);

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

        // What a Go to box accepts: a cell name ("c4", "AA10") or just a row number ("2500"), both 1-based as
        // typed; the result is 0-based. `column` is empty for a bare row number. Nullopt for anything else
        // (including row 0 and more than 4 letters / 9 digits).
        // @reflect ignore=true
        struct CellRef {
            std::size_t row = 0;
            std::optional<std::size_t> column;
        };
        static std::optional<CellRef> parseCellRef(const std::string& text);

    private:
        void push(std::string description, std::vector<CellEdit> edits);
        // A CellEdit setting (row, column) to `text`, or nothing when the cell already shows it.
        void addEdit(std::vector<CellEdit>& edits, std::size_t row, std::size_t column, const std::string& text) const;
        static std::string rangeName(const newui::CellRange& range);
        bool fill(const newui::CellRange& range, bool down);
        void replay(bool undoing);

        ManyRowsModel* model_;
        newui::UndoStack stack_;
        std::size_t position_ = 0;               // steps currently applied (the undo depth)
        std::optional<std::size_t> saved_ = 0;   // position_ at the last save; none once it was discarded
        std::shared_ptr<std::vector<CellEdit>> lastEdits_;   // the step just done / undone; read by replay()
    };

}

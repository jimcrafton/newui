#pragma once

#include "manyrowsmodel.h"
#include "spreadsheeteditor.h"
#include "timedtableview.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/delegate.h>
#include <newui/menus.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace manyrows {

    // Everything that makes the table behave like a spreadsheet, with no knowledge of files: typing a
    // value into a cell, undo / redo, cut / copy / paste, fill down / right, clearing, the keyboard
    // shortcuts for all of those, and the readouts that follow the table (formula bar, status bar panes).
    //
    // It works on a TimedTableView and the ManyRowsModel behind it - nothing else is required, so the lite
    // app is just `SpreadsheetController controller(table, model)` (the shortcuts and the in-cell editor
    // work straight away). Each of the parts below is optional and added with a setter:
    //   setFormulaBar    name box + formula field above the table
    //   setStatusBar     the five panes (message, paint time, visible rows, selection, total)
    //   setEditMenu      binds the Edit menu items by their text and keeps Undo / Redo's text current
    // The full app additionally owns a ManyRowsController (the DocumentController: New / Open / Save,
    // loading, saving) and connects the two through onEdited, documentReplaced() and setEditGuard() - this
    // class never mentions a file.
    //
    // It is a newui::Controller (the C between the table and the model) but does not call Controller::setModel():
    // that would make it a second owner, and the table's own TableController already owns the ManyRowsModel. It
    // observes the model through a plain reference instead, so the model must outlive it (the demos keep both
    // for the whole run).
    //
    // Lifetime: it registers handlers on the table and the controls it is given, so it must be created
    // after them and destroyed no earlier than them (the demos keep it for the whole run).
    // @reflect ignore=true
    class SpreadsheetController : public newui::Controller {
    public:
        // Status bar pane order (the order of the StatusPane children in the .newui files).
        enum Pane : std::size_t { MessagePane = 0, PaintPane = 1, RowsPane = 2, SelectionPane = 3, TotalPane = 4 };

        SpreadsheetController(TimedTableView& table, ManyRowsModel& model);
        ~SpreadsheetController() override = default;
        SpreadsheetController(const SpreadsheetController&) = delete;
        SpreadsheetController& operator=(const SpreadsheetController&) = delete;

        // ---- optional parts ----
        // Either may be null. The name box shows the active cell and is also the Go to box (type C4 or a row
        // number, Enter); Enter in the field commits the active cell.
        void setFormulaBar(newui::TextField* nameBox, newui::TextField* formulaField);
        // Fills the panes as the table paints, scrolls and selects. Null detaches nothing (set once).
        void setStatusBar(newui::StatusBar* status);
        // Binds Edit > Undo / Redo / Cut / Copy / Paste / Fill Down / Fill Right / Clear Cells / Edit Cell /
        // Select All (those that exist, matched by text) and keeps the Undo / Redo items' text and enabled
        // state current ("Undo Edit C4").
        void setEditMenu(newui::MenuBar* menuBar);
        // Binds Data > Sort Ascending / Sort Descending / Clear Sort / Filter by Selected Value / Clear Filters.
        void setDataMenu(newui::MenuBar* menuBar);
        // Binds View > Show Headers (a check item) and View > Reset Render Stats.
        void setViewMenu(newui::MenuBar* menuBar);

        // Asked before anything changes the cells: return "" to allow, or the reason edits are refused right
        // now (shown in the status bar). The full app refuses while a file is loading or saving.
        void setEditGuard(std::function<std::string()> guard) { guard_ = std::move(guard); }

        // ---- commands (the menu items and shortcuts call these; so can any other UI) ----
        void undo();
        void redo();
        void clearSelection();   // blanks the selected cells (the Delete key)
        void cut();
        void copy();
        void paste();
        void fillDown();
        void fillRight();

        // ---- sort and filter (a view over the rows: nothing moves, nothing is undone) ----
        // Sorts by the active cell's column. Alt+Up / Alt+Down.
        void sortActiveColumn(bool ascending);
        // Sorts by any column. Clicking a column header does this in a cycle (see handleHeaderClick()).
        void sortColumn(std::size_t column, bool ascending);
        void clearSort();
        // Shows only the rows whose cell in the active cell's column equals the active cell (a blank cell: the
        // blank rows). Repeating it on another column narrows further; on the same column replaces; on the very
        // same value switches that filter off. Ctrl+Shift+F.
        void filterBySelectedValue();
        // Replaces the whole filter (empty = show everything). False when a rule names a column that is not there.
        bool setFilters(std::vector<FilterRule> rules);
        void clearFilters();
        // Alt+Backspace: no sort, no filter.
        void resetView();
        // Moves the selection to a cell named like "C4" or to a row number (the active column stays). Out of range
        // clamps to the last row / column and says so; text that is neither says what is accepted. False then.
        bool goTo(const std::string& where);
        // Puts the cursor in the name box (Ctrl+G, the toolbar's Go to). No-op without a formula bar.
        void focusNameBox();
        // The active cell's column, or nullopt with no active cell.
        std::optional<std::size_t> activeColumn() const;
        // Gives the keyboard focus back to the table (after a strip closes).
        void focusTable();
        // "Sorted by Total (descending); Region = North - 23,410 of 1,000,000 rows".
        std::string viewSummary() const;

        // Validates (generated table only) and applies one typed value - the path for the in-cell editor and
        // the formula bar. False when refused (the status bar says why).
        bool commit(std::size_t row, std::size_t column, const std::string& text);

        // The content was replaced (New / Open / Sample data): drops the undo history, the selection and the
        // readouts. Does not fire onEdited.
        void documentReplaced();

        // Writes one status bar pane (no-op without a status bar).
        void setPaneText(Pane pane, const std::string& text);
        void say(const std::string& message) { setPaneText(MessagePane, message); }
        // "1,000,000 rows x 10 cols" in the total pane.
        void showTotals();

        SpreadsheetEditor& editor() { return editor_; }
        const SpreadsheetEditor& editor() const { return editor_; }

        // After any change of the cells or of the history (typing, paste, fill, clear, undo, redo) - the full
        // app marks its document modified, or clean again when the history is back at the saved step. Not
        // fired by documentReplaced().
        typedef newui::Delegate<SpreadsheetController> EditedDelegate;
        EditedDelegate onEdited;
        // After a sort or filter changed (or was cleared): the full app marks a sorted document modified (a
        // save keeps the sort order) and shows the filter in its strip.
        typedef newui::Delegate<SpreadsheetController> ViewDelegate;
        ViewDelegate onViewChanged;
        // Whenever the undo history changed in any way (an edit, undo, redo, a reset): the toolbar enables its
        // Undo / Redo buttons from canUndo() / canRedo().
        typedef newui::Delegate<SpreadsheetController> HistoryDelegate;
        HistoryDelegate onHistoryChanged;

    private:
        // True (after saying why in the status bar) when the edit guard refuses edits right now.
        bool refused();
        // Puts the selection on the clipboard as text; false when there is none or it could not be written.
        bool copySelection();
        void fill(bool down);
        void syncFormulaBar();
        void syncHistoryMenu();
        void selectBlock(const newui::CellRange& block);
        newui::SyncReturn handleKey(std::uint32_t keyMask, std::uint32_t vkey);
        newui::SyncReturn handleSelectionChanged();
        newui::SyncReturn handleHeaderClick(std::size_t column, std::uint32_t keyMask);
        newui::SyncReturn handlePainted();

        TimedTableView& table_;
        ManyRowsModel& model_;
        SpreadsheetEditor editor_;
        std::function<std::string()> guard_;
        newui::StatusBar* status_ = nullptr;
        newui::TextField* nameBox_ = nullptr;
        newui::TextField* formulaField_ = nullptr;
        newui::MenuBar* menuBar_ = nullptr;
        bool replacing_ = false;   // clearing the history is not an edit
    };

}

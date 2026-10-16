#include "spreadsheetcontroller.h"

#include "menubinding.h"
#include "tablecliptext.h"
#include "textformat.h"

#include <newui/clipboardmgr.h>
#include <newui/keyboard_constants.h>
#include <newui/rootview.h>
#include <newui/utils.h>

#include <stdexcept>

namespace manyrows {

    SpreadsheetController::SpreadsheetController(TimedTableView& table, ManyRowsModel& model)
        : table_(table), model_(model), editor_(model) {
        // Typed values (in-cell editor) come here; the table never changes data itself.
        table_.setCommitHandler([this](std::size_t row, std::size_t column, const std::string& text) { return commit(row, column, text); });

        // Undo / redo move the selection onto the cells they changed (what a spreadsheet does).
        editor_.onReplayed = [this](const newui::CellRange& block) { selectBlock(block); };
        editor_.onHistoryChanged = [this]() {
            syncHistoryMenu();
            if (!replacing_) {
                onEdited(*this);
            }
        };

        table_.onKeyDown += [this](newui::View&, std::uint32_t keyMask, int, int, std::uint32_t vkey) { return handleKey(keyMask, vkey); };
        table_.onSelectionChanged += [this](newui::TableView&) { return handleSelectionChanged(); };
        table_.onPainted += [this](TimedTableView&) { return handlePainted(); };
        // Programmatic edits (undo, paste...) change the active cell's text without moving the selection.
        model_.onChanged += [this](newui::Model&) {
            syncFormulaBar();
            return newui::SyncReturn::Ignored;
        };
    }

    // ---- optional parts ----

    void SpreadsheetController::setFormulaBar(newui::TextField* nameBox, newui::TextField* formulaField) {
        nameBox_ = nameBox;
        formulaField_ = formulaField;
        if (nameBox_ != nullptr) {
            nameBox_->controller().inputTraits().setReadOnly(true);
        }
        if (formulaField_ != nullptr) {
            // Enter in the field commits it to the active cell and hands the focus back to the table.
            formulaField_->onReturnPressed += [this](newui::TextField&) {
                if (table_.hasActiveCell() && commit(table_.activeRow(), table_.activeColumn(), newui::wideToUtf8(formulaField_->text()))) {
                    if (auto* root = table_.rootView()) {
                        root->setFocusedSubView(&table_);
                    }
                }
                return newui::SyncReturn::Handled;
            };
        }
        syncFormulaBar();
    }

    void SpreadsheetController::setStatusBar(newui::StatusBar* status) {
        status_ = status;
        showTotals();
    }

    void SpreadsheetController::setEditMenu(newui::MenuBar* menuBar) {
        menuBar_ = menuBar;
        if (menuBar_ == nullptr) {
            return;
        }
        auto bind = [this](const char* text, std::function<void()> action) {
            bindMenuItem(menuBar_, "Edit", text, [action](newui::MenuItem&) { action(); });
        };
        bind("Undo", [this]() { undo(); });
        bind("Redo", [this]() { redo(); });
        bind("Cut", [this]() { cut(); });
        bind("Copy", [this]() { copy(); });
        bind("Paste", [this]() { paste(); });
        bind("Fill Down", [this]() { fillDown(); });
        bind("Fill Right", [this]() { fillRight(); });
        bind("Clear Cells", [this]() { clearSelection(); });
        bind("Edit Cell", [this]() { table_.beginEdit(); });
        bind("Select All", [this]() { table_.selectAll(); });
        syncHistoryMenu();
    }

    void SpreadsheetController::setViewMenu(newui::MenuBar* menuBar) {
        bindMenuItem(menuBar, "View", "Show Headers", [this](newui::MenuItem& item) {
            table_.setHeadersVisible(!table_.headersVisible());
            item.setChecked(table_.headersVisible());
        });
        bindMenuItem(menuBar, "View", "Reset Render Stats", [this](newui::MenuItem&) { table_.resetPaintStats(); });
    }

    // ---- commands ----

    bool SpreadsheetController::refused() {
        if (!guard_) {
            return false;
        }
        std::string reason = guard_();
        if (reason.empty()) {
            return false;
        }
        say(reason);
        return true;
    }

    bool SpreadsheetController::commit(std::size_t row, std::size_t column, const std::string& typed) {
        if (refused()) {
            return false;
        }
        std::string text = typed;
        // The typed schema (Qty a whole number, Status one of five...) belongs to the generated table; a
        // loaded file accepts any text.
        std::string error = model_.isGenerated() ? SpreadsheetEditor::validate(column, text) : std::string();
        if (!error.empty()) {
            say(SpreadsheetEditor::cellName(row, column) + ": " + error);
            return false;
        }
        editor_.setCell(row, column, text);
        return true;
    }

    void SpreadsheetController::undo() {
        if (editor_.canUndo() && !refused()) {
            std::string what = editor_.undoDescription();
            editor_.undo();
            say("Undid " + what);
        }
    }

    void SpreadsheetController::redo() {
        if (editor_.canRedo() && !refused()) {
            std::string what = editor_.redoDescription();
            editor_.redo();
            say("Redid " + what);
        }
    }

    void SpreadsheetController::clearSelection() {
        std::optional<newui::CellRange> range = table_.selectedRange();
        if (!range.has_value() || refused()) {
            return;
        }
        if (!editor_.clearRange(*range)) {
            say("Nothing cleared (already blank, or more than " + withCommas(SpreadsheetEditor::kMaxRangeCells) + " cells)");
        }
    }

    bool SpreadsheetController::copySelection() {
        std::optional<newui::CellRange> range = table_.selectedRange();
        if (!range.has_value()) {
            return false;
        }
        std::string text;
        try {
            text = TableClipText::encode(model_, *range);
        } catch (const std::runtime_error&) {
            say("Too many cells to copy (limit 1,000,000)");
            return false;
        }
        // The table owns the clipboard data (and is asked to render it later if it ever is delayed).
        if (!newui::ClipboardManager::setText(newui::utf8ToWide(text), &table_)) {
            say("The clipboard is busy - try again");
            return false;
        }
        say("Copied " + withCommas(range->rowCount() * range->columnCount()) + " cells");
        return true;
    }

    void SpreadsheetController::copy() { copySelection(); }

    void SpreadsheetController::cut() {
        if (!table_.selectedRange().has_value() || refused()) {
            return;
        }
        if (copySelection()) {
            clearSelection();   // one undo step; the cells are on the clipboard already
        }
    }

    void SpreadsheetController::paste() {
        std::optional<newui::CellRange> range = table_.selectedRange();
        if (!range.has_value() || refused()) {
            return;
        }
        std::wstring wide;
        if (!newui::ClipboardManager::getText(wide) || wide.empty()) {
            say("The clipboard has no text to paste");
            return;
        }
        SpreadsheetEditor::PasteOutcome outcome = editor_.paste(*range, TableClipText::parse(newui::wideToUtf8(wide)), model_.isGenerated());
        if (!outcome.error.empty()) {
            say(outcome.error);
            return;
        }
        selectBlock(outcome.block);
        say(outcome.changed == 0 ? std::string("Paste changed nothing")
                                 : "Pasted " + withCommas(outcome.changed) + " cells" +
                                       (outcome.clipped ? " - the part past the end of the table was dropped" : ""));
    }

    void SpreadsheetController::fillDown() { fill(true); }
    void SpreadsheetController::fillRight() { fill(false); }

    void SpreadsheetController::fill(bool down) {
        std::optional<newui::CellRange> range = table_.selectedRange();
        if (!range.has_value() || refused()) {
            return;
        }
        if (!(down ? editor_.fillDown(*range) : editor_.fillRight(*range))) {
            say("Nothing to fill (already equal, nothing above / left to copy, or more than " + withCommas(SpreadsheetEditor::kMaxRangeCells) +
                " cells)");
        }
    }

    void SpreadsheetController::documentReplaced() {
        replacing_ = true;
        editor_.reset();
        replacing_ = false;
        table_.clearSelection();
        syncFormulaBar();
        showTotals();
    }

    // ---- readouts ----

    void SpreadsheetController::setPaneText(Pane pane, const std::string& text) {
        if (status_ != nullptr) {
            status_->setPaneText(pane, text);
        }
    }

    void SpreadsheetController::showTotals() {
        setPaneText(TotalPane, withCommas(model_.rowCount()) + " rows x " + std::to_string(model_.columnCount()) + " cols");
    }

    void SpreadsheetController::syncFormulaBar() {
        if (nameBox_ == nullptr || formulaField_ == nullptr) {
            return;
        }
        if (!table_.hasActiveCell()) {
            nameBox_->setText(L"");
            formulaField_->setText(L"");
            return;
        }
        nameBox_->setText(newui::utf8ToWide(SpreadsheetEditor::cellName(table_.activeRow(), table_.activeColumn())));
        formulaField_->setText(newui::utf8ToWide(model_.cellText(table_.activeRow(), table_.activeColumn())));
    }

    // Edit > Undo / Redo show what they would act on.
    void SpreadsheetController::syncHistoryMenu() {
        if (menuBar_ == nullptr) {
            return;
        }
        for (newui::MenuItem* menu : menuBar_->menus()) {
            if (menu->text() != "Edit") {
                continue;
            }
            for (newui::MenuItem* item : menu->children()) {
                if (item->text().rfind("Undo", 0) == 0) {
                    item->setText(editor_.canUndo() ? "Undo " + editor_.undoDescription() : "Undo");
                    item->state().setEnabled(editor_.canUndo());
                } else if (item->text().rfind("Redo", 0) == 0) {
                    item->setText(editor_.canRedo() ? "Redo " + editor_.redoDescription() : "Redo");
                    item->state().setEnabled(editor_.canRedo());
                }
            }
        }
    }

    void SpreadsheetController::selectBlock(const newui::CellRange& block) {
        table_.setActiveCell(block.firstRow, block.firstColumn);
        if (block.lastRow > block.firstRow || block.lastColumn > block.firstColumn) {
            table_.setActiveCell(block.lastRow, block.lastColumn, true);
        }
    }

    // ---- table events ----

    newui::SyncReturn SpreadsheetController::handleKey(std::uint32_t keyMask, std::uint32_t vkey) {
        bool ctrl = (keyMask & newui::kmCtrl) != 0;
        if (ctrl) {
            switch (vkey) {
                case newui::vkLetterZ: undo(); return newui::SyncReturn::Handled;
                case newui::vkLetterY: redo(); return newui::SyncReturn::Handled;
                case newui::vkLetterC: copy(); return newui::SyncReturn::Handled;
                case newui::vkLetterX: cut(); return newui::SyncReturn::Handled;
                case newui::vkLetterV: paste(); return newui::SyncReturn::Handled;
                case newui::vkLetterD: fillDown(); return newui::SyncReturn::Handled;
                case newui::vkLetterR: fillRight(); return newui::SyncReturn::Handled;
                default: break;
            }
        }
        if (vkey == newui::vkDelete) {
            clearSelection();
            return newui::SyncReturn::Handled;
        }
        return newui::SyncReturn::Ignored;
    }

    newui::SyncReturn SpreadsheetController::handleSelectionChanged() {
        syncFormulaBar();
        std::optional<newui::CellRange> range = table_.selectedRange();
        if (!range.has_value()) {
            setPaneText(SelectionPane, "no selection");
            return newui::SyncReturn::Handled;
        }
        std::string text = SpreadsheetEditor::cellName(range->firstRow, range->firstColumn);
        std::size_t cells = range->rowCount() * range->columnCount();
        if (cells > 1) {
            text += ":" + SpreadsheetEditor::cellName(range->lastRow, range->lastColumn) + "  (" + withCommas(cells) + " cells)";
        }
        setPaneText(SelectionPane, text);
        return newui::SyncReturn::Handled;
    }

    // Painting updates the readouts, so scrolling keeps them live; setPaneText marks only that pane dirty,
    // which is cheap enough to run on every paint.
    newui::SyncReturn SpreadsheetController::handlePainted() {
        if (status_ == nullptr) {
            return newui::SyncReturn::Handled;
        }
        setPaneText(PaintPane, "paint " + millis(table_.lastPaintMs()) + " / " + millis(table_.smoothedPaintMs()) + " / " +
                                   millis(table_.worstPaintMs()) + " ms");
        std::size_t count = table_.visibleRowCount();
        if (count == 0) {
            setPaneText(RowsPane, "rows -");
        } else {
            std::size_t first = table_.firstVisibleRow();
            setPaneText(RowsPane, "rows " + withCommas(first + 1) + "-" + withCommas(first + count));
        }
        return newui::SyncReturn::Handled;
    }

}

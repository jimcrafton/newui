#include "spreadsheeteditor.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <memory>

namespace manyrows {

    namespace {
        std::string trimmed(const std::string& text) {
            std::size_t first = 0;
            std::size_t last = text.size();
            while (first < last && std::isspace((unsigned char)text[first])) {
                ++first;
            }
            while (last > first && std::isspace((unsigned char)text[last - 1])) {
                --last;
            }
            return text.substr(first, last - first);
        }

        bool sameFold(const std::string& a, const char* b) {
            std::size_t i = 0;
            for (; i < a.size() && b[i] != '\0'; ++i) {
                if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) {
                    return false;
                }
            }
            return i == a.size() && b[i] == '\0';
        }
    }

    std::string SpreadsheetEditor::cellName(std::size_t row, std::size_t column) {
        std::string letters;
        for (std::size_t n = column + 1; n > 0; n = (n - 1) / 26) {
            letters.insert(letters.begin(), char('A' + (n - 1) % 26));
        }
        return letters + std::to_string(row + 1);
    }

    std::string SpreadsheetEditor::validate(std::size_t column, std::string& text) {
        text = trimmed(text);
        if (text.empty()) {
            return std::string();
        }
        char buffer[48];
        switch (ManyRowsModel::columnType(column)) {
            case ManyRowsModel::ColumnType::Integer: {
                std::size_t digits = text[0] == '-' ? 1 : 0;
                if (digits == text.size() || text.size() - digits > 12) {
                    return "Enter a whole number";
                }
                for (std::size_t i = digits; i < text.size(); ++i) {
                    if (!std::isdigit((unsigned char)text[i])) {
                        return "Enter a whole number";
                    }
                }
                return std::string();
            }
            case ManyRowsModel::ColumnType::Number: {
                char* end = nullptr;
                double value = std::strtod(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0' || value != value || value > 1e12 || value < -1e12) {
                    return "Enter a number";
                }
                std::snprintf(buffer, sizeof(buffer), column == ManyRowsModel::kRatingColumn ? "%.1f" : "%.2f", value);
                text = buffer;
                return std::string();
            }
            case ManyRowsModel::ColumnType::Date: {
                unsigned y = 0, m = 0, d = 0;
                char extra = 0;
                if (text.size() != 10 || std::sscanf(text.c_str(), "%4u-%2u-%2u%c", &y, &m, &d, &extra) != 3 || m < 1 || m > 12
                    || d < 1 || d > 31) {
                    return "Enter a date as yyyy-mm-dd";
                }
                return std::string();
            }
            case ManyRowsModel::ColumnType::Text:
                if (column == ManyRowsModel::kStatusColumn) {
                    static const char* const kStatuses[] = {"Pending", "Shipped", "Delivered", "Returned", "Cancelled"};
                    for (const char* status : kStatuses) {
                        if (sameFold(text, status)) {
                            text = status;
                            return std::string();
                        }
                    }
                    return "Status must be Pending, Shipped, Delivered, Returned or Cancelled";
                }
                return std::string();
        }
        return std::string();
    }

    void SpreadsheetEditor::push(std::string description, std::vector<CellEdit> edits) {
        newui::CellRange block;
        block.firstRow = block.lastRow = edits.front().row;
        block.firstColumn = block.lastColumn = edits.front().column;
        for (const CellEdit& edit : edits) {
            block.firstRow = edit.row < block.firstRow ? edit.row : block.firstRow;
            block.lastRow = edit.row > block.lastRow ? edit.row : block.lastRow;
            block.firstColumn = edit.column < block.firstColumn ? edit.column : block.firstColumn;
            block.lastColumn = edit.column > block.lastColumn ? edit.column : block.lastColumn;
        }
        auto shared = std::make_shared<std::vector<CellEdit>>(std::move(edits));
        newui::UndoableAction action;
        action.description = std::move(description);
        action.doIt = [this, shared, block]() {
            model_->applyEdits(*shared, true);
            lastBlock_ = block;
        };
        action.undoIt = [this, shared, block]() {
            model_->applyEdits(*shared, false);
            lastBlock_ = block;
        };
        stack_.push(std::move(action));   // also drops every redo step
        if (saved_.has_value() && *saved_ > position_) {
            saved_.reset();   // the saved state lay in the redo steps just discarded: it cannot be reached again
        }
        ++position_;
        if (onHistoryChanged) {
            onHistoryChanged();
        }
    }

    bool SpreadsheetEditor::setCell(std::size_t row, std::size_t column, const std::string& text) {
        if (model_->cellText(row, column) == text) {
            return false;
        }
        CellEdit edit;
        edit.row = row;
        edit.column = column;
        edit.before = model_->editAt(row, column);
        edit.after = text;
        push("Edit " + cellName(row, column), {edit});
        return true;
    }

    bool SpreadsheetEditor::clearRange(const newui::CellRange& range) {
        if (double(range.rowCount()) * double(range.columnCount()) > double(kMaxRangeCells)) {
            return false;
        }
        std::vector<CellEdit> edits;
        for (std::size_t r = range.firstRow; r <= range.lastRow; ++r) {
            for (std::size_t c = range.firstColumn; c <= range.lastColumn; ++c) {
                if (model_->cellText(r, c).empty()) {
                    continue;
                }
                CellEdit edit;
                edit.row = r;
                edit.column = c;
                edit.before = model_->editAt(r, c);
                edit.after = std::string();
                edits.push_back(std::move(edit));
            }
        }
        if (edits.empty()) {
            return false;
        }
        std::string name = cellName(range.firstRow, range.firstColumn);
        if (range.rowCount() * range.columnCount() > 1) {
            name += ":" + cellName(range.lastRow, range.lastColumn);
        }
        push("Clear " + name, std::move(edits));
        return true;
    }

    void SpreadsheetEditor::addEdit(std::vector<CellEdit>& edits, std::size_t row, std::size_t column, const std::string& text) const {
        if (model_->cellText(row, column) == text) {
            return;
        }
        CellEdit edit;
        edit.row = row;
        edit.column = column;
        edit.before = model_->editAt(row, column);
        edit.after = text;
        edits.push_back(std::move(edit));
    }

    std::string SpreadsheetEditor::rangeName(const newui::CellRange& range) {
        std::string name = cellName(range.firstRow, range.firstColumn);
        if (range.rowCount() * range.columnCount() > 1) {
            name += ":" + cellName(range.lastRow, range.lastColumn);
        }
        return name;
    }

    SpreadsheetEditor::PasteOutcome SpreadsheetEditor::paste(
        const newui::CellRange& target, const std::vector<std::vector<std::string>>& cells, bool typed) {
        PasteOutcome outcome;
        std::size_t width = 0;
        for (const std::vector<std::string>& row : cells) {
            width = row.size() > width ? row.size() : width;
        }
        if (cells.empty() || width == 0) {
            outcome.error = "Nothing to paste";
            return outcome;
        }
        std::size_t rows = model_->rowCount();
        std::size_t columns = model_->columnCount();
        if (target.firstRow >= rows || target.firstColumn >= columns) {
            outcome.error = "Nothing to paste there";
            return outcome;
        }
        // One cell pasted over a bigger selection fills the selection; otherwise the clipboard's own shape rules.
        bool single = cells.size() == 1 && width == 1;
        bool fillTarget = single && target.rowCount() * target.columnCount() > 1;
        std::size_t high = fillTarget ? target.rowCount() : cells.size();
        std::size_t wide = fillTarget ? target.columnCount() : width;
        if (double(high) * double(wide) > double(kMaxRangeCells)) {
            outcome.error = "Too much to paste at once (limit " + std::to_string(kMaxRangeCells) + " cells)";
            return outcome;
        }
        std::size_t lastRow = target.firstRow + high - 1;
        std::size_t lastColumn = target.firstColumn + wide - 1;
        if (lastRow >= rows) {
            lastRow = rows - 1;
            outcome.clipped = true;
        }
        if (lastColumn >= columns) {
            lastColumn = columns - 1;
            outcome.clipped = true;
        }
        std::vector<CellEdit> edits;
        for (std::size_t r = target.firstRow; r <= lastRow; ++r) {
            for (std::size_t c = target.firstColumn; c <= lastColumn; ++c) {
                std::size_t i = fillTarget ? 0 : r - target.firstRow;
                std::size_t j = fillTarget ? 0 : c - target.firstColumn;
                std::string text = j < cells[i].size() ? cells[i][j] : std::string();
                if (typed) {
                    std::string error = validate(c, text);
                    if (!error.empty()) {
                        outcome.error = cellName(r, c) + ": " + error;
                        return outcome;
                    }
                }
                addEdit(edits, r, c, text);
            }
        }
        outcome.block.firstRow = target.firstRow;
        outcome.block.firstColumn = target.firstColumn;
        outcome.block.lastRow = lastRow;
        outcome.block.lastColumn = lastColumn;
        outcome.changed = edits.size();
        if (!edits.empty()) {
            push("Paste " + rangeName(outcome.block), std::move(edits));
        }
        return outcome;
    }

    bool SpreadsheetEditor::fill(const newui::CellRange& range, bool down) {
        if (double(range.rowCount()) * double(range.columnCount()) > double(kMaxRangeCells)) {
            return false;
        }
        newui::CellRange area = range;   // the cells that get overwritten
        std::size_t source = down ? range.firstRow : range.firstColumn;   // the row / column copied from
        if (down ? range.rowCount() == 1 : range.columnCount() == 1) {
            if (source == 0) {
                return false;   // nothing above / to the left to copy
            }
            --source;
        } else if (down) {
            ++area.firstRow;
        } else {
            ++area.firstColumn;
        }
        std::vector<CellEdit> edits;
        for (std::size_t r = area.firstRow; r <= area.lastRow; ++r) {
            for (std::size_t c = area.firstColumn; c <= area.lastColumn; ++c) {
                std::string text = down ? model_->cellText(source, c) : model_->cellText(r, source);
                // Filling right carries text across columns; the generated table's typed columns only take their own kind.
                if (!down && model_->isGenerated() && !validate(c, text).empty()) {
                    continue;
                }
                addEdit(edits, r, c, text);
            }
        }
        if (edits.empty()) {
            return false;
        }
        push(std::string(down ? "Fill down " : "Fill right ") + rangeName(area), std::move(edits));
        return true;
    }

    bool SpreadsheetEditor::fillDown(const newui::CellRange& range) { return fill(range, true); }
    bool SpreadsheetEditor::fillRight(const newui::CellRange& range) { return fill(range, false); }

    void SpreadsheetEditor::replay(bool undoing) {
        if (undoing ? !stack_.canUndo() : !stack_.canRedo()) {
            return;
        }
        if (undoing) {
            stack_.undo();
            --position_;
        } else {
            stack_.redo();
            ++position_;
        }
        if (onReplayed) {
            onReplayed(lastBlock_);
        }
        if (onHistoryChanged) {
            onHistoryChanged();
        }
    }

    void SpreadsheetEditor::reset() {
        stack_.clear();
        position_ = 0;
        saved_ = 0;   // new content is clean
        if (onHistoryChanged) {
            onHistoryChanged();
        }
    }

    void SpreadsheetEditor::undo() { replay(true); }
    void SpreadsheetEditor::redo() { replay(false); }

}

#include "tablecliptext.h"

#include <stdexcept>

namespace manyrows {

    namespace {
        bool needsQuotes(const std::string& cell) {
            return cell.find_first_of("\t\r\n\"") != std::string::npos;
        }
    }

    std::string TableClipText::encode(const ManyRowsModel& model, const newui::CellRange& range, std::size_t maxCells) {
        if (double(range.rowCount()) * double(range.columnCount()) > double(maxCells)) {
            throw std::runtime_error("TableClipText::encode: the block is larger than " + std::to_string(maxCells) + " cells");
        }
        std::string out;
        for (std::size_t r = range.firstRow; r <= range.lastRow; ++r) {
            for (std::size_t c = range.firstColumn; c <= range.lastColumn; ++c) {
                if (c > range.firstColumn) {
                    out += '\t';
                }
                std::string cell = model.cellText(r, c);
                if (!needsQuotes(cell)) {
                    out += cell;
                    continue;
                }
                out += '"';
                for (char ch : cell) {
                    if (ch == '"') {
                        out += '"';   // a quote inside a quoted cell is doubled
                    }
                    out += ch;
                }
                out += '"';
            }
            out += "\r\n";
        }
        return out;
    }

    std::vector<std::vector<std::string>> TableClipText::parse(const std::string& text) {
        std::vector<std::vector<std::string>> rows;
        std::vector<std::string> row;
        std::string cell;
        bool inQuotes = false;
        bool cellWasQuoted = false;   // a quoted empty cell ("") still counts as a cell
        bool rowStarted = false;      // anything seen since the last line break
        auto endCell = [&]() {
            row.push_back(std::move(cell));
            cell.clear();
            cellWasQuoted = false;
        };
        auto endRow = [&]() {
            endCell();
            rows.push_back(std::move(row));
            row.clear();
            rowStarted = false;
        };
        for (std::size_t i = 0; i < text.size(); ++i) {
            char ch = text[i];
            if (inQuotes) {
                if (ch == '"') {
                    if (i + 1 < text.size() && text[i + 1] == '"') {
                        cell += '"';
                        ++i;
                    } else {
                        inQuotes = false;
                    }
                } else {
                    cell += ch;
                }
                continue;
            }
            if (ch == '"' && cell.empty() && !cellWasQuoted) {
                inQuotes = true;   // a quote opens a quoted cell only at the very start of a cell
                cellWasQuoted = true;
                rowStarted = true;
            } else if (ch == '\t') {
                endCell();
                rowStarted = true;
            } else if (ch == '\r' || ch == '\n') {
                if (ch == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
                    ++i;
                }
                endRow();
            } else {
                cell += ch;
                rowStarted = true;
            }
        }
        if (rowStarted || !cell.empty() || cellWasQuoted) {
            endRow();   // the last line had no line break
        }
        return rows;
    }

}

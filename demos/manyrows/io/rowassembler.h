#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace manyrows {
    namespace io {

        // One cell of a spreadsheet row as the Excel readers see it.
        // @reflect ignore=true
        struct Cell {
            std::string text;
            bool numeric = false;   // a plain number (not a date, not text)
        };

        // Widest sheet either Excel format allows (XLSX: column XFD).
        constexpr std::size_t kMaxSheetColumns = 16384;

        // Collects cells (which arrive row by row, left to right) into whole rows, filling the gaps with
        // empty rows so row numbers match the sheet. Cells out of order or past kMaxSheetColumns are dropped.
        // @reflect ignore=true
        class RowAssembler {
        public:
            typedef std::function<bool(std::size_t row, const std::vector<Cell>& cells)> Emit;
            explicit RowAssembler(Emit emit) : emit_(std::move(emit)) {}

            // False when the emit callback asked to stop.
            bool add(std::size_t row, std::size_t column, std::string&& text, bool numeric) {
                if (row < current_ || column >= kMaxSheetColumns) {
                    return true;
                }
                while (current_ < row) {
                    bool more = emit_(current_, cells_);
                    cells_.clear();
                    ++current_;
                    if (!more) {
                        return false;
                    }
                }
                if (cells_.size() <= column) {
                    cells_.resize(column + 1);
                }
                cells_[column].text = std::move(text);
                cells_[column].numeric = numeric;
                return true;
            }

            // Emits the row in progress (when it has cells).
            bool finish() {
                if (cells_.empty()) {
                    return true;
                }
                bool more = emit_(current_, cells_);
                cells_.clear();
                ++current_;
                return more;
            }

        private:
            Emit emit_;
            std::size_t current_ = 0;
            std::vector<Cell> cells_;
        };

        // Does the first of `top` (the sheet's first rows) look like a header? Every one of its `columns`
        // cells must hold text, while some column below holds a number. Fills `titles` when it does.
        inline bool sniffHeader(const std::vector<std::vector<Cell>>& top, std::size_t columns, std::vector<std::string>& titles) {
            titles.clear();
            if (top.size() < 2 || columns == 0 || top[0].size() < columns) {
                return false;
            }
            for (std::size_t c = 0; c < columns; ++c) {
                if (top[0][c].text.empty() || top[0][c].numeric) {
                    return false;
                }
            }
            bool numberBelow = false;
            for (std::size_t r = 1; r < top.size() && !numberBelow; ++r) {
                for (std::size_t c = 0; c < top[r].size() && c < columns; ++c) {
                    if (top[r][c].numeric) {
                        numberBelow = true;
                        break;
                    }
                }
            }
            if (!numberBelow) {
                return false;
            }
            for (std::size_t c = 0; c < columns; ++c) {
                titles.push_back(top[0][c].text);
            }
            return true;
        }

    }
}

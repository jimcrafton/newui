#pragma once

#include "filterrule.h"
#include "rowindex.h"

#include <newui/models.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace manyrows {

    // One cell's change, as the undo history stores it: the overlay entry before and after
    // (nullopt = no overlay entry, i.e. the cell shows its underlying value). Storing overlay
    // state rather than displayed text is what lets undoing the first edit of a cell restore the
    // underlying value instead of pinning it as an "edit".
    // @reflect ignore=true
    struct CellEdit {
        std::size_t row = 0;
        std::size_t column = 0;
        std::optional<std::string> before;
        std::optional<std::string> after;
    };

    // The table's data. Two kinds of content sit under the same edit overlay:
    //
    // GENERATED (the startup demo): a million-row, ten-column "orders" table that is computed, not stored.
    // Every cell is a pure function of (seed, row, column) - a splitmix64 hash picks the item from a word
    // list, the quantity, the price and so on - so startup is instant and the model costs a few bytes
    // however many rows it claims. Columns (see columnTypeOf()): Order (int), Item, Category, Region,
    // Status (text), Qty (int), Price (double), Total (double, Qty x Price), Date (yyyy-mm-dd), Rating (double).
    //
    // LOADED (a file opened with File > Open, or a blank sheet from File > New): rows of text read by an
    // io::TableFormat via beginLoad() / appendRow() / endLoad(). Stored compactly - one string arena plus a
    // 32-bit end offset per cell and a per-row start index (about 4 bytes per cell on top of the text, 8 per
    // row), so a million-row file stays in the hundreds of MB at most. Column types are inferred from the
    // first rows (all-numeric columns right-align).
    //
    // SORT AND FILTER are a view over the stored rows (see RowIndex): the cells never move. Every row number the
    // table, the editor and the clipboard use - rowCount(), cellText(), editAt() - is a DISPLAY row; the stored
    // row behind it comes from sourceRow(). Edits (CellEdit::row) and the overlay are keyed by STORED row, so
    // sorting or filtering never loses or misplaces an edit, and undo steps stay valid across re-sorting. A
    // sort or filter is not re-applied when a cell is edited (what a spreadsheet does) - sort or filter again.
    // A saved file keeps every row, in sort order (orderedSourceRow()), whatever the filter hides.
    //
    // Edits live in a sparse overlay (only the cells the user changed are remembered); a cell not in the
    // overlay shows its generated / loaded value. Values are handed to TableView as strings - the paint path
    // only ever asks for the rows on screen.
    // @reflect ignore=true
    class ManyRowsModel : public newui::TableModel {
    public:
        enum class ColumnType { Integer, Number, Text, Date };

        // The generated table's width (and the largest column count a loaded table may have is kMaxColumns).
        static constexpr std::size_t kColumnCount = 10;
        static constexpr std::size_t kMaxColumns = 65535;
        // Columns CellItem gives special typography (generated data only).
        static constexpr std::size_t kOrderColumn = 0;
        static constexpr std::size_t kStatusColumn = 4;
        static constexpr std::size_t kRatingColumn = 9;

        // Starts as the generated table.
        explicit ManyRowsModel(std::size_t rows = 1000000, std::uint64_t seed = 20260101);

        // Rows on screen: all of them, or only those passing the filter.
        std::size_t rowCount() const override { return index_.rowCount(rows_); }
        std::size_t columnCount() const override { return columns_; }
        // The title the table paints: titleText() plus an arrow on the sorted column.
        std::string columnTitle(std::size_t column) const override;
        // The title itself, without the sort arrow - what a saved file's header row holds.
        std::string titleText(std::size_t column) const;

        // Key is a std::pair<std::size_t, std::size_t> (row, column) - see TableModel::valueAt().
        // Returns the cell text (a std::string), or an empty std::any for a bad key.
        std::any value(const std::any& key = std::any()) override;

        // Stores the text in the overlay and fires onChanged. newValue may be a std::string or a
        // const char*; anything else is ignored. Out-of-range cells throw std::runtime_error.
        void setValue(const std::any& newValue, const std::any& key = std::any()) override;

        // Text of a cell (by DISPLAY row) without the std::any boxing - the hot path for anything iterating cells.
        std::string cellText(std::size_t row, std::size_t column) const;
        // The same by STORED row (what sorting, filtering and saving read).
        std::string sourceText(std::size_t sourceRow, std::size_t column) const;

        // ---- sort and filter ----
        struct SortKey {
            std::size_t column = 0;
            bool ascending = true;
        };
        // Stored rows, and the stored row shown at a display row / the display row of a stored row (nullopt:
        // hidden by the filter).
        std::size_t sourceRowCount() const { return rows_; }
        std::size_t sourceRow(std::size_t displayRow) const { return index_.sourceRow(displayRow); }
        std::optional<std::size_t> displayRowOf(std::size_t sourceRow) const { return index_.displayRowOf(sourceRow, rows_); }
        // The i-th stored row in sort order, ignoring the filter - the order a file is written in.
        std::size_t orderedSourceRow(std::size_t i) const { return index_.orderedSourceRow(i); }

        // Stable sort of the current order by one column (blanks last either way); ties keep their previous order,
        // so sorting by B and then by A gives A, then B. Re-applies the filter. Fires onChanged once.
        // Throws std::runtime_error for a column past the last.
        void sortBy(std::size_t column, bool ascending);
        void clearSort();
        const std::optional<SortKey>& sortKey() const { return sortKey_; }
        bool isSorted() const { return sortKey_.has_value(); }

        // Shows only the rows passing ALL the rules (an empty list shows everything). Fires onChanged once.
        // Throws std::runtime_error for a rule on a column past the last (nothing changes).
        void setFilters(std::vector<FilterRule> rules);
        void clearFilters() { setFilters({}); }
        const std::vector<FilterRule>& filters() const { return filters_; }
        bool isFiltered() const { return !filters_.empty(); }
        // Both back to showing every row in stored order, without firing onChanged (a load calls this).
        void resetView();

        // The generated schema's column types (what SpreadsheetEditor validates against).
        static ColumnType columnType(std::size_t column);
        // The type of a column of THIS table: the generated schema's, or what was inferred from loaded rows.
        ColumnType columnTypeOf(std::size_t column) const;

        bool isGenerated() const { return generated_; }
        // True when columnTitle() returns real titles (generated schema, or a file's header row); false when
        // the view should show A, B, C... and a saved file should have no header row.
        bool hasTitles() const { return generated_ || !titles_.empty(); }

        // ---- loading ----
        // Replaces the content with a loaded table. Call beginLoad(), then appendRow() for each row (rows
        // shorter than `columns` read as blank past their end, longer ones are cut), then endLoad() - which
        // fires onChanged once. `titles` is empty for none. Throws std::runtime_error for more than
        // kMaxColumns columns or text past 4 GB; the model is then left empty.
        void beginLoad(std::size_t columns, std::vector<std::string> titles);
        void appendRow(const std::vector<std::string>& cells);
        void endLoad();
        // Takes over the content of `staged` - a table built elsewhere (a worker thread's ManyRowsLoader) -
        // in O(1) by swapping storage, drops this table's edits and fires onChanged once. `staged` must be a
        // finished (endLoad()ed) loaded table; it is left blank. Throws std::runtime_error when it is not.
        void adoptLoaded(ManyRowsModel& staged);
        // A blank loaded table of rows x columns (File > New).
        void makeBlank(std::size_t rows, std::size_t columns);
        // Back to the generated demo table.
        void makeGenerated(std::size_t rows = 1000000);

        // The overlay entry for a cell (nullopt when it still shows its underlying value).
        std::optional<std::string> editAt(std::size_t row, std::size_t column) const;
        bool isEdited(std::size_t row, std::size_t column) const { return editAt(row, column).has_value(); }

        // Applies every edit's `after` (forward) or `before` (backward) to the overlay and fires
        // onChanged ONCE - a 100,000-cell clear is one notification, not 100,000. Out-of-range
        // cells throw std::runtime_error before anything is changed.
        void applyEdits(const std::vector<CellEdit>& edits, bool forward);

        // How many cells have been edited (the overlay's size).
        std::size_t editedCellCount() const { return edits_.size(); }

    private:
        static std::uint64_t mix(std::uint64_t value);
        std::uint64_t hashOf(std::size_t row, std::size_t column) const;
        std::string generate(std::size_t row, std::size_t column) const;
        std::string loadedText(std::size_t row, std::size_t column) const;
        void inferColumnTypes();
        static std::uint64_t cellKey(std::size_t row, std::size_t column) {
            return (std::uint64_t(row) << 16) | std::uint64_t(column);
        }

        bool generated_ = true;
        std::size_t rows_;
        std::size_t columns_ = kColumnCount;
        std::uint64_t seed_;
        std::unordered_map<std::uint64_t, std::string> edits_;

        RowIndex index_;
        std::optional<SortKey> sortKey_;
        std::vector<FilterRule> filters_;
        void applyFilters();

        // Loaded content (empty while generated_).
        std::string arena_;
        std::vector<std::uint32_t> rowFirst_;   // rows_ + 1 entries: index of each row's first cell in cellEnd_
        std::vector<std::uint32_t> cellEnd_;    // end offset in arena_ of every cell, row after row
        std::vector<std::string> titles_;
        std::vector<ColumnType> types_;
    };

}

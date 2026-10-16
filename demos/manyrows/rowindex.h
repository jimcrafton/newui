#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace manyrows {

    // Which stored (source) row each displayed row shows, once the table is sorted and/or filtered. Two
    // layers, both plain index lists (4 bytes per row, so a million rows cost 4 MB each) - the cells are
    // never moved or copied:
    //
    //   order   a permutation of ALL source rows: the sort. Empty (not sorted) means source order.
    //   view    the rows that pass the filter, in display order. Used only while filtered.
    //
    // A display row d shows source row view[d] when filtered, else order[d] when sorted, else d. Edits are
    // remembered against SOURCE rows (so an undo step survives re-sorting); displayRowOf() is the reverse
    // lookup for the rare time a source row has to be found on screen - it builds an inverse table lazily
    // and drops it whenever the lists change.
    // @reflect ignore=true
    class RowIndex {
    public:
        // Back to showing every row in source order.
        void reset();

        // Sorts: `order` must hold every source row once. Does not touch the view - the caller re-filters.
        void setOrder(std::vector<std::uint32_t> order);
        void clearOrder();
        // Filters: `view` lists the rows to show, in display order.
        void setView(std::vector<std::uint32_t> view);
        void clearView();

        bool isSorted() const { return sorted_; }
        bool isFiltered() const { return filtered_; }

        // Rows on screen, given how many the table stores.
        std::size_t rowCount(std::size_t sourceRows) const { return filtered_ ? view_.size() : sourceRows; }
        // The stored row shown at display row d (d must be below rowCount()).
        std::size_t sourceRow(std::size_t d) const {
            return filtered_ ? view_[d] : (sorted_ ? order_[d] : d);
        }
        // The i-th stored row in sort order, ignoring the filter - the order a file is saved in.
        std::size_t orderedSourceRow(std::size_t i) const { return sorted_ ? order_[i] : i; }
        // Where a stored row is shown, or nullopt when the filter hides it.
        std::optional<std::size_t> displayRowOf(std::size_t source, std::size_t sourceRows) const;

    private:
        std::vector<std::uint32_t> order_;
        std::vector<std::uint32_t> view_;
        bool sorted_ = false;
        bool filtered_ = false;
        mutable std::vector<std::uint32_t> inverse_;   // source -> display, kNone when hidden; empty = not built
    };

}

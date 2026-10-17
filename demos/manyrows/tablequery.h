#pragma once

#include "filterrule.h"

#include <cstdint>
#include <vector>

namespace manyrows {

    class ManyRowsModel;

    // The two algorithms behind sort and filter, reading cells through ManyRowsModel::sourceText() so edits
    // count. Both return plain index lists for RowIndex; neither changes the model.
    // @reflect ignore=true
    class TableQuery {
    public:
        // Every stored row, sorted by `column`. Starts from the model's CURRENT order and is stable, so ties
        // keep that order. Numeric columns compare as numbers, others as case-insensitive text (dates are
        // yyyy-mm-dd, which sorts correctly as text); blank cells - and text in a numeric column - go last
        // whichever way the sort runs.
        static std::vector<std::uint32_t> sortedOrder(const ManyRowsModel& model, std::size_t column, bool ascending);

        // The stored rows passing every rule, in the model's current sort order.
        static std::vector<std::uint32_t> filteredRows(const ManyRowsModel& model, const std::vector<FilterRule>& rules);
    };

}

#include "rowindex.h"

#include <utility>

namespace manyrows {

    namespace {
        constexpr std::uint32_t kNone = 0xFFFFFFFFu;
    }

    void RowIndex::reset() {
        order_.clear();
        order_.shrink_to_fit();
        view_.clear();
        view_.shrink_to_fit();
        inverse_.clear();
        inverse_.shrink_to_fit();
        sorted_ = false;
        filtered_ = false;
    }

    void RowIndex::setOrder(std::vector<std::uint32_t> order) {
        order_ = std::move(order);
        sorted_ = true;
        inverse_.clear();
    }

    void RowIndex::clearOrder() {
        order_.clear();
        order_.shrink_to_fit();
        sorted_ = false;
        inverse_.clear();
    }

    void RowIndex::setView(std::vector<std::uint32_t> view) {
        view_ = std::move(view);
        filtered_ = true;
        inverse_.clear();
    }

    void RowIndex::clearView() {
        view_.clear();
        view_.shrink_to_fit();
        filtered_ = false;
        inverse_.clear();
    }

    std::optional<std::size_t> RowIndex::displayRowOf(std::size_t source, std::size_t sourceRows) const {
        if (source >= sourceRows) {
            return std::nullopt;
        }
        if (!sorted_ && !filtered_) {
            return source;
        }
        if (inverse_.empty()) {
            inverse_.assign(sourceRows, kNone);
            std::size_t shown = rowCount(sourceRows);
            for (std::size_t d = 0; d < shown; ++d) {
                inverse_[sourceRow(d)] = std::uint32_t(d);
            }
        }
        std::uint32_t d = inverse_[source];
        if (d == kNone) {
            return std::nullopt;
        }
        return std::size_t(d);
    }

}

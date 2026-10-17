#include "manyrowsmodel.h"

#include "tablequery.h"

#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace manyrows {

    namespace {
        const char* const kItems[] = {
            "Widget", "Gadget", "Sprocket", "Bracket", "Gasket", "Flange", "Bearing", "Coupling",
            "Fastener", "Valve", "Piston", "Rotor", "Spindle", "Bushing", "Washer", "Hinge"};
        const char* const kCategories[] = {"Hardware", "Electrical", "Plumbing", "Tools", "Safety", "Fasteners"};
        const char* const kRegions[] = {"North", "South", "East", "West", "Central"};
        const char* const kStatuses[] = {"Pending", "Shipped", "Delivered", "Returned", "Cancelled"};
        const char* const kTitles[] = {"Order", "Item", "Category", "Region", "Status", "Qty", "Price", "Total", "Date", "Rating"};

        template <typename T, std::size_t N>
        constexpr std::size_t countOf(const T (&)[N]) {
            return N;
        }
    }

    ManyRowsModel::ManyRowsModel(std::size_t rows, std::uint64_t seed) : rows_(rows), seed_(seed) {}

    std::string ManyRowsModel::titleText(std::size_t column) const {
        if (generated_) {
            return column < kColumnCount ? kTitles[column] : std::string();
        }
        return column < titles_.size() ? titles_[column] : std::string();
    }

    std::string ManyRowsModel::columnTitle(std::size_t column) const {
        std::string title = titleText(column);
        if (sortKey_.has_value() && sortKey_->column == column) {
            if (title.empty()) {
                // An untitled column is painted A, B, C... by the view; the arrow needs the letters too.
                for (std::size_t n = column + 1; n > 0; n = (n - 1) / 26) {
                    title.insert(title.begin(), char('A' + (n - 1) % 26));
                }
            }
            title += sortKey_->ascending ? " \xE2\x96\xB2" : " \xE2\x96\xBC";   // black up / down pointing triangle
        }
        return title;
    }

    ManyRowsModel::ColumnType ManyRowsModel::columnType(std::size_t column) {
        switch (column) {
            case 0: case 5: return ColumnType::Integer;
            case 6: case 7: case 9: return ColumnType::Number;
            case 8: return ColumnType::Date;
            default: return ColumnType::Text;
        }
    }

    ManyRowsModel::ColumnType ManyRowsModel::columnTypeOf(std::size_t column) const {
        if (generated_) {
            return columnType(column);
        }
        return column < types_.size() ? types_[column] : ColumnType::Text;
    }

    std::string ManyRowsModel::loadedText(std::size_t row, std::size_t column) const {
        std::uint32_t first = rowFirst_[row];
        std::uint32_t cells = rowFirst_[row + 1] - first;
        if (column >= cells) {
            return std::string();
        }
        std::uint32_t index = first + std::uint32_t(column);
        std::uint32_t begin = index == 0 ? 0 : cellEnd_[index - 1];
        return arena_.substr(begin, cellEnd_[index] - begin);
    }

    void ManyRowsModel::beginLoad(std::size_t columns, std::vector<std::string> titles) {
        if (columns > kMaxColumns) {
            throw std::runtime_error("ManyRowsModel::beginLoad: more than " + std::to_string(kMaxColumns) + " columns");
        }
        generated_ = false;
        rows_ = 0;
        columns_ = columns;
        edits_.clear();
        resetView();
        arena_.clear();
        cellEnd_.clear();
        rowFirst_.assign(1, 0);
        titles_ = std::move(titles);
        types_.clear();
    }

    void ManyRowsModel::appendRow(const std::vector<std::string>& cells) {
        std::size_t count = cells.size() < columns_ ? cells.size() : columns_;
        for (std::size_t i = 0; i < count; ++i) {
            if (arena_.size() + cells[i].size() > 0xFFFFFFF0u || cellEnd_.size() >= 0xFFFFFFF0u) {
                makeBlank(0, 0);
                throw std::runtime_error("ManyRowsModel::appendRow: the table is larger than 4 GB");
            }
            arena_ += cells[i];
            cellEnd_.push_back(std::uint32_t(arena_.size()));
        }
        rowFirst_.push_back(std::uint32_t(cellEnd_.size()));
        ++rows_;
    }

    void ManyRowsModel::endLoad() {
        arena_.shrink_to_fit();
        cellEnd_.shrink_to_fit();
        rowFirst_.shrink_to_fit();
        inferColumnTypes();
        onChanged(*this);
    }

    void ManyRowsModel::inferColumnTypes() {
        // A column is numeric when every non-empty cell among the first rows parses as a number; Integer when
        // none of them has a fraction or exponent.
        constexpr std::size_t kScanRows = 1000;
        types_.assign(columns_, ColumnType::Text);
        std::size_t scan = rows_ < kScanRows ? rows_ : kScanRows;
        for (std::size_t c = 0; c < columns_; ++c) {
            bool numeric = true;
            bool integral = true;
            std::size_t seen = 0;
            for (std::size_t r = 0; r < scan && numeric; ++r) {
                std::string text = loadedText(r, c);
                if (text.empty()) {
                    continue;
                }
                ++seen;
                char* end = nullptr;
                std::strtod(text.c_str(), &end);
                if (end == text.c_str() || *end != '\0') {
                    numeric = false;
                } else if (text.find_first_of(".eE") != std::string::npos) {
                    integral = false;
                }
            }
            if (numeric && seen > 0) {
                types_[c] = integral ? ColumnType::Integer : ColumnType::Number;
            }
        }
    }

    void ManyRowsModel::adoptLoaded(ManyRowsModel& staged) {
        if (staged.generated_) {
            throw std::runtime_error("ManyRowsModel::adoptLoaded: the staged table is not a loaded one");
        }
        generated_ = false;
        rows_ = staged.rows_;
        columns_ = staged.columns_;
        edits_.clear();
        resetView();
        arena_.swap(staged.arena_);
        rowFirst_.swap(staged.rowFirst_);
        cellEnd_.swap(staged.cellEnd_);
        titles_.swap(staged.titles_);
        types_.swap(staged.types_);
        staged.makeBlank(0, 0);   // leaves the staging table valid and empty
        onChanged(*this);
    }

    void ManyRowsModel::makeBlank(std::size_t rows, std::size_t columns) {
        beginLoad(columns, {});
        std::vector<std::string> blank(columns);
        for (std::size_t r = 0; r < rows; ++r) {
            appendRow(blank);
        }
        endLoad();
    }

    void ManyRowsModel::makeGenerated(std::size_t rows) {
        generated_ = true;
        rows_ = rows;
        columns_ = kColumnCount;
        edits_.clear();
        resetView();
        arena_.clear();
        arena_.shrink_to_fit();
        cellEnd_.clear();
        cellEnd_.shrink_to_fit();
        rowFirst_.clear();
        rowFirst_.shrink_to_fit();
        titles_.clear();
        types_.clear();
        onChanged(*this);
    }

    std::uint64_t ManyRowsModel::mix(std::uint64_t value) {
        // splitmix64 finalizer: cheap, and good enough that neighbouring rows look unrelated.
        value += 0x9E3779B97F4A7C15ull;
        value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ull;
        value = (value ^ (value >> 27)) * 0x94D049BB133111EBull;
        return value ^ (value >> 31);
    }

    std::uint64_t ManyRowsModel::hashOf(std::size_t row, std::size_t column) const {
        return mix(seed_ ^ mix(std::uint64_t(row) * 16u + std::uint64_t(column)));
    }

    std::string ManyRowsModel::generate(std::size_t row, std::size_t column) const {
        char buffer[48];
        switch (column) {
            case 0:
                return std::to_string(row + 1);
            case 1:
                return kItems[hashOf(row, 1) % countOf(kItems)];
            case 2:
                return kCategories[hashOf(row, 2) % countOf(kCategories)];
            case 3:
                return kRegions[hashOf(row, 3) % countOf(kRegions)];
            case 4:
                return kStatuses[hashOf(row, 4) % countOf(kStatuses)];
            case 5:
                return std::to_string(1 + hashOf(row, 5) % 100);
            case 6: {
                double price = double(500 + hashOf(row, 6) % 49500) / 100.0;
                std::snprintf(buffer, sizeof(buffer), "%.2f", price);
                return buffer;
            }
            case 7: {
                double price = double(500 + hashOf(row, 6) % 49500) / 100.0;
                double total = price * double(1 + hashOf(row, 5) % 100);
                std::snprintf(buffer, sizeof(buffer), "%.2f", total);
                return buffer;
            }
            case 8: {
                std::uint64_t h = hashOf(row, 8);
                std::snprintf(buffer, sizeof(buffer), "%04u-%02u-%02u", unsigned(2020 + h % 6), unsigned(1 + (h >> 8) % 12),
                    unsigned(1 + (h >> 16) % 28));
                return buffer;
            }
            case 9: {
                double rating = double(10 + hashOf(row, 9) % 41) / 10.0;
                std::snprintf(buffer, sizeof(buffer), "%.1f", rating);
                return buffer;
            }
            default:
                return std::string();
        }
    }

    std::string ManyRowsModel::cellText(std::size_t row, std::size_t column) const {
        if (row >= rowCount() || column >= columns_) {
            return std::string();
        }
        return sourceText(index_.sourceRow(row), column);
    }

    std::string ManyRowsModel::sourceText(std::size_t row, std::size_t column) const {
        if (row >= rows_ || column >= columns_) {
            return std::string();
        }
        if (!edits_.empty()) {
            auto found = edits_.find(cellKey(row, column));
            if (found != edits_.end()) {
                return found->second;
            }
        }
        return generated_ ? generate(row, column) : loadedText(row, column);
    }

    std::optional<std::string> ManyRowsModel::editAt(std::size_t row, std::size_t column) const {
        if (edits_.empty() || row >= rowCount()) {
            return std::nullopt;
        }
        auto found = edits_.find(cellKey(index_.sourceRow(row), column));
        if (found == edits_.end()) {
            return std::nullopt;
        }
        return found->second;
    }

    void ManyRowsModel::applyEdits(const std::vector<CellEdit>& edits, bool forward) {
        for (const CellEdit& edit : edits) {
            if (edit.row >= rows_ || edit.column >= columns_) {
                throw std::runtime_error("ManyRowsModel::applyEdits: cell out of range");
            }
        }
        for (const CellEdit& edit : edits) {
            const std::optional<std::string>& state = forward ? edit.after : edit.before;
            if (state.has_value()) {
                edits_[cellKey(edit.row, edit.column)] = *state;
            } else {
                edits_.erase(cellKey(edit.row, edit.column));
            }
        }
        onChanged(*this);
    }

    std::any ManyRowsModel::value(const std::any& key) {
        const auto* cell = std::any_cast<std::pair<std::size_t, std::size_t>>(&key);
        if (cell == nullptr) {
            return std::any();
        }
        return cellText(cell->first, cell->second);
    }

    void ManyRowsModel::setValue(const std::any& newValue, const std::any& key) {
        const auto* cell = std::any_cast<std::pair<std::size_t, std::size_t>>(&key);
        if (cell == nullptr) {
            return;
        }
        if (cell->first >= rowCount() || cell->second >= columns_) {
            throw std::runtime_error("ManyRowsModel::setValue: cell out of range");
        }
        std::string text;
        if (const auto* asString = std::any_cast<std::string>(&newValue)) {
            text = *asString;
        } else if (const auto* asChars = std::any_cast<const char*>(&newValue)) {
            text = *asChars;
        } else {
            return;
        }
        edits_[cellKey(index_.sourceRow(cell->first), cell->second)] = std::move(text);
        onChanged(*this);
    }

    // ---- sort and filter ----

    void ManyRowsModel::resetView() {
        index_.reset();
        sortKey_.reset();
        filters_.clear();
    }

    void ManyRowsModel::sortBy(std::size_t column, bool ascending) {
        if (column >= columns_) {
            throw std::runtime_error("ManyRowsModel::sortBy: column out of range");
        }
        index_.setOrder(TableQuery::sortedOrder(*this, column, ascending));
        sortKey_ = SortKey{column, ascending};
        applyFilters();   // the visible rows must follow the new order
        onChanged(*this);
    }

    void ManyRowsModel::clearSort() {
        if (!sortKey_.has_value()) {
            return;
        }
        index_.clearOrder();
        sortKey_.reset();
        applyFilters();
        onChanged(*this);
    }

    void ManyRowsModel::setFilters(std::vector<FilterRule> rules) {
        for (const FilterRule& rule : rules) {
            if (rule.column >= columns_) {
                throw std::runtime_error("ManyRowsModel::setFilters: column out of range");
            }
        }
        filters_ = std::move(rules);
        applyFilters();
        onChanged(*this);
    }

    // Recomputes the visible rows from filters_ in the current order (no event).
    void ManyRowsModel::applyFilters() {
        if (filters_.empty()) {
            index_.clearView();
        } else {
            index_.setView(TableQuery::filteredRows(*this, filters_));
        }
    }

}

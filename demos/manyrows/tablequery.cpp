#include "tablequery.h"

#include "manyrowsmodel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

namespace manyrows {

    namespace {
        bool isNumeric(ManyRowsModel::ColumnType type) {
            return type == ManyRowsModel::ColumnType::Integer || type == ManyRowsModel::ColumnType::Number;
        }

        double numberOf(const std::string& text) {
            if (text.empty()) {
                return std::numeric_limits<double>::quiet_NaN();
            }
            char* end = nullptr;
            double value = std::strtod(text.c_str(), &end);
            return (end == text.c_str() || *end != '\0') ? std::numeric_limits<double>::quiet_NaN() : value;
        }

        std::string folded(std::string text) {
            for (char& c : text) {
                c = char(std::tolower((unsigned char)c));
            }
            return text;
        }
    }

    std::vector<std::uint32_t> TableQuery::sortedOrder(const ManyRowsModel& model, std::size_t column, bool ascending) {
        const std::size_t rows = model.sourceRowCount();
        std::vector<std::uint32_t> order(rows);
        for (std::size_t i = 0; i < rows; ++i) {
            order[i] = std::uint32_t(model.orderedSourceRow(i));   // the current order is the starting point
        }
        // The keys are read once, by stored row, so the comparisons below are plain loads.
        if (isNumeric(model.columnTypeOf(column))) {
            std::vector<double> key(rows);
            for (std::size_t r = 0; r < rows; ++r) {
                key[r] = numberOf(model.sourceText(r, column));
            }
            std::stable_sort(order.begin(), order.end(), [&key, ascending](std::uint32_t a, std::uint32_t b) {
                bool blankA = std::isnan(key[a]);
                bool blankB = std::isnan(key[b]);
                if (blankA || blankB) {
                    return !blankA && blankB;   // blanks last in both directions
                }
                return ascending ? key[a] < key[b] : key[a] > key[b];
            });
        } else {
            std::vector<std::string> key(rows);
            for (std::size_t r = 0; r < rows; ++r) {
                key[r] = folded(model.sourceText(r, column));
            }
            std::stable_sort(order.begin(), order.end(), [&key, ascending](std::uint32_t a, std::uint32_t b) {
                bool blankA = key[a].empty();
                bool blankB = key[b].empty();
                if (blankA || blankB) {
                    return !blankA && blankB;
                }
                return ascending ? key[a] < key[b] : key[a] > key[b];
            });
        }
        return order;
    }

    std::vector<std::uint32_t> TableQuery::filteredRows(const ManyRowsModel& model, const std::vector<FilterRule>& rules) {
        std::vector<bool> numeric;
        for (const FilterRule& rule : rules) {
            numeric.push_back(isNumeric(model.columnTypeOf(rule.column)));
        }
        std::vector<std::uint32_t> shown;
        const std::size_t rows = model.sourceRowCount();
        for (std::size_t i = 0; i < rows; ++i) {
            std::size_t row = model.orderedSourceRow(i);
            bool passes = true;
            for (std::size_t r = 0; r < rules.size() && passes; ++r) {
                passes = rules[r].matches(model.sourceText(row, rules[r].column), numeric[r]);
            }
            if (passes) {
                shown.push_back(std::uint32_t(row));
            }
        }
        return shown;
    }

}

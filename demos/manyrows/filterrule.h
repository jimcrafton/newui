#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace manyrows {

    // One condition of a filter: "<column> <operator> <value>", e.g. Region = North, Qty > 50, Item contains "ing".
    // A filter is a list of these and a row is shown only when it satisfies ALL of them.
    //
    // Text is compared case-insensitively. In a numeric column (the caller says so) the comparison is numeric
    // whenever both the cell and the value are numbers ("5" equals "5.00"), else it falls back to text order.
    // A blank cell never satisfies <, <=, >, >=; it satisfies = only for a blank value, and != for anything else.
    // @reflect ignore=true
    struct FilterRule {
        enum class Op { Equals, NotEquals, Contains, NotContains, Greater, GreaterOrEqual, Less, LessOrEqual, Blank, NotBlank };

        std::size_t column = 0;
        Op op = Op::Equals;
        std::string value;   // ignored by Blank / NotBlank

        FilterRule() {}
        FilterRule(std::size_t column, Op op, std::string value = std::string()) : column(column), op(op), value(std::move(value)) {}

        // Whether a cell showing `text` passes. `numeric` is the column's kind (compare as numbers if possible).
        bool matches(const std::string& text, bool numeric) const;

        bool operator==(const FilterRule& other) const { return column == other.column && op == other.op && value == other.value; }

        // The operator as the user reads it ("=", "contains", "is blank"...) and the whole rule: "Region = North".
        static const char* opText(Op op);
        std::string describe(const std::string& columnName) const;
        // True for the operators that take a value.
        static bool needsValue(Op op) { return op != Op::Blank && op != Op::NotBlank; }
    };

}

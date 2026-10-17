#include "filterrule.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

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

        std::string folded(const std::string& text) {
            std::string out = text;
            for (char& c : out) {
                c = char(std::tolower((unsigned char)c));
            }
            return out;
        }

        // True with `out` set when the whole text is a number.
        bool parseNumber(const std::string& text, double& out) {
            std::string t = trimmed(text);
            if (t.empty()) {
                return false;
            }
            char* end = nullptr;
            out = std::strtod(t.c_str(), &end);
            return end != t.c_str() && *end == '\0' && !std::isnan(out);
        }

        // <0, 0, >0 like strcmp: numbers when both are (and the column is numeric), else case-insensitive text.
        int compare(const std::string& a, const std::string& b, bool numeric) {
            double x = 0.0;
            double y = 0.0;
            if (numeric && parseNumber(a, x) && parseNumber(b, y)) {
                return x < y ? -1 : (x > y ? 1 : 0);
            }
            return folded(trimmed(a)).compare(folded(trimmed(b)));
        }
    }

    bool FilterRule::matches(const std::string& text, bool numeric) const {
        switch (op) {
            case Op::Blank:
                return trimmed(text).empty();
            case Op::NotBlank:
                return !trimmed(text).empty();
            case Op::Contains:
                return folded(text).find(folded(value)) != std::string::npos;
            case Op::NotContains:
                return folded(text).find(folded(value)) == std::string::npos;
            default:
                break;
        }
        bool blank = trimmed(text).empty();
        if (op == Op::Equals) {
            return compare(text, value, numeric) == 0;
        }
        if (op == Op::NotEquals) {
            return compare(text, value, numeric) != 0;
        }
        if (blank) {
            return false;   // blanks are not "greater" or "less" than anything
        }
        int order = compare(text, value, numeric);
        switch (op) {
            case Op::Greater: return order > 0;
            case Op::GreaterOrEqual: return order >= 0;
            case Op::Less: return order < 0;
            default: return order <= 0;   // LessOrEqual
        }
    }

    const char* FilterRule::opText(Op op) {
        switch (op) {
            case Op::Equals: return "=";
            case Op::NotEquals: return "<>";
            case Op::Contains: return "contains";
            case Op::NotContains: return "does not contain";
            case Op::Greater: return ">";
            case Op::GreaterOrEqual: return ">=";
            case Op::Less: return "<";
            case Op::LessOrEqual: return "<=";
            case Op::Blank: return "is blank";
            case Op::NotBlank: return "is not blank";
        }
        return "";
    }

    std::string FilterRule::describe(const std::string& columnName) const {
        std::string text = columnName + " " + opText(op);
        if (needsValue(op)) {
            text += " " + value;
        }
        return text;
    }

}

#include "newui/displayunit.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>

namespace newui {

    namespace {

        // Classic Win32 dialog unit divisors (same convention MapDialogRect
        // has always used): 1 DLU horizontally is 1/4 of the dialog font's
        // average character width, 1 DLU vertically is 1/8 of its line height.
        constexpr float kDluDivisorX = 4.0f;
        constexpr float kDluDivisorY = 8.0f;

        bool equalsIgnoreCase(const std::string& a, const char* b) {
            size_t i = 0;
            for (; i < a.size() && b[i] != '\0'; ++i) {
                char ca = static_cast<char>(std::tolower(static_cast<unsigned char>(a[i])));
                if (ca != b[i]) {
                    return false;
                }
            }
            return i == a.size() && b[i] == '\0';
        }

    }

    float DisplayMetrics::toPixelsX(float value, DisplayUnit unit) const {
        switch (unit) {
            case DisplayUnit::Px: return value;
            case DisplayUnit::Dip: return value * dpiScale_;
            case DisplayUnit::Dlu: return value * duBaseX_ / kDluDivisorX;
        }
        return value;
    }

    float DisplayMetrics::toPixelsY(float value, DisplayUnit unit) const {
        switch (unit) {
            case DisplayUnit::Px: return value;
            case DisplayUnit::Dip: return value * dpiScale_;
            case DisplayUnit::Dlu: return value * duBaseY_ / kDluDivisorY;
        }
        return value;
    }

    bool DisplayValue::fromString(const std::string& str, DisplayValue& out) {
        size_t start = str.find_first_not_of(" \t");
        if (start == std::string::npos) {
            return false;
        }
        size_t end = str.find_last_not_of(" \t");
        std::string trimmed = str.substr(start, end - start + 1);

        const char* cstr = trimmed.c_str();
        char* numEnd = nullptr;
        float value = std::strtof(cstr, &numEnd);
        if (numEnd == cstr) {
            return false;  // no digits at all
        }

        std::string rest(numEnd);
        size_t suffixStart = rest.find_first_not_of(" \t");
        std::string suffix = (suffixStart == std::string::npos) ? std::string() : rest.substr(suffixStart);

        DisplayUnit unit;
        if (suffix.empty() || equalsIgnoreCase(suffix, "px")) {
            unit = DisplayUnit::Px;
        } else if (equalsIgnoreCase(suffix, "dip")) {
            unit = DisplayUnit::Dip;
        } else if (equalsIgnoreCase(suffix, "du")) {
            unit = DisplayUnit::Dlu;
        } else {
            return false;  // unrecognized suffix - no silent partial parse
        }

        out = DisplayValue(value, unit);
        return true;
    }

    std::string DisplayValue::toString() const {
        const char* suffix = "px";
        switch (unit_) {
            case DisplayUnit::Px: suffix = "px"; break;
            case DisplayUnit::Dip: suffix = "dip"; break;
            case DisplayUnit::Dlu: suffix = "du"; break;
        }

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%g%s", value_, suffix);
        return std::string(buf);
    }

}

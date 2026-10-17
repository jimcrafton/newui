#pragma once

#include <cstdio>
#include <string>

namespace manyrows {

    // Small number-to-text helpers the status bar and progress text share.

    // 1234567 -> "1,234,567".
    inline std::string withCommas(std::size_t value) {
        std::string digits = std::to_string(value);
        for (std::ptrdiff_t i = std::ptrdiff_t(digits.size()) - 3; i > 0; i -= 3) {
            digits.insert(std::size_t(i), ",");
        }
        return digits;
    }

    // 1.84 -> "1.8" (a time in milliseconds).
    inline std::string millis(double ms) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%.1f", ms);
        return buffer;
    }

}

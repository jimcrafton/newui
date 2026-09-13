#include "newui/color.h"
#include "newui/uicolormanager.h"

namespace newui {

    // Out-of-line (unlike every other Color parser in color.h) specifically
    // to avoid a circular #include - see this function's own declaration
    // comment (color.h).
    bool Color::fromUIColorRoleName(const std::string& str, Color& outColor) noexcept {
        static const struct { const char* name; UIColorRole role; } kRoles[] = {
            {"WindowBackground", UIColorRole::WindowBackground},
            {"WindowText", UIColorRole::WindowText},
            {"ControlBackground", UIColorRole::ControlBackground},
            {"ControlText", UIColorRole::ControlText},
            {"ControlBorder", UIColorRole::ControlBorder},
            {"DisabledText", UIColorRole::DisabledText},
            {"HighlightBackground", UIColorRole::HighlightBackground},
            {"HighlightText", UIColorRole::HighlightText},
            {"LinkText", UIColorRole::LinkText},
            {"LinkHoverText", UIColorRole::LinkHoverText},
        };

        for (const auto& entry : kRoles) {
            if (Color::compareNameCaseInsensitive(str, entry.name) == 0) {
                outColor = UIColorManager::colorFor(entry.role);
                return true;
            }
        }

        return false;
    }

}

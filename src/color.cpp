#include "newui/color.h"
#include "newui/uicolormanager.h"
#include "newui/platform.h"

#if NEWUI_PLATFORM_WIN32
#include <windows.h>
#endif

namespace newui {

#if NEWUI_PLATFORM_WIN32
    namespace {

        // Translates our own SystemColor numbering (see color_constants.h) to the Win32 COLOR_*
        // constant it corresponds to, for fromSystemColor() to pass to GetSysColor().
        int toWin32SysColorIndex(SystemColor color) noexcept {
            switch (color) {
                case SystemColor::ScrollBar: return COLOR_SCROLLBAR;
                case SystemColor::Desktop: return COLOR_DESKTOP;
                case SystemColor::ActiveCaption: return COLOR_ACTIVECAPTION;
                case SystemColor::InactiveCaption: return COLOR_INACTIVECAPTION;
                case SystemColor::Menu: return COLOR_MENU;
                case SystemColor::Window: return COLOR_WINDOW;
                case SystemColor::WindowFrame: return COLOR_WINDOWFRAME;
                case SystemColor::MenuText: return COLOR_MENUTEXT;
                case SystemColor::WindowText: return COLOR_WINDOWTEXT;
                case SystemColor::CaptionText: return COLOR_CAPTIONTEXT;
                case SystemColor::ActiveBorder: return COLOR_ACTIVEBORDER;
                case SystemColor::InactiveBorder: return COLOR_INACTIVEBORDER;
                case SystemColor::AppWorkspace: return COLOR_APPWORKSPACE;
                case SystemColor::Highlight: return COLOR_HIGHLIGHT;
                case SystemColor::HighlightText: return COLOR_HIGHLIGHTTEXT;
                case SystemColor::ButtonFace: return COLOR_BTNFACE;
                case SystemColor::ButtonShadow: return COLOR_BTNSHADOW;
                case SystemColor::GrayText: return COLOR_GRAYTEXT;
                case SystemColor::ButtonText: return COLOR_BTNTEXT;
                case SystemColor::InactiveCaptionText: return COLOR_INACTIVECAPTIONTEXT;
                case SystemColor::ButtonHighlight: return COLOR_BTNHIGHLIGHT;
                case SystemColor::DarkShadow3D: return COLOR_3DDKSHADOW;
                case SystemColor::Light3D: return COLOR_3DLIGHT;
                case SystemColor::InfoText: return COLOR_INFOTEXT;
                case SystemColor::InfoBackground: return COLOR_INFOBK;
                case SystemColor::Hotlight: return COLOR_HOTLIGHT;
                case SystemColor::GradientActiveCaption: return COLOR_GRADIENTACTIVECAPTION;
                case SystemColor::GradientInactiveCaption: return COLOR_GRADIENTINACTIVECAPTION;
                case SystemColor::MenuHighlight: return COLOR_MENUHILIGHT;
                case SystemColor::MenuBar: return COLOR_MENUBAR;
            }
            return COLOR_WINDOW;  // unreachable for a valid SystemColor
        }

    }

    Color Color::fromSystemColor(SystemColor color) noexcept {
        COLORREF ref = ::GetSysColor(toWin32SysColorIndex(color));
        return Color(
            float(GetRValue(ref)) / 255.0f,
            float(GetGValue(ref)) / 255.0f,
            float(GetBValue(ref)) / 255.0f,
            1.0f);
    }
#endif // NEWUI_PLATFORM_WIN32

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

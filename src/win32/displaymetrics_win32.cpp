// DisplayMetrics::forWindow()/forDpi(): the parts that need the window's DPI and the system dialog font.
// (forDpi() moves to src/displayunit.cpp once FontManager exists on every platform.)
#include "newui/displayunit.h"

#include "newui/font.h"
#include "newui/fontmanager.h"
#include "newui/win32/nativehandles.h"

namespace newui {

    namespace {

        // The reference string Microsoft's own documented algorithm uses to
        // compute a dialog font's average character width: the 52 upper-
        // and lower-case Latin letters, divided evenly.
        constexpr char kAverageWidthProbe[] =
            "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        constexpr float kAverageWidthProbeLength = 52.0f;

        // Fallback Dlu base metrics (already DPI-scaled) used only if the
        // dialog font somehow fails to resolve/measure - keeps toPixelsX/Y
        // from silently collapsing every Dlu value to 0 in that edge case.
        // Roughly Segoe UI 9pt's own average-char-width/line-height at 96 DPI.
        constexpr float kFallbackDuBaseX96 = 6.0f;
        constexpr float kFallbackDuBaseY96 = 13.0f;

    }

    DisplayMetrics DisplayMetrics::forWindow(NativeWindowHandle handle) {
        HWND hwnd = newui::win32::toHWND(handle);
        unsigned dpi = kBaselineDpi;
        if (hwnd != nullptr) {
            UINT hwndDpi = ::GetDpiForWindow(hwnd);
            if (hwndDpi != 0) {
                dpi = hwndDpi;
            }
        }
        return forDpi(dpi);
    }

    DisplayMetrics DisplayMetrics::forDpi(unsigned dpi) {
        DisplayMetrics metrics;
        metrics.dpiScale_ = static_cast<float>(dpi) / static_cast<float>(kBaselineDpi);

        Font dialogFont = FontManager::getSystemFont(SystemUIFont::Message);
        Font scaledFont(dialogFont.name(), dialogFont.size() * metrics.dpiScale_);
        scaledFont.setBold(dialogFont.bold());
        scaledFont.setItalic(dialogFont.italic());

        TextMetrics tm = scaledFont.measureText(kAverageWidthProbe);
        if (tm.width > 0.0f) {
            metrics.duBaseX_ = tm.width / kAverageWidthProbeLength;
            metrics.duBaseY_ = tm.ascent + tm.descent;
        } else {
            metrics.duBaseX_ = kFallbackDuBaseX96 * metrics.dpiScale_;
            metrics.duBaseY_ = kFallbackDuBaseY96 * metrics.dpiScale_;
        }

        return metrics;
    }

}

#pragma once

#include <string>

#include "newui/geometry.h"
#include "newui/newui.h"

namespace newui {

    // Windows' baseline DPI - "100% scaling", the reference point every DPI
    // ratio (including DisplayMetrics::dpiScale()) is computed against.
    constexpr unsigned kBaselineDpi = 96;

    // What a DisplayValue's number means. See DisplayValue's own comment for
    // the full design rationale (display-units-plan.md at the repo root has
    // the original design discussion).
    enum class DisplayUnit {
        Px,   // raw device pixels - explicit escape hatch, never scaled
        Dip,  // device-independent pixel, 1/96 inch - scales with DPI only
        Dlu,  // dialog unit - scales with the current UI font (classic Win32 DLU convention)
    };

    // Everything needed to resolve a DisplayValue to real pixels, for one
    // window/monitor. A plain value type (no HWND stored) so it's
    // constructible in tests without a real window - see forDpi(). Not free
    // to build (forWindow() loads and measures a font), which is why
    // RootView caches one instance per window rather than every call site
    // building its own - see RootView::displayMetrics()/View::displayMetrics().
    //
    // Dlu conversion needs separate X/Y base measurements (a dialog font's
    // average character width and its line height are different numbers,
    // and the classic DLU formula divides each by a different constant - 4
    // horizontally, 8 vertically) - hence toPixelsX()/toPixelsY() instead of
    // one axis-agnostic toPixels(). For Px/Dip, both axes produce the same
    // result; only Dlu actually differs by axis.
    class DisplayMetrics {
    public:
        // GetDpiForWindow(hwnd), then SystemUIFont::Message loaded at that
        // DPI's scaled point size, measured for the Dlu base metrics. hwnd
        // may be nullptr (e.g. a RootView with no HWND yet) - treated the
        // same as forDpi(96).
        static DisplayMetrics forWindow(HWND hwnd);

        // Synthesizes metrics for a given DPI without touching a real
        // window - for unit tests (forDpi(192) to check 2x scaling math)
        // and as View::displayMetrics()'s fallback for an unparented View.
        static DisplayMetrics forDpi(unsigned dpi);

        float dpiScale() const { return dpiScale_; }  // dpi / 96.0f

        float toPixelsX(float value, DisplayUnit unit) const;
        float toPixelsY(float value, DisplayUnit unit) const;

    private:
        float dpiScale_ = 1.0f;
        float duBaseX_ = 0.0f;  // avg dialog-font character width, physical px at this DPI
        float duBaseY_ = 0.0f;  // dialog-font line height, physical px at this DPI
    };

    // One dimension's worth of "a size meant in some unit" - what a call
    // site writes instead of a bare float pixel literal, and what a .newui
    // file/PropertiesGrid reads and writes as a plain string (fromString()/
    // toString() below) instead of a bare number.
    //
    // Deliberately NOT a stored, live-reactive property anywhere in the
    // View/Rect model - bounds() etc. stay plain pixel Rects. This is a
    // resolve-at-point-of-use value: build one, call toPixelsX()/toPixelsY()
    // against a DisplayMetrics whenever pixels are actually needed (typically
    // every layout/paint pass, same as any other constant used there).
    class DisplayValue {
    public:
        constexpr DisplayValue(float value = 0.0f, DisplayUnit unit = DisplayUnit::Px)
            : value_(value), unit_(unit) {}

        float value() const { return value_; }
        DisplayUnit unit() const { return unit_; }

        float toPixelsX(const DisplayMetrics& metrics) const { return metrics.toPixelsX(value_, unit_); }
        float toPixelsY(const DisplayMetrics& metrics) const { return metrics.toPixelsY(value_, unit_); }

        // Grammar: an ordinary float (whatever strtof accepts) followed by
        // an optional unit suffix, case-insensitive, one of "px"/"dip"/"du"
        // (Dlu's suffix - "display unit"/dialog unit). Optional whitespace
        // is allowed between the number and the suffix. No suffix means Px -
        // an unadorned number means exactly what every existing pixel
        // literal already means today, so an old or hand-written value
        // never silently changes meaning.
        //
        // Returns false (out left untouched) for anything that isn't a
        // float optionally followed by exactly one of the three known
        // suffixes - no silent partial parse.
        static bool fromString(const std::string& str, DisplayValue& out);

        // Always emits an explicit suffix, including "px" for
        // DisplayUnit::Px (never a bare number) - the no-suffix form
        // fromString() accepts is a parse-time convenience for old/
        // hand-written values, not this class's own canonical output, so a
        // value written by toString() and read back by fromString() always
        // round-trips exactly regardless of unit.
        std::string toString() const;

        bool operator==(const DisplayValue& other) const {
            return value_ == other.value_ && unit_ == other.unit_;
        }

        bool operator!=(const DisplayValue& other) const { return !(*this == other); }

    private:
        float value_;
        DisplayUnit unit_;
    };

    struct DisplayPoint {
        DisplayValue x;
        DisplayValue y;

        Point toPixels(const DisplayMetrics& metrics) const {
            return Point(x.toPixelsX(metrics), y.toPixelsY(metrics));
        }
    };

    struct DisplaySize {
        DisplayValue width;
        DisplayValue height;

        Size toPixels(const DisplayMetrics& metrics) const {
            return Size(width.toPixelsX(metrics), height.toPixelsY(metrics));
        }
    };

}

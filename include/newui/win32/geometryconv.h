#pragma once

// Windows-only conversions between newui's geometry value types and POINT/RECT.
// These used to be implicit constructors/operators on Point and Rect; they are explicit
// free functions now so geometry.h carries no Win32 types.

#include "newui/geometry.h"
#include "newui/platform.h"

#if !NEWUI_PLATFORM_WIN32
#   error "newui/win32/geometryconv.h is Windows-only"
#endif

#include <windows.h>

namespace newui {
    namespace win32 {

        inline Point fromPOINT(const POINT& pt) { return Point(float(pt.x), float(pt.y)); }

        inline POINT toPOINT(const Point& pt) { return POINT{ LONG(pt.x), LONG(pt.y) }; }

        // RECT::right/bottom are exclusive edges, matching Rect::right()/bottom().
        inline Rect fromRECT(const RECT& r) {
            return Rect(float(r.left), float(r.top), float(r.right - r.left), float(r.bottom - r.top));
        }

        // Truncates every edge; use Rect::snappedOutwardToPixels() first when invalidating a region.
        inline RECT toRECT(const Rect& r) {
            return RECT{ LONG(r.left()), LONG(r.top()), LONG(r.right()), LONG(r.bottom()) };
        }

    } // namespace win32
} // namespace newui

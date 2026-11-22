#pragma once

// Windows-only conversions between newui's opaque handles and Win32 handles.
// Include from src/win32/ or Windows-only code; this header pulls in <windows.h>.

#include "newui/platform.h"

#if !NEWUI_PLATFORM_WIN32
#   error "newui/win32/nativehandles.h is Windows-only"
#endif

// Same macro setup newui.h uses; must precede <windows.h> whichever header gets included first.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>

namespace newui {
    namespace win32 {

        inline NativeWindowHandle toNativeHandle(HWND hwnd) { return static_cast<NativeWindowHandle>(hwnd); }
        inline HWND toHWND(NativeWindowHandle handle) { return static_cast<HWND>(handle); }

        inline NativeCursorHandle toNativeCursor(HCURSOR cursor) { return static_cast<NativeCursorHandle>(cursor); }
        inline HCURSOR toHCURSOR(NativeCursorHandle handle) { return static_cast<HCURSOR>(handle); }

        inline NativeMenuHandle toNativeMenu(HMENU menu) { return static_cast<NativeMenuHandle>(menu); }
        inline HMENU toHMENU(NativeMenuHandle handle) { return static_cast<HMENU>(handle); }

        inline NativeInstanceHandle toNativeInstance(HINSTANCE instance) { return static_cast<NativeInstanceHandle>(instance); }
        inline HINSTANCE toHINSTANCE(NativeInstanceHandle handle) { return static_cast<HINSTANCE>(handle); }

        inline NativeBitmapHandle toNativeBitmap(HBITMAP bitmap) { return static_cast<NativeBitmapHandle>(bitmap); }
        inline HBITMAP toHBITMAP(NativeBitmapHandle handle) { return static_cast<HBITMAP>(handle); }

    } // namespace win32
} // namespace newui

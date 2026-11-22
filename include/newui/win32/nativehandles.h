#pragma once

// Windows-only conversions between newui's opaque handles and Win32 handles.
// Include from src/win32/ or Windows-only code; this header pulls in <windows.h>.

#include "newui/platform.h"

#if !NEWUI_PLATFORM_WIN32
#   error "newui/win32/nativehandles.h is Windows-only"
#endif

#include <windows.h>

namespace newui {
    namespace win32 {

        inline NativeWindow toNativeWindow(HWND hwnd) { return reinterpret_cast<NativeWindow>(hwnd); }
        inline NativeView toNativeView(HWND hwnd) { return reinterpret_cast<NativeView>(hwnd); }
        inline HWND toHWND(NativeWindow window) { return reinterpret_cast<HWND>(window); }
        inline HWND toHWND(NativeView view) { return reinterpret_cast<HWND>(view); }

    } // namespace win32
} // namespace newui

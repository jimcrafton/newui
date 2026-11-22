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

        inline NativeWindowHandle toNativeHandle(HWND hwnd) { return static_cast<NativeWindowHandle>(hwnd); }
        inline HWND toHWND(NativeWindowHandle handle) { return static_cast<HWND>(handle); }

    } // namespace win32
} // namespace newui

#pragma once

// Platform detection and opaque native handle types.
//
// Detection comes from the compiler's own predefined macros, never from CMake -D flags, so the build and
// reflectgen's separate libclang parse always agree (libclang only needs the right target and sysroot).
//
// This header must stay free of any platform SDK header (no <windows.h>, no Objective-C). Exactly one of
// NEWUI_PLATFORM_WIN32 / NEWUI_PLATFORM_MACOS / NEWUI_PLATFORM_IOS is defined to 1; the others are 0.

#if defined(_WIN32)
#   define NEWUI_PLATFORM_WIN32 1
#   define NEWUI_PLATFORM_MACOS 0
#   define NEWUI_PLATFORM_IOS   0
#elif defined(__APPLE__)
#   include <TargetConditionals.h>
#   define NEWUI_PLATFORM_WIN32 0
#   if TARGET_OS_IOS
#       define NEWUI_PLATFORM_MACOS 0
#       define NEWUI_PLATFORM_IOS   1
#   elif TARGET_OS_OSX
#       define NEWUI_PLATFORM_MACOS 1
#       define NEWUI_PLATFORM_IOS   0
#   else
#       error "Unsupported Apple platform"
#   endif
#else
#   error "Unsupported platform: newui targets Windows, macOS and iOS"
#endif

#define NEWUI_PLATFORM_APPLE (NEWUI_PLATFORM_MACOS || NEWUI_PLATFORM_IOS)

namespace newui {

    // Opaque native window/view handle. A plain void* so public headers can pass it around without seeing
    // the platform SDK. Platform code converts at the boundary:
    //   Windows: HWND        (see newui/win32/nativehandles.h)
    //   macOS:   NSWindow* / NSView*  (bridge with (__bridge void*))
    //   iOS:     UIWindow* / UIView*
    // One type covers both windows and views; the compiler will not catch mixing them up.
    using NativeWindowHandle = void*;

    // Opaque native cursor handle: HCURSOR on Windows (see newui/win32/nativehandles.h), NSCursor* on macOS.
    // Null means "nothing to show".
    using NativeCursorHandle = void*;

    // Opaque native popup-menu handle: HMENU on Windows, NSMenu* on macOS. Null means "no menu built".
    using NativeMenuHandle = void*;

    // Opaque application/module instance: HINSTANCE on Windows. Other platforms have no equivalent; pass nullptr.
    using NativeInstanceHandle = void*;

    // Opaque native bitmap handle: HBITMAP on Windows. Not owned by whoever holds it unless a class says so.
    using NativeBitmapHandle = void*;

    // Opaque native drawing-context handle: HDC on Windows (CGContextRef on macOS). Not owned by whoever holds it
    // unless a class says so.
    using NativeDeviceContextHandle = void*;

} // namespace newui

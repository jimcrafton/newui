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

    // Opaque native handles. They are pointers to incomplete tag types, so public headers can pass them
    // around without seeing the platform SDK. Platform code converts at the boundary:
    //   Windows: HWND        (see newui/win32/nativehandles.h)
    //   macOS:   NSWindow* / NSView*
    //   iOS:     UIWindow*  / UIView*
    struct NativeWindowTag;
    struct NativeViewTag;
    using NativeWindow = NativeWindowTag*;
    using NativeView = NativeViewTag*;

} // namespace newui

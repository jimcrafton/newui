#pragma once

// Windows SDK setup: the macros that must precede <windows.h>, then the SDK headers the Win32 backend uses.
// Only Windows-specific code (src/*.cpp that call Win32, include/newui/win32/*, Windows tests/examples) includes
// this; public cross-platform headers must not. Empty on other platforms, so shared tests can include it harmlessly.

#if defined(_WIN32)

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
#include <windowsx.h>
#include <commctrl.h>
#include <ole2.h>

#endif // _WIN32

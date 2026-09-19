#pragma once

// Shared by test_gdipresentsurface.cpp / test_dxgipresentsurface.cpp.

#include <windows.h>

namespace presenttest {


    // A real STATIC control with a client area for InvalidateRect()/GetUpdateRect() to act
    // on. Has to be *visible* - InvalidateRect() on a hidden window accumulates no update
    // region at all - but is fully transparent, click-through and off the taskbar/activation
    // order (layered, alpha 0), so it never shows or steals focus while the test runs.
    struct ScopedWindow {
        HWND hwnd = ::CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            L"STATIC", L"", WS_POPUP | WS_VISIBLE, 0, 0, 100, 100,
            nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);

        ScopedWindow() {
            if (hwnd) {
                ::SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA);
            }
        }

        ~ScopedWindow() {
            if (hwnd) {
                ::DestroyWindow(hwnd);
            }
        }
    };

    // A memory DC + 32bpp bitmap standing in for a window's paint DC, prefilled
    // solid blue so "paint() didn't touch this" is distinguishable from
    // "paint() copied black".
    struct ScopedTargetDc {
        HDC dc = ::CreateCompatibleDC(nullptr);
        HBITMAP bmp = nullptr;
        HGDIOBJ old = nullptr;

        ScopedTargetDc(int w, int h) {
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = w;
            bmi.bmiHeader.biHeight = -h;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            void* bits = nullptr;
            bmp = ::CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            old = ::SelectObject(dc, bmp);
            HBRUSH blue = ::CreateSolidBrush(RGB(0, 0, 255));
            RECT r = { 0, 0, w, h };
            ::FillRect(dc, &r, blue);
            ::DeleteObject(blue);
        }

        ~ScopedTargetDc() {
            ::SelectObject(dc, old);
            ::DeleteObject(bmp);
            ::DeleteDC(dc);
        }
    };

// A real but hidden, non-layered window - what a DXGI swap chain can attach to.
struct ScopedPlainWindow {
    HWND hwnd = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 100, 100,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);

    ~ScopedPlainWindow() {
        if (hwnd) {
            ::DestroyWindow(hwnd);
        }
    }
};

}

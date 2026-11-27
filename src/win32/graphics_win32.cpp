// gfx::Image's Windows backing: a DIB section Blend2D draws into, and the memory DC GDI draws through.
#include "newui/graphics.h"
#include "newui/win32/nativehandles.h"

#include <cstring>

namespace newui::gfx {

    bool Image::createDibBackedImage(int width, int height, const void* copySrc, intptr_t copySrcStride) {
        if (width <= 0 || height <= 0) {
            return false;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;  // top-down, matches BLImage's own row order (rootview.cpp/cursor.cpp)
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP dib = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (dib == nullptr) {
            return false;
        }

        const size_t dstStride = size_t(width) * 4;
        if (copySrc != nullptr) {
            const uint8_t* src = static_cast<const uint8_t*>(copySrc);
            uint8_t* dst = static_cast<uint8_t*>(bits);
            for (int y = 0; y < height; ++y) {
                std::memcpy(dst + size_t(y) * dstStride, src + size_t(y) * size_t(copySrcStride), dstStride);
            }
        } else {
            std::memset(bits, 0, dstStride * size_t(height));
        }

        image_.create_from_data(width, height, BL_FORMAT_PRGB32, bits, intptr_t(dstStride));
        dibSection_ = newui::win32::toNativeBitmap(dib);
        return true;
    }

    void Image::releaseGdiResources() {
        if (memDC_ != nullptr) {
            ::SelectObject(newui::win32::toHDC(memDC_), newui::win32::toHBITMAP(oldBitmap_));
            ::DeleteDC(newui::win32::toHDC(memDC_));
            memDC_ = nullptr;
            oldBitmap_ = nullptr;
        }
        if (dibSection_ != nullptr) {
            ::DeleteObject(newui::win32::toHBITMAP(dibSection_));
            dibSection_ = nullptr;
        }
        image_.reset();
    }

    NativeDeviceContextHandle Image::memDC() {
        if (!isValid()) {
            return nullptr;
        }

        if (memDC_ == nullptr) {
            memDC_ = newui::win32::toNativeDC(::CreateCompatibleDC(nullptr));
            if (memDC_ == nullptr) {
                return nullptr;
            }
            oldBitmap_ = newui::win32::toNativeBitmap(static_cast<HBITMAP>(::SelectObject(newui::win32::toHDC(memDC_), newui::win32::toHBITMAP(dibSection_))));
        }

        return memDC_;
    }

}

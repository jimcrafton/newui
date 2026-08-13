#include "newui/graphics.h"

#include <cstring>
#include <utility>

namespace newui {

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
        dibSection_ = dib;
        return true;
    }

    Image::Image(int width, int height) {
        createDibBackedImage(width, height);
    }

    Image::Image(const std::string& path) {
        BLImage decoded;
        if (decoded.read_from_file(path.c_str()) != BL_SUCCESS) {
            return;
        }
        if (decoded.format() != BL_FORMAT_PRGB32 && decoded.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return;
        }

        BLImageData data;
        decoded.get_data(&data);
        createDibBackedImage(data.size.w, data.size.h, data.pixel_data, data.stride);
    }

    Image::Image(const BLImage& source) {
        BLImage converted = source;  // BLImage's own copy is cheap/COW - convert() below needs a non-const instance
        if (converted.format() != BL_FORMAT_PRGB32 && converted.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return;
        }

        BLImageData data;
        converted.get_data(&data);
        createDibBackedImage(data.size.w, data.size.h, data.pixel_data, data.stride);
    }

    void Image::releaseGdiResources() {
        if (memDC_ != nullptr) {
            ::SelectObject(memDC_, oldBitmap_);
            ::DeleteDC(memDC_);
            memDC_ = nullptr;
            oldBitmap_ = nullptr;
        }
        if (dibSection_ != nullptr) {
            ::DeleteObject(dibSection_);
            dibSection_ = nullptr;
        }
        image_.reset();
    }

    Image::~Image() {
        releaseGdiResources();
    }

    Image::Image(Image&& other) noexcept {
        *this = std::move(other);
    }

    Image& Image::operator=(Image&& other) noexcept {
        if (this != &other) {
            releaseGdiResources();

            image_ = other.image_;
            dibSection_ = other.dibSection_;
            memDC_ = other.memDC_;
            oldBitmap_ = other.oldBitmap_;

            other.image_.reset();
            other.dibSection_ = nullptr;
            other.memDC_ = nullptr;
            other.oldBitmap_ = nullptr;
        }
        return *this;
    }

    HDC Image::memDC() {
        if (!isValid()) {
            return nullptr;
        }

        if (memDC_ == nullptr) {
            memDC_ = ::CreateCompatibleDC(nullptr);
            if (memDC_ == nullptr) {
                return nullptr;
            }
            oldBitmap_ = static_cast<HBITMAP>(::SelectObject(memDC_, dibSection_));
        }

        return memDC_;
    }

}

#include "newui/graphics.h"
#include "newui/svgimage.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace {

bool hasExtension(const std::string& path, const char* ext) {
    const size_t extLen = std::strlen(ext);
    if (path.size() < extLen) {
        return false;
    }
    return _stricmp(path.c_str() + (path.size() - extLen), ext) == 0;
}

}

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
        dibSection_ = dib;
        return true;
    }

    Image::Image(int width, int height) {
        createDibBackedImage(width, height);
    }

    Image::Image(const std::string& path) {
        if (hasExtension(path, ".svg")) {
            // See this constructor's own header comment - kDefaultSvgRasterSize
            // is a fixed stand-in for "natural size", not a real one.
            *this = Image(path, kDefaultSvgRasterSize, kDefaultSvgRasterSize);
            return;
        }

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

    Image::Image(const std::string& path, int width, int height) {
        if (width <= 0 || height <= 0) {
            return;
        }

        BLImage decoded;
        if (hasExtension(path, ".svg")) {
            if (!renderSvgFile(path, width, height, decoded)) {
                return;
            }
        } else {
            if (decoded.read_from_file(path.c_str()) != BL_SUCCESS) {
                return;
            }
            if (decoded.format() != BL_FORMAT_PRGB32 && decoded.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
                return;
            }
            if (decoded.size().w != width || decoded.size().h != height) {
                BLImage scaled;
                if (scaled.create(width, height, BL_FORMAT_PRGB32) != BL_SUCCESS) {
                    return;
                }
                BLContext scaleCtx(scaled);
                scaleCtx.blit_image(BLRectI(0, 0, width, height), decoded);
                scaleCtx.end();
                decoded = scaled;
            }
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

    BLGradient Gradient::toBLGradient() const {
        BLGradient gradient;

        switch (kind_) {
            case GradientKind::Radial:
                gradient.create(BLRadialGradientValues(
                    double(radialCenter_.x), double(radialCenter_.y),
                    double(radialCenter_.x + radialFocalOffset_.x), double(radialCenter_.y + radialFocalOffset_.y),
                    double(radialRadius_), 0.0), toBLExtendMode(extendMode_));
                break;

            case GradientKind::Conic:
                gradient.create(BLConicGradientValues(
                    double(conicCenter_.x), double(conicCenter_.y),
                    double(conicAngle_), double(conicRepeat_)), toBLExtendMode(extendMode_));
                break;

            case GradientKind::Linear:
            case GradientKind::Point:  // unreachable via toBLVar() - Point never calls this
            default:
                gradient.create(BLLinearGradientValues(
                    double(linearStart_.x), double(linearStart_.y),
                    double(linearEnd_.x), double(linearEnd_.y)), toBLExtendMode(extendMode_));
                break;
        }

        for (const GradientStop& stop : stops_) {
            gradient.add_stop(double(stop.offset()), stop.color().toBLRgba32());
        }

        return gradient;
    }

    // Blends points_ across a raster sized to localBounds (its longer side
    // capped at pointRasterMax_, preserving aspect ratio) via inverse-
    // distance weighting: each pixel's color is a weighted average of
    // every point's color, weight = 1/distance^pointBlendPower_ (a point
    // exactly on a pixel dominates that pixel outright - see the
    // near-zero-distance guard below). Straightforward O(width*height*
    // points_.size()) - fine for the small raster sizes pointRasterMax_
    // defaults to and the handful of points a design accent actually
    // needs; not written for a large point count or a large raster.
    BLImage Gradient::rasterizePoints(const Rect& localBounds) const {
        BLImage image;

        if (points_.empty() || localBounds.width() <= 0.0f || localBounds.height() <= 0.0f) {
            return image;
        }

        float longSide = localBounds.width() > localBounds.height() ? localBounds.width() : localBounds.height();
        float scale = longSide > float(pointRasterMax_) ? float(pointRasterMax_) / longSide : 1.0f;

        int w = int(std::ceil(localBounds.width() * scale));
        int h = int(std::ceil(localBounds.height() * scale));
        w = w < 1 ? 1 : w;
        h = h < 1 ? 1 : h;

        if (image.create(w, h, BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return BLImage();
        }

        BLImageData data;
        image.get_data(&data);
        uint8_t* pixels = static_cast<uint8_t*>(data.pixel_data);

        for (int y = 0; y < h; ++y) {
            float localY = localBounds.top() + (float(y) + 0.5f) / scale;
            uint8_t* row = pixels + intptr_t(y) * data.stride;

            for (int x = 0; x < w; ++x) {
                float localX = localBounds.left() + (float(x) + 0.5f) / scale;

                float weightSum = 0.0f;
                float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
                for (const GradientPoint& p : points_) {
                    float dx = localX - p.position().x;
                    float dy = localY - p.position().y;
                    float dist = std::sqrt(dx * dx + dy * dy);
                    float weight = dist < 0.001f ? 1.0e6f : 1.0f / std::pow(dist, pointBlendPower_);

                    weightSum += weight;
                    r += p.color().r * weight;
                    g += p.color().g * weight;
                    b += p.color().b * weight;
                    a += p.color().a * weight;
                }

                if (weightSum > 0.0f) {
                    r /= weightSum;
                    g /= weightSum;
                    b /= weightSum;
                    a /= weightSum;
                }

                // Direct premultiplied-BGRA store, not Color::toBGRA32() -
                // that's a fully generic PackedChannel[] bit-packer (built
                // for arbitrary channel widths, e.g. 5/6/5) plus a second,
                // separate premultiply-by-byte-alpha pass on top; way more
                // machinery than a fixed 8-bit-per-channel store needs in
                // a tight per-pixel loop. r/g/b/a here are already a
                // weighted *average* of colors already in [0,1] with non-
                // negative weights, so the result can't leave [0,1] either -
                // clampToByte() below is just cheap insurance against float
                // error at the edges, not a real clamping need.
                auto clampToByte = [](float v) -> uint8_t {
                    v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
                    return uint8_t(std::lround(v * 255.0f));
                };
                float clampedA = a < 0.0f ? 0.0f : (a > 1.0f ? 1.0f : a);
                row[x * 4 + 0] = clampToByte(b * clampedA);
                row[x * 4 + 1] = clampToByte(g * clampedA);
                row[x * 4 + 2] = clampToByte(r * clampedA);
                row[x * 4 + 3] = clampToByte(a);
            }
        }

        return image;
    }

    BLVar Gradient::toBLVar(const Rect& localBounds) const {
        if (kind_ != GradientKind::Point) {
            return BLVar(toBLGradient());
        }

        BLImage raster = rasterizePoints(localBounds);
        if (raster.is_empty()) {
            return BLVar();
        }

        float longSide = localBounds.width() > localBounds.height() ? localBounds.width() : localBounds.height();
        float scale = longSide > float(pointRasterMax_) ? float(pointRasterMax_) / longSide : 1.0f;

        BLMatrix2D m = BLMatrix2D::make_identity();
        m.translate(double(localBounds.left()), double(localBounds.top()));
        m.scale(1.0 / double(scale));

        // BLPattern(raster, ...) copies blend2d's own COW image handle
        // (a cheap refcount bump, not a pixel copy) - the real pixel
        // buffer this points at stays alive for as long as the returned
        // BLVar (or anything copied from it) does, unlike the gfx::Image
        // this used to wrap here (see rasterizePoints()'s own comment,
        // graphics.h, for the use-after-free that caused).
        return BLVar(BLPattern(raster, BL_EXTEND_MODE_PAD, m));
    }

    const BLImage& Fill::resolvedImage() const {
        if (!imageCacheValid_) {
            imageCache_.reset();
            imageCache_.read_from_file(imagePath_.c_str());
            imageCacheValid_ = true;
        }
        return imageCache_;
    }

    BLVar Fill::toBLVar(const Rect& localBounds) const {
        switch (kind_) {
            case PaintKind::Color:
                return BLVar(color_.toBLRgba32());

            case PaintKind::Gradient:
                return gradient_.toBLVar(localBounds);

            case PaintKind::Image: {
                const BLImage& image = resolvedImage();
                if (image.is_empty()) {
                    return BLVar();
                }
                return BLVar(BLPattern(image));
            }

            case PaintKind::None:
            default:
                return BLVar();
        }
    }

}

#pragma once

#include <newui/newui.h>

#include <blend2d/blend2d.h>

#include <string>

namespace newui::gfx {

    // Wraps a BLImage backed by a real Win32 DIB section, so the exact
    // same pixel buffer blend2d reads/writes via blImage() can also be
    // drawn into (or read out of) via classic GDI, through a memory
    // device context - memDC(). Exists so code that has to call a
    // GDI-only API (DrawThemeBackground(), DrawIconEx(), ...) against a
    // blend2d-managed buffer doesn't have to re-derive the
    // CreateDIBSection()/CreateCompatibleDC()/SelectObject() dance from
    // scratch each time - see ThemedViewStyle::paint() (viewstyle.cpp,
    // its own hand-rolled BeginBufferedPaint()-based version of this) and
    // RootView::resizeImageBuffer() (rootview.cpp, the externally-owned-
    // buffer/create_from_data() pattern Image reuses) for the two
    // existing, separately-hand-rolled instances of a similar problem in
    // this codebase.
    //
    // Always a 32bpp top-down premultiplied-BGRA buffer (BL_FORMAT_PRGB32) -
    // byte-for-byte what both a 32bpp DIB section and blend2d's alpha-aware
    // format agree on (same fact ThemedViewStyle::paint() and cursor.cpp's
    // createCursorFromImage() already rely on), so no format parameter to
    // get wrong.
    //
    // Non-copyable (owns real GDI handles) - move-only.
    class Image {
    public:
        Image() = default;

        // Blank width x height canvas, fully transparent - CreateDIBSection()'s
        // returned bits aren't guaranteed zeroed, so this explicitly clears them.
        Image(int width, int height);

        // Decodes path (PNG/BMP/JPEG/QOI - whatever the file actually is,
        // via BLImage::read_from_file(), same codecs Bundle::loadImage()
        // already uses) into a fresh DIB-backed buffer, converting to
        // BL_FORMAT_PRGB32 first if the decoded image isn't already that
        // format. isValid() is false afterward if the file couldn't be
        // read/decoded.
        explicit Image(const std::string& path);

        // Copies source's pixel data into a fresh DIB-backed buffer
        // (converting to BL_FORMAT_PRGB32 first if needed) - source keeps
        // whatever backing memory it already had; this Image gets its
        // own independent, GDI-selectable copy. isValid() is false
        // afterward if source is empty.
        explicit Image(const BLImage& source);

        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;

        bool isValid() const {
            return !image_.is_empty();
        }

        int width() const {
            return image_.size().w;
        }

        int height() const {
            return image_.size().h;
        }

        // Live, read/write access to the pixel data via blend2d - e.g.
        // BLContext ctx(image.blImage()); ...draws into the exact same
        // buffer memDC() GDI calls read/write too.
        BLImage& blImage() {
            return image_;
        }

        const BLImage& blImage() const {
            return image_;
        }

        // A memory DC selected with the DIB section backing blImage() -
        // any GDI call into this HDC writes straight into blImage()'s own
        // buffer, no blit/copy needed afterward (or beforehand, to read
        // out whatever GDI already drew). Created lazily on first call -
        // the same HDC is returned every time after that, until this
        // Image is destroyed or moved-from. Returns nullptr if isValid()
        // is false.
        HDC memDC();

    private:
        // Allocates a fresh width x height DIB section and wraps its bits
        // directly as image_ (via BLImage::create_from_data(), no
        // destroy_func - the DIB section, not blend2d, owns this memory;
        // releaseGdiResources() is what frees it). If copySrc is
        // non-null, copies copySrcStride-strided pixel rows in from it
        // (assumed already BL_FORMAT_PRGB32/BGRA); otherwise zero-fills
        // (a blank, fully transparent canvas). Returns false (this Image
        // left invalid) on any failure.
        bool createDibBackedImage(int width, int height, const void* copySrc = nullptr, intptr_t copySrcStride = 0);

        void releaseGdiResources();

        BLImage image_;
        HBITMAP dibSection_ = nullptr;
        HDC memDC_ = nullptr;
        HBITMAP oldBitmap_ = nullptr;  // memDC_'s original 1x1 monochrome bitmap - restored before DeleteDC()
    };

}

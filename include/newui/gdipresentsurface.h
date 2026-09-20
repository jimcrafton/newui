#pragma once

#include <newui/presentsurface.h>

namespace newui {

    // The original (and default) RootView presentation path, moved here
    // unchanged from RootView itself: image() wraps a CreateDIBSection()-
    // allocated buffer directly (via BLImage::create_from_data(), not
    // BLImage::create() - blend2d pads its own allocations to a 16-byte
    // stride for SIMD, which wouldn't match the stride a DIB section infers
    // from biWidth), so paint() can BitBlt() from an already-realized GDI
    // bitmap object instead of re-describing a raw pointer via
    // StretchDIBits() on every WM_PAINT. Owning the buffer this way keeps
    // the stride at exactly width * 4 so Blend2D and GDI agree on layout
    // (guaranteed DWORD-aligned for 32bpp regardless of width, so no
    // padding to account for). present() just InvalidateRect()s - the
    // actual transfer happens when Windows delivers WM_PAINT.
    //
    // @reflect ignore=true
    class GdiPresentSurface : public PresentSurface {
    public:
        GdiPresentSurface() = default;
        ~GdiPresentSurface() override;

        GdiPresentSurface(const GdiPresentSurface&) = delete;
        GdiPresentSurface& operator=(const GdiPresentSurface&) = delete;

        PresentBackend backend() const override {
            return PresentBackend::Gdi;
        }

        bool resize(int width, int height, BLFormat format) override;
        void release() override;

        bool isValid() const override {
            return memDC_ != nullptr;
        }

        BLImage& image() override {
            return image_;
        }

        void present(const newui::Rect& dirty) override;
        void paint(HDC hdc, const newui::Rect& paintRect) override;

    private:
        BLImage image_;
        HDC memDC_ = nullptr;
        HBITMAP dibSection_ = nullptr;
        // Whatever memDC_ had selected before dibSection_ - re-selected
        // before deleting dibSection_ (see release()), since deleting a
        // bitmap while it's still selected into a DC is undefined behavior.
        HBITMAP dibSectionOldBitmap_ = nullptr;
    };

}

#pragma once

#include <windows.h>

#include <blend2d/blend2d.h>

#include <newui/geometry.h>

#include <memory>
#include <string>

namespace newui {

    // Which PresentSurface implementation a RootView uses. Dxgi means "DXGI
    // if this machine can actually do it, otherwise GDI" - DxgiPresentSurface
    // falls back on its own (no D3D11 device, only a software adapter, a
    // swap-chain failure, an unrecoverable device loss), so there's no
    // separate "auto" mode: it would behave identically. What a RootView
    // ended up with is PresentSurface::backend() / RootView::presentBackend().
    //
    // @reflect ignore=true
    enum class PresentBackend {
        Gdi,
        Dxgi
    };

    // The one place a RootView's "CPU-rendered pixels -> the screen" step
    // lives, so how those pixels actually reach the window (GDI BitBlt
    // today - GdiPresentSurface; a DXGI swap chain later) can be swapped
    // per RootView without touching anything that draws. Blend2D always
    // renders into image() on the CPU, whichever implementation this is -
    // this only owns that buffer's allocation and its final transfer.
    //
    // The interface is exactly what RootView's own call sites need, nothing
    // more: resize()/release() are its old resizeImageBuffer()/
    // releaseImageBuffer(), present() is presentRepaintedBuffer()'s default
    // "hand the dirty region to the screen", paint() is its WM_PAINT
    // handler.
    //
    // @reflect ignore=true
    class PresentSurface {
    public:
        virtual ~PresentSurface() = default;

        // The RootView's real HWND - set as soon as it exists, cleared
        // (nullptr) when it's destroyed. present()/paint() no-op sensibly
        // without one.
        void setWindow(HWND hwnd) {
            hwnd_ = hwnd;
        }

        HWND window() const {
            return hwnd_;
        }

        // What this surface is actually presenting through right now - a
        // DxgiPresentSurface that fell back to GDI reports Gdi.
        virtual PresentBackend backend() const = 0;

        // (Re)allocates image() at width x height in `format`, fully zeroed
        // (transparent black - see RootView::imageBufferFormat()), dropping
        // whatever was there before. A non-positive width/height just
        // releases and returns false, like any other failure - isValid() is
        // false afterward either way.
        virtual bool resize(int width, int height, BLFormat format) = 0;

        // Frees everything resize() allocated; image() is empty afterward.
        // Safe to call when already released.
        virtual void release() = 0;

        virtual bool isValid() const = 0;

        // The Blend2D render target. Empty when !isValid().
        virtual BLImage& image() = 0;

        // Called once repaint() has finished writing `dirty` (root-local,
        // pixel-snapped) into image() - hands that region to the screen,
        // either directly or by scheduling a WM_PAINT for it.
        virtual void present(const newui::Rect& dirty) = 0;

        // WM_PAINT: transfer paintRect of image() onto hdc, if this
        // implementation presents that way. An implementation that presents
        // outside WM_PAINT does nothing here.
        virtual void paint(HDC hdc, const newui::Rect& paintRect) = 0;

    private:
        HWND hwnd_ = nullptr;
    };

    // Case-insensitive "gdi" / "dxgi". False (out untouched) for anything else.
    bool parsePresentBackend(const std::string& text, PresentBackend& out);

    // What a newly constructed RootView uses: whatever setDefaultPresentBackend()
    // last set, otherwise the NEWUI_PRESENT environment variable ("gdi" or
    // "dxgi", read once, first use), otherwise Gdi. Only affects RootViews
    // constructed afterward - the one place this gets consulted is RootView's
    // constructors.
    PresentBackend defaultPresentBackend();
    void setDefaultPresentBackend(PresentBackend backend);

    // A fresh, unattached surface of the requested kind. Never null: Dxgi
    // returns a DxgiPresentSurface, which handles its own GDI fallback.
    std::unique_ptr<PresentSurface> createPresentSurface(PresentBackend backend);

}

#pragma once

#include <memory>
#include <string>

#include <newui/gdipresentsurface.h>
#include <newui/presentsurface.h>

namespace newui {

    // Presents through a DXGI flip-model swap chain instead of WM_PAINT/BitBlt.
    // Blend2D still renders on the CPU, into a plain image() exactly like
    // GdiPresentSurface's - only the final transfer differs: present() uploads
    // just the dirty box of that buffer into a GPU texture
    // (UpdateSubresource), CopyResource()s it into the swap chain's back
    // buffer, and Present1()s. paint() (WM_PAINT) does nothing - DWM keeps the
    // last presented frame on screen by itself.
    //
    // The CPU buffer deliberately stays the render target (rather than
    // rendering straight into mapped texture memory): RootView repaints only
    // dirtyRect_ into a *persistent* buffer and relies on the rest staying
    // intact, and D3D11_MAP_WRITE_DISCARD (like FLIP_DISCARD's back buffer)
    // hands back undefined contents every frame.
    //
    // Degrades to GDI on its own, transparently, and never throws: no D3D11
    // hardware device, an adapter that's only Microsoft's software rasterizer
    // (e.g. a Remote Desktop session - a CPU-emulated GPU would present slower
    // than plain GDI), a swap chain that can't be created on this window, or a
    // device loss that can't be recovered. Pixels already rendered are carried
    // over, and backend() reports Gdi from then on. A recoverable device loss
    // (driver reset/update) instead rebuilds the device and swap chain and
    // re-uploads the whole CPU buffer.
    //
    // The D3D device/swap chain are created lazily, the first time there's both
    // a buffer and a window (RootView sizes its buffer before its HWND exists).
    //
    // @reflect ignore=true
    class DxgiPresentSurface : public PresentSurface {
    public:
        DxgiPresentSurface();
        ~DxgiPresentSurface() override;

        DxgiPresentSurface(const DxgiPresentSurface&) = delete;
        DxgiPresentSurface& operator=(const DxgiPresentSurface&) = delete;

        PresentBackend backend() const override;

        bool resize(int width, int height, BLFormat format) override;
        void release() override;
        bool isValid() const override;
        BLImage& image() override;
        void present(const newui::Rect& dirty) override;
        void paint(HDC hdc, const newui::Rect& paintRect) override;

        // Why this surface gave up on DXGI - empty while it hasn't. (Also sent to the debugger's output
        // window when it happens.)
        const std::string& fallbackReason() const {
            return fallbackReason_;
        }

    private:
        struct Gpu;  // D3D11 device/context/swap chain/texture - keeps d3d11.h out of this header

        bool ensureGpu();
        bool resizeGpu();
        void releaseGpu();
        void fallBackToGdi(const std::string& reason);

        BLImage image_;
        // Non-null once this surface has given up on DXGI - every operation
        // then just forwards to it. (image_ is empty from then on.)
        std::unique_ptr<GdiPresentSurface> gdi_;
        std::unique_ptr<Gpu> gpu_;
        // The whole CPU buffer needs uploading on the next present(): set by
        // anything that leaves the GPU texture without valid content (a new
        // texture after a resize, a rebuilt device).
        bool needsFullUpload_ = true;
        // Set once a device has been rebuilt after a loss without an
        // intervening successful present - a second consecutive loss means it
        // isn't recoverable.
        bool recoveringFromLoss_ = false;
        BLFormat format_ = BL_FORMAT_XRGB32;
        std::string fallbackReason_;
    };

}

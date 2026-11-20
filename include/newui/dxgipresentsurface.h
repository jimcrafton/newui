#pragma once

#include <memory>
#include <string>

#include <functional>

#include <newui/gdipresentsurface.h>
#include <newui/presentsurface.h>

// D3D11's interfaces live in the global namespace - these have to be declared
// here, not inside namespace newui, or they'd be unrelated newui::ID3D11*
// types that no real D3D pointer converts to.
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;

namespace newui {

    // Fires when the D3D11 device is created or recreated (first
    // ensureGpu(), a device-loss rebuild, a swap chain made for a new
    // window), and when it's about to go away: a null device/context and
    // 0x0 size, called *before* the old device is destroyed so the receiver
    // can release its own resources against it. A GDI fallback, release()
    // and a minimize-to-nothing resize all end up there. width/height are
    // the presentable size the new device's texture was built at - the
    // resize hook does not fire for that size, so this is where a canvas
    // learns it.
    using GpuDeviceHook = std::function<void(ID3D11Device*, ID3D11DeviceContext*, UINT width, UINT height)>;

    // Fires once per present() that reaches Present1(), after the CPU
    // buffer has been copied into the back buffer and before Present1().
    // The render target is only valid for this call and is unbound
    // afterward. present() returns early on an empty or out-of-buffer dirty
    // rect, before this runs - a canvas-only change still needs a non-empty
    // RootView::invalidate(view, &rect). The draw lands on top of everything
    // Blend2D painted, including popups/overlays over the same area.
    using GpuCompositeHook = std::function<void(ID3D11DeviceContext*, ID3D11RenderTargetView*)>;

    // Fires at the end of a resize() that left a device in place (never
    // under GDI, and not while there's no window/device yet). May repeat
    // the size a device hook just delivered.
    using GpuResizeHook = std::function<void(UINT width, UINT height)>;

    // Hooks must not throw: exceptions are caught and dropped at the call
    // site, so they can't unwind through present()/resize().

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

        // Fires immediately if a device already exists. Replaces any
        // previous hook. See the hook typedefs above.
        void setGpuDeviceHook(GpuDeviceHook hook);
        void setGpuCompositeHook(GpuCompositeHook hook);
        // Unlike the device hook, does not fire on registration - read the
        // size from the device hook instead.
        void setGpuResizeHook(GpuResizeHook hook);

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
        GpuDeviceHook gpuDeviceHook_;
        GpuCompositeHook gpuCompositeHook_;
        GpuResizeHook gpuResizeHook_;
    };

}

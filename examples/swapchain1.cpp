// Phase 1b feasibility spike for swapchain-plan.md: proves that Blend2D-rendered pixels can reach a
// real newui RootView's HWND through a DXGI flip-model swap chain, independent of RootView's own GDI
// imageBuffer_/WM_PAINT path. Deliberately does NOT touch RootView's presentation code - this is a
// throwaway program, not the Phase 2/3 integration.
//
// Per frame: Map() the dynamic staging texture (WRITE_DISCARD) -> wrap the mapped memory in a BLImage
// (no intermediate copy) -> render with Blend2D straight into it -> Unmap() -> CopyResource() into the
// swap chain's back buffer -> Present1().

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/uicolormanager.h"

#include <cstdio>

#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

using Microsoft::WRL::ComPtr;

namespace {

    ComPtr<ID3D11Device>        g_d3dDevice;
    ComPtr<ID3D11DeviceContext> g_d3dContext;
    ComPtr<IDXGISwapChain1>     g_swapChain;
    ComPtr<ID3D11Texture2D>     g_stagingTexture; // bridge from Blend2D CPU memory to the GPU
    UINT g_width = 0;
    UINT g_height = 0;

    bool check(HRESULT hr, const char* what) {
        if (FAILED(hr)) {
            std::printf("swapchain1: %s failed, hr=0x%08lX\n", what, static_cast<unsigned long>(hr));
            return false;
        }
        return true;
    }

    bool createDeviceAndSwapChain(HWND hwnd, UINT width, UINT height) {
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        if (!check(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                featureLevels, 1, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, &g_d3dContext), "D3D11CreateDevice")) {
            return false;
        }

        ComPtr<IDXGIDevice> dxgiDevice;
        ComPtr<IDXGIAdapter> dxgiAdapter;
        ComPtr<IDXGIFactory2> dxgiFactory;
        if (!check(g_d3dDevice.As(&dxgiDevice), "QueryInterface(IDXGIDevice)") ||
            !check(dxgiDevice->GetAdapter(&dxgiAdapter), "GetAdapter") ||
            !check(dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory)), "GetParent(IDXGIFactory2)")) {
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = width;
        sd.Height = height;
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // native match for Blend2D's BL_FORMAT_PRGB32
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.Scaling = DXGI_SCALING_NONE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

        return check(dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.Get(), hwnd, &sd, nullptr, nullptr, &g_swapChain),
            "CreateSwapChainForHwnd");
    }

    // (Re)creates everything sized to the window: swap chain buffers + the staging texture.
    bool resizeTargets(UINT width, UINT height) {
        if (!g_swapChain || width == 0 || height == 0) {
            return false;  // not created yet, or minimized
        }

        g_stagingTexture.Reset();

        // ResizeBuffers() fails unless every outstanding back-buffer reference is released - renderAndPresent()
        // only ever holds one for the duration of a single call, so none can be live here.
        if (!check(g_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "ResizeBuffers")) {
            return false;
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width = width;
        desc.Height = height;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DYNAMIC;             // fast CPU writes
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // allows Map()

        if (!check(g_d3dDevice->CreateTexture2D(&desc, nullptr, &g_stagingTexture), "CreateTexture2D(staging)")) {
            return false;
        }

        g_width = width;
        g_height = height;
        return true;
    }

    void renderAndPresent() {
        if (!g_stagingTexture || !g_swapChain) {
            return;
        }

        D3D11_MAPPED_SUBRESOURCE mapped = {};
        if (!check(g_d3dContext->Map(g_stagingTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped), "Map(staging)")) {
            return;
        }

        {
            // Blend2D renders directly into the mapped texture memory - no intermediate buffer, no memcpy.
            // RowPitch is the GPU's own row stride, generally not width * 4, so it must be passed through.
            BLImage img;
            img.create_from_data(int(g_width), int(g_height), BL_FORMAT_PRGB32, mapped.pData, intptr_t(mapped.RowPitch));

            BLContext ctx(img);

            BLGradient bg(BLLinearGradientValues(0, 0, 0, double(g_height)));
            bg.add_stop(0.0, BLRgba32(0xFF1E3A8A));
            bg.add_stop(1.0, BLRgba32(0xFF0F172A));
            ctx.set_fill_style(bg);
            ctx.fill_all();

            // Edge frame: any size/scaling mismatch between the swap chain and the window shows up here as a
            // missing or thickened side.
            ctx.set_fill_style(BLRgba32(0xFFEF4444));
            const double t = 4.0, w = double(g_width), h = double(g_height);
            ctx.fill_rect(BLRect(0, 0, w, t));
            ctx.fill_rect(BLRect(0, h - t, w, t));
            ctx.fill_rect(BLRect(0, 0, t, h));
            ctx.fill_rect(BLRect(w - t, 0, t, h));

            ctx.set_fill_style(BLRgba32(0xFFFACC15));
            ctx.fill_round_rect(BLRoundRect(w * 0.25, h * 0.25, w * 0.5, h * 0.5, 24.0));
            ctx.set_fill_style(BLRgba32(0xC0FFFFFF)); // translucent, exercises PRGB32 blending
            ctx.fill_circle(BLCircle(w * 0.5, h * 0.5, (w < h ? w : h) * 0.2));

            ctx.end();
        }

        g_d3dContext->Unmap(g_stagingTexture.Get(), 0);

        ComPtr<ID3D11Texture2D> backBuffer;
        if (!check(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer(0)")) {
            return;
        }
        g_d3dContext->CopyResource(backBuffer.Get(), g_stagingTexture.Get());

        HRESULT hr = g_swapChain->Present1(1, 0, &DXGI_PRESENT_PARAMETERS{});
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
            std::printf("swapchain1: device lost (hr=0x%08lX) - Phase 4 recreate path not implemented in spike\n",
                static_cast<unsigned long>(hr));
        }
        else {
            check(hr, "Present1");
        }
    }

}

int main() {

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("swapchain1");
    app.setFrame(&frame);

    frame.setTitle("Swap Chain Example");
    frame.setBounds(newui::Rect(10, 10, 820, 420));

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    root.onCreated += [](newui::View& v) {
        HWND hwnd = static_cast<newui::RootView&>(v).windowHandle();

        // bounds() can still be 0x0 here (a RootView's child HWND only gets real bounds on its first
        // WM_SIZE), so ask the HWND itself.
        RECT rc = {};
        ::GetClientRect(hwnd, &rc);
        if (createDeviceAndSwapChain(hwnd, UINT(rc.right - rc.left), UINT(rc.bottom - rc.top))) {
            if (resizeTargets(UINT(rc.right - rc.left), UINT(rc.bottom - rc.top))) {
                renderAndPresent();
            }
        }
        return newui::SyncReturn::Handled;
    };

    root.onSizeChanged += [](newui::View&, const newui::Size& newSize) {
        if (newSize.width > 0 && newSize.height > 0 && resizeTargets(UINT(newSize.width), UINT(newSize.height))) {
            renderAndPresent();
        }
        return newui::SyncReturn::Handled;
    };

    app.run();

    return 0;
}

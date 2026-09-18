#include "newui/newui.h"
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/graphics.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <iostream>


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

ComPtr<ID3D11Device>        g_d3dDevice;
ComPtr<ID3D11DeviceContext> g_d3dContext;
ComPtr<IDXGISwapChain1>     g_swapChain;
ComPtr<ID3D11Texture2D>     g_stagingTexture; // Bridge from Blend2D CPU memory to GPU



int main() {
    
    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("swapchain1");
    app.setFrame(&frame);

    frame.setTitle("Swap Chain Example");
    frame.setBounds(newui::Rect(10, 10, 820, 420));
    

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

	root.onCreated += [](newui::View& v)  {
		newui::RootView& rootView = static_cast<newui::RootView&>(v);
        HWND hwnd = rootView.windowHandle();
        D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
        UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

        D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, creationFlags,
            featureLevels, 1, D3D11_SDK_VERSION, &g_d3dDevice, nullptr, &g_d3dContext
        );

		ComPtr<IDXGIDevice> dxgiDevice;
		g_d3dDevice.As(&dxgiDevice);
		ComPtr<IDXGIAdapter> dxgiAdapter;
		dxgiDevice->GetAdapter(&dxgiAdapter);
		ComPtr<IDXGIFactory2> dxgiFactory;
		dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));


        DXGI_SWAP_CHAIN_DESC1 sd = {};
        sd.Width = rootView.bounds().width();
        sd.Height = rootView.bounds().height();
        sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // Native Match for Blend2D BL_FORMAT_PRGB32
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2; // Double buffered
        sd.Scaling = DXGI_SCALING_NONE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Modern hardware page-flip

        auto hr = dxgiFactory->CreateSwapChainForHwnd(g_d3dDevice.Get(), hwnd, &sd, nullptr, nullptr, &g_swapChain);


		return newui::SyncReturn::Handled;
		};

	root.onSizeChanged += [](newui::View& v, const newui::Size& newSize) {
		newui::RootView& rootView = static_cast<newui::RootView&>(v);
		if (newSize.width <= 0 || newSize.height <= 0) {
			return newui::SyncReturn::Handled;
		}
        
        g_stagingTexture.Reset();


		if (g_swapChain) {
			auto hr = g_swapChain->ResizeBuffers(0, newSize.width, newSize.height, DXGI_FORMAT_UNKNOWN, 0);

            D3D11_TEXTURE2D_DESC desc = {};
            desc.Width = newSize.width;
            desc.Height = newSize.height;
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DYNAMIC;         // Allows fast CPU writes
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE; // Allows mapping memory

            hr = g_d3dDevice->CreateTexture2D(&desc, nullptr, &g_stagingTexture);

		}
		return newui::SyncReturn::Handled;
		};
    
	newui::RunLoop::current().postIdle([&]() {
        D3D11_MAPPED_SUBRESOURCE mappedResource;
        HRESULT hr = g_d3dContext->Map(g_stagingTexture.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
        if (SUCCEEDED(hr)) {
            BYTE* destPixels = reinterpret_cast<BYTE*>(mappedResource.pData);
            BYTE* srcPixels = reinterpret_cast<BYTE*>(imgData.pixelData);

            // 3. Fast scanline transfer from CPU memory allocation straight into the mapped GPU texture
            for (int y = 0; y < g_height; ++y) {
                memcpy(
                    destPixels + (y * mappedResource.RowPitch),
                    srcPixels + (y * imgData.stride),
                    g_width * 4
                );
            }
            g_d3dContext->Unmap(g_stagingTexture.Get(), 0);
        }

		return false;  // done after this one call
		});

    


    app.run();

    return 0;
}
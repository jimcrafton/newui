#include "newui/dxgipresentsurface.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

	constexpr UINT kMicrosoftVendorId = 0x1414;  // "Microsoft Basic Render Driver" / WARP

	bool isDeviceLoss(HRESULT hr) {
		return hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET;
	}

	// True for an adapter that's Microsoft's software rasterizer (WARP) - what a Remote Desktop
	// session or a machine with no GPU driver hands back. A CPU-emulated GPU presents slower than
	// plain GDI, so it counts as "no DXGI here", same as having no device at all.
	bool isSoftwareAdapter(IDXGIAdapter* adapter) {
		ComPtr<IDXGIAdapter1> adapter1;
		if (FAILED(adapter->QueryInterface(IID_PPV_ARGS(&adapter1)))) {
			return false;
		}
		DXGI_ADAPTER_DESC1 desc = {};
		if (FAILED(adapter1->GetDesc1(&desc))) {
			return false;
		}
		return (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || desc.VendorId == kMicrosoftVendorId;
	}

	// floor()/ceil() then clamp into [0, limit] - the outward-snapped pixel edge of a dirty rect.
	int clampEdge(float value, int limit) {
		int v = int(value);
		return v < 0 ? 0 : (v > limit ? limit : v);
	}

}

namespace newui {

	struct DxgiPresentSurface::Gpu {
		ComPtr<ID3D11Device> device;
		ComPtr<ID3D11DeviceContext> context;
		ComPtr<IDXGISwapChain1> swapChain;
		ComPtr<ID3D11Texture2D> texture;  // same size/format as the swap chain's buffers; CopyResource() source
		HWND hwnd = nullptr;              // the window swapChain was created for

		bool createTexture(UINT width, UINT height) {
			D3D11_TEXTURE2D_DESC desc = {};
			desc.Width = width;
			desc.Height = height;
			desc.MipLevels = 1;
			desc.ArraySize = 1;
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // byte-for-byte Blend2D's XRGB32/PRGB32
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			return SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &texture));
		}
	};

	DxgiPresentSurface::DxgiPresentSurface() = default;

	DxgiPresentSurface::~DxgiPresentSurface() = default;

	PresentBackend DxgiPresentSurface::backend() const {
		return gdi_ ? PresentBackend::Gdi : PresentBackend::Dxgi;
	}

	bool DxgiPresentSurface::isValid() const {
		return gdi_ ? gdi_->isValid() : !image_.is_empty();
	}

	BLImage& DxgiPresentSurface::image() {
		return gdi_ ? gdi_->image() : image_;
	}

	void DxgiPresentSurface::release() {
		image_.reset();
		releaseGpu();
		if (gdi_) {
			gdi_->release();
		}
	}

	void DxgiPresentSurface::releaseGpu() {
		gpu_.reset();
	}

	bool DxgiPresentSurface::resize(int width, int height, BLFormat format) {
		format_ = format;
		if (gdi_) {
			gdi_->setWindow(window());
			return gdi_->resize(width, height, format);
		}

		image_.reset();
		if (width <= 0 || height <= 0) {
			releaseGpu();
			return false;
		}
		if (image_.create(width, height, format) != BL_SUCCESS) {
			return false;
		}

		// A fresh buffer has to start fully zeroed (transparent black), same as GdiPresentSurface's -
		// BLImage::create() doesn't guarantee it.
		BLImageData data;
		image_.get_data(&data);
		std::memset(data.pixel_data, 0, size_t(data.stride) * size_t(height));

		needsFullUpload_ = true;

		// Already have a swap chain: resize it in place (falls back to a rebuild, then to GDI). Otherwise
		// try to create one now - which quietly does nothing while there's no window yet.
		if (gpu_) {
			resizeGpu();
		}
		else {
			ensureGpu();
		}

		return gdi_ ? gdi_->isValid() : true;
	}

	// Brings up the device, swap chain and texture for the current buffer size and window. True when
	// they're ready. False either because there's nothing to attach to yet (no buffer or no window -
	// try again later) or because DXGI isn't possible here, in which case gdi_ is now set.
	bool DxgiPresentSurface::ensureGpu() {
		if (gdi_ || image_.is_empty() || window() == nullptr) {
			return false;
		}
		if (gpu_ && gpu_->hwnd == window()) {
			return true;
		}
		releaseGpu();  // no swap chain yet, or one made for a different window

		const UINT width = UINT(image_.size().w);
		const UINT height = UINT(image_.size().h);

		auto gpu = std::make_unique<Gpu>();

		const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
		HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
			D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_SINGLETHREADED,
			levels, UINT(sizeof(levels) / sizeof(levels[0])), D3D11_SDK_VERSION, &gpu->device, nullptr, &gpu->context);
		if (FAILED(hr)) {
			fallBackToGdi("no D3D11 hardware device");
			return false;
		}

		ComPtr<IDXGIDevice> dxgiDevice;
		ComPtr<IDXGIAdapter> adapter;
		ComPtr<IDXGIFactory2> factory;
		if (FAILED(gpu->device.As(&dxgiDevice)) || FAILED(dxgiDevice->GetAdapter(&adapter))
			|| FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) {
			fallBackToGdi("couldn't reach the DXGI factory");
			return false;
		}
		if (isSoftwareAdapter(adapter.Get())) {
			fallBackToGdi("only a software adapter is available");
			return false;
		}

		DXGI_SWAP_CHAIN_DESC1 sd = {};
		sd.Width = width;
		sd.Height = height;
		sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		sd.SampleDesc.Count = 1;
		sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		sd.BufferCount = 2;
		sd.Scaling = DXGI_SCALING_NONE;
		sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
		sd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
		hr = factory->CreateSwapChainForHwnd(gpu->device.Get(), window(), &sd, nullptr, nullptr, &gpu->swapChain);
		if (FAILED(hr)) {
			fallBackToGdi("couldn't create a swap chain for this window");
			return false;
		}

		// Stop DXGI from hooking this window's messages (its Alt+Enter fullscreen toggle in particular) -
		// RootView owns its own window's behavior.
		factory->MakeWindowAssociation(window(), DXGI_MWA_NO_WINDOW_CHANGES);

		if (!gpu->createTexture(width, height)) {
			fallBackToGdi("couldn't create the upload texture");
			return false;
		}

		gpu->hwnd = window();
		gpu_ = std::move(gpu);
		needsFullUpload_ = true;
		return true;
	}

	bool DxgiPresentSurface::resizeGpu() {
		if (gpu_ && gpu_->swapChain) {
			const UINT width = UINT(image_.size().w);
			const UINT height = UINT(image_.size().h);

			// ResizeBuffers() needs every outstanding back-buffer reference gone - present() only ever
			// holds one for the duration of a single call, so none can be live here.
			gpu_->texture.Reset();
			if (SUCCEEDED(gpu_->swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0))
				&& gpu_->createTexture(width, height)) {
				return true;
			}
			releaseGpu();  // couldn't resize in place - rebuild from scratch below
		}
		return ensureGpu();
	}

	void DxgiPresentSurface::fallBackToGdi(const std::string& reason) {
		fallbackReason_ = reason;
		const std::string message = "newui: DXGI presentation unavailable (" + reason + ") - using GDI\n";
		::OutputDebugStringA(message.c_str());

		auto gdi = std::make_unique<GdiPresentSurface>();
		gdi->setWindow(window());

		// Carry over whatever's already been rendered.
		if (!image_.is_empty() && gdi->resize(image_.size().w, image_.size().h, format_)) {
			BLImageData from, to;
			image_.get_data(&from);
			gdi->image().get_data(&to);
			const size_t rowBytes = size_t(from.size.w) * 4;
			for (int y = 0; y < from.size.h; ++y) {
				std::memcpy(static_cast<std::uint8_t*>(to.pixel_data) + intptr_t(y) * to.stride,
					static_cast<const std::uint8_t*>(from.pixel_data) + intptr_t(y) * from.stride, rowBytes);
			}
		}

		const int width = image_.size().w;
		const int height = image_.size().h;
		image_.reset();
		releaseGpu();
		gdi_ = std::move(gdi);

		// The window shows nothing until GDI's WM_PAINT path runs, so ask for it.
		gdi_->present(newui::Rect(0.0f, 0.0f, float(width), float(height)));
	}

	void DxgiPresentSurface::present(const newui::Rect& dirty) {
		if (gdi_) {
			gdi_->setWindow(window());
			gdi_->present(dirty);
			return;
		}
		if (image_.is_empty() || window() == nullptr) {
			return;
		}
		if (!ensureGpu()) {
			if (gdi_) {
				gdi_->present(dirty);
			}
			return;
		}

		BLImageData data;
		image_.get_data(&data);
		const int width = data.size.w;
		const int height = data.size.h;

		int left = 0, top = 0, right = width, bottom = height;
		if (!needsFullUpload_) {
			left = clampEdge(std::floor(dirty.left()), width);
			top = clampEdge(std::floor(dirty.top()), height);
			right = clampEdge(std::ceil(dirty.right()), width);
			bottom = clampEdge(std::ceil(dirty.bottom()), height);
			if (right <= left || bottom <= top) {
				return;  // nothing changed (an empty dirty rect, or one entirely outside the buffer)
			}
		}

		// UpdateSubresource() with a box reads its source starting at the box's own top-left texel, so the
		// pointer has to point there - not at the buffer's origin.
		const D3D11_BOX box = { UINT(left), UINT(top), 0, UINT(right), UINT(bottom), 1 };
		const auto* src = static_cast<const std::uint8_t*>(data.pixel_data) + intptr_t(top) * data.stride + intptr_t(left) * 4;
		gpu_->context->UpdateSubresource(gpu_->texture.Get(), 0, &box, src, UINT(data.stride), 0);

		HRESULT hr = S_OK;
		ComPtr<ID3D11Texture2D> backBuffer;
		hr = gpu_->swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
		if (SUCCEEDED(hr)) {
			// The whole texture, not just the box: FLIP_DISCARD leaves the back buffer's contents undefined
			// after each present, so every frame has to be complete. No dirty rects are passed to Present1()
			// either: on this FLIP_DISCARD chain, passing the box came back DXGI_ERROR_INVALID_CALL on the
			// partial-present frames (full-frame presents were fine) - not narrowed down to which rect - so
			// only the *upload* is partial. It's a DWM composition hint, not needed for correctness.
			gpu_->context->CopyResource(backBuffer.Get(), gpu_->texture.Get());

			DXGI_PRESENT_PARAMETERS params = {};
			hr = gpu_->swapChain->Present1(0, 0, &params);
		}

		if (SUCCEEDED(hr)) {
			needsFullUpload_ = false;
			recoveringFromLoss_ = false;
			return;
		}

		if (isDeviceLoss(hr) && !recoveringFromLoss_) {
			// Driver reset/update/TDR: the CPU buffer is intact, so rebuild the device and swap chain and
			// upload the whole thing again. A second loss before any frame gets through isn't recoverable.
			recoveringFromLoss_ = true;
			releaseGpu();
			needsFullUpload_ = true;
			present(newui::Rect(0.0f, 0.0f, float(width), float(height)));
			return;
		}

		char hex[16];
		std::snprintf(hex, sizeof(hex), "0x%08lX", static_cast<unsigned long>(hr));
		fallBackToGdi(std::string(isDeviceLoss(hr) ? "device lost repeatedly" : "Present failed") + ", hr=" + hex);
	}

	void DxgiPresentSurface::paint(HDC hdc, const newui::Rect& paintRect) {
		if (gdi_) {
			gdi_->setWindow(window());
			gdi_->paint(hdc, paintRect);
		}
		// Otherwise nothing: DWM keeps showing the last presented frame, and drawing to this window
		// with GDI while a flip-model swap chain owns it is exactly what causes flicker.
	}

}

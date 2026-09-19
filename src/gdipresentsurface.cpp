#include "newui/gdipresentsurface.h"

#include <cstring>

namespace newui {

	GdiPresentSurface::~GdiPresentSurface() {
		release();
	}

	void GdiPresentSurface::release() {
		// Release the Blend2D wrapper before the DIB section it points
		// into goes away.
		image_.reset();

		if (dibSection_ != nullptr) {
			// Re-select whatever the memory DC originally had (a 1x1 mono
			// stock bitmap) before deleting our own bitmap - deleting a
			// bitmap while it's still selected into a DC is undefined
			// behavior (GDI leaves the DC referencing a half-destroyed
			// object), same reasoning as any other GDI select/delete pair.
			::SelectObject(memDC_, dibSectionOldBitmap_);
			::DeleteObject(dibSection_);
			dibSection_ = nullptr;
			dibSectionOldBitmap_ = nullptr;
		}
		if (memDC_ != nullptr) {
			::DeleteDC(memDC_);
			memDC_ = nullptr;
		}
	}

	bool GdiPresentSurface::resize(int width, int height, BLFormat format) {
		release();

		if (width <= 0 || height <= 0) {
			return false;
		}

		// CreateDIBSection(), not a plain heap buffer wrapped by
		// BLImage::create_from_data() (the original approach) - gives
		// back memory GDI itself already recognizes as a real bitmap
		// object, so paint() can BitBlt() from it directly instead of
		// re-describing a raw pointer via StretchDIBits() on every single
		// WM_PAINT. BitBlt() between two already-realized GDI objects is
		// the faster, more idiomatic Win32 path for a CPU-rendered-then-
		// blitted buffer like this one (StretchDIBits() re-validates the
		// BITMAPINFO header and negotiates pixel format on every call,
		// even for a 1:1 unscaled blit). Blend2D still writes into this
		// memory exactly as before - CreateDIBSection()'s ppvBits is
		// plain, directly-writable pixel memory, just GDI-backed instead
		// of a std::vector.
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height; // negative = top-down, matching Blend2D's row order (see paint())
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void* bits = nullptr;
		memDC_ = ::CreateCompatibleDC(nullptr);
		if (memDC_ == nullptr) {
			return false;
		}

		dibSection_ = ::CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (dibSection_ == nullptr || bits == nullptr) {
			::DeleteDC(memDC_);
			memDC_ = nullptr;
			dibSection_ = nullptr;
			return false;
		}
		dibSectionOldBitmap_ = static_cast<HBITMAP>(::SelectObject(memDC_, dibSection_));

		// 32bpp is always DWORD-aligned regardless of width, so the DIB
		// section's own row stride is exactly width * 4 - no padding to
		// account for, same guarantee the original std::vector-backed
		// buffer's comment already relied on.
		const size_t stride = size_t(width) * 4;

		// CreateDIBSection()'s returned bits are NOT guaranteed zeroed
		// (same fact gfx::Image::createDibBackedImage()'s own comment
		// notes, graphics.cpp) - harmless for the default opaque
		// BL_FORMAT_XRGB32 case (RootView's dirtyRect_ always covers the
		// whole buffer on the first paint after a resize, and paintStyle()'s
		// background fill is normally fully opaque anyway), but load-bearing
		// for a BL_FORMAT_PRGB32 override (RootView::imageBufferFormat())
		// whose background is deliberately left transparent (e.g.
		// PopupTool, popuptool.h) - without this, whatever pixels this DIB
		// section happened to come back with would show through as opaque
		// garbage instead of transparency wherever nothing paints.
		std::memset(bits, 0, stride * size_t(height));

		image_.create_from_data(width, height, format, bits, intptr_t(stride));
		return true;
	}

	void GdiPresentSurface::present(const newui::Rect& dirty) {
		if (window() != nullptr) {
			// Snapped outward to whole pixels: converting a fractional Rect to a RECT truncates every
			// edge, which would leave up to a pixel of the region's right/bottom edge un-invalidated.
			RECT r = dirty.snappedOutwardToPixels();
			::InvalidateRect(window(), &r, FALSE);
		}
	}

	void GdiPresentSurface::paint(HDC hdc, const newui::Rect& paintRect) {
		if (memDC_ == nullptr) {
			return;
		}

		const float width = float(image_.size().w);
		const float height = float(image_.size().h);

		// Outward to whole pixels (a fractional edge otherwise loses the pixel it partly covers), then
		// clipped to the buffer. A rect that isn't entirely inside the buffer still gets its overlap
		// repainted - the window and the buffer are resized in two separate steps, so for a moment WM_PAINT
		// can ask for more than the buffer holds, and dropping the whole rect left that part of the window
		// showing stale pixels. Clamped as floats first so an absurdly large rect can't overflow the int
		// conversion.
		const newui::Rect snapped = paintRect.snappedOutwardToPixels();
		const auto clampTo = [](float value, float limit) { return value < 0.0f ? 0.0f : (value > limit ? limit : value); };
		const int left = int(clampTo(snapped.left(), width));
		const int top = int(clampTo(snapped.top(), height));
		const int right = int(clampTo(snapped.right(), width));
		const int bottom = int(clampTo(snapped.bottom(), height));
		if (right <= left || bottom <= top) {
			return;  // nothing of it overlaps the buffer
		}

		// BitBlt from memDC_ (the DIB section Blend2D renders directly
		// into - see resize()), not StretchDIBits from a raw pointer -
		// dest == src is an unscaled 1:1 blit of just that sub-region, via
		// the faster GDI-to-GDI path (StretchDIBits re-validates a fresh
		// BITMAPINFO header and negotiates pixel format on every call, even
		// for a 1:1 blit between two already-realized bitmap objects BitBlt
		// doesn't need to).
		::BitBlt(hdc, left, top, right - left, bottom - top, memDC_, left, top, SRCCOPY);
	}

}

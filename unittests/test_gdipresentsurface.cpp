#include "newui/gdipresentsurface.h"

#include <gtest/gtest.h>

#include <cstdint>

#include "presentsurface_test_helpers.h"

using presenttest::ScopedWindow;
using presenttest::ScopedTargetDc;

TEST(GdiPresentSurfaceResize, StartsInvalidWithAnEmptyImage) {
    newui::GdiPresentSurface surface;

    EXPECT_FALSE(surface.isValid());
    EXPECT_TRUE(surface.image().is_empty());
}

TEST(GdiPresentSurfaceResize, AllocatesAnImageOfTheRequestedSizeAndFormat) {
    newui::GdiPresentSurface surface;

    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));

    EXPECT_TRUE(surface.isValid());
    BLImageData data;
    surface.image().get_data(&data);
    EXPECT_EQ(data.size.w, 64);
    EXPECT_EQ(data.size.h, 32);
    EXPECT_EQ(surface.image().format(), BL_FORMAT_XRGB32);
    // The DIB section's own stride is exactly width * 4 - the whole reason the buffer is
    // wrapped via create_from_data() instead of BLImage::create() (see GdiPresentSurface).
    EXPECT_EQ(data.stride, intptr_t(64 * 4));
}

TEST(GdiPresentSurfaceResize, HonorsARequestedAlphaFormat) {
    newui::GdiPresentSurface surface;

    ASSERT_TRUE(surface.resize(8, 8, BL_FORMAT_PRGB32));

    EXPECT_EQ(surface.image().format(), BL_FORMAT_PRGB32);
}

TEST(GdiPresentSurfaceResize, NewBufferIsFullyZeroed) {
    newui::GdiPresentSurface surface;

    ASSERT_TRUE(surface.resize(37, 11, BL_FORMAT_PRGB32));  // odd width - no accidental alignment help

    BLImageData data;
    surface.image().get_data(&data);
    const auto* bytes = static_cast<const std::uint8_t*>(data.pixel_data);
    for (int y = 0; y < data.size.h; ++y) {
        for (int x = 0; x < data.size.w * 4; ++x) {
            ASSERT_EQ(bytes[y * data.stride + x], 0) << "non-zero byte at row " << y << ", byte " << x;
        }
    }
}

TEST(GdiPresentSurfaceResize, ResizingReplacesThePreviousBuffer) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));

    ASSERT_TRUE(surface.resize(20, 10, BL_FORMAT_XRGB32));

    BLImageData data;
    surface.image().get_data(&data);
    EXPECT_EQ(data.size.w, 20);
    EXPECT_EQ(data.size.h, 10);
}

TEST(GdiPresentSurfaceResize, NonPositiveSizeReleasesAndReturnsFalse) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));

    EXPECT_FALSE(surface.resize(0, 32, BL_FORMAT_XRGB32));

    EXPECT_FALSE(surface.isValid());
    EXPECT_TRUE(surface.image().is_empty());
    EXPECT_FALSE(surface.resize(64, -1, BL_FORMAT_XRGB32));
    EXPECT_FALSE(surface.isValid());
}

TEST(GdiPresentSurfaceRelease, ReleasesAndIsSafeToCallTwice) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(16, 16, BL_FORMAT_XRGB32));

    surface.release();
    surface.release();

    EXPECT_FALSE(surface.isValid());
    EXPECT_TRUE(surface.image().is_empty());
}

TEST(GdiPresentSurfacePaint, BlitsTheRequestedRegionOfTheImageOntoTheDc) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    {
        BLContext ctx(surface.image());
        ctx.set_fill_style(BLRgba32(0xFFFF0000));  // opaque red
        ctx.fill_rect(BLRect(10, 5, 20, 10));
        ctx.end();
    }
    ScopedTargetDc target(64, 32);

    surface.paint(target.dc, newui::Rect(0, 0, 64, 32));

    EXPECT_EQ(::GetPixel(target.dc, 15, 8), RGB(255, 0, 0));  // inside the red rect
    EXPECT_EQ(::GetPixel(target.dc, 2, 2), RGB(0, 0, 0));     // outside it: the image's own zero fill, copied over the blue
}

TEST(GdiPresentSurfacePaint, OnlyTouchesThePaintRect) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    ScopedTargetDc target(64, 32);

    surface.paint(target.dc, newui::Rect(0, 0, 10, 10));

    EXPECT_EQ(::GetPixel(target.dc, 5, 5), RGB(0, 0, 0));      // inside the rect: copied
    EXPECT_EQ(::GetPixel(target.dc, 30, 20), RGB(0, 0, 255));  // outside: still the DC's own blue
}

namespace {

    // A surface whose whole buffer is opaque red, so "was this pixel copied?" is a plain red/blue check
    // against a target DC that starts solid blue.
    void fillRed(newui::GdiPresentSurface& surface) {
        BLContext ctx(surface.image());
        ctx.set_fill_style(BLRgba32(0xFFFF0000));
        ctx.fill_all();
        ctx.end();
    }

}

// A WM_PAINT rect that isn't entirely inside the buffer (the window and the buffer are resized in two
// separate steps, so for a moment they can disagree) must still repaint the part that IS - dropping the
// whole rect left that part of the window showing stale pixels.

TEST(GdiPresentSurfacePaint, RectExtendingPastTheImageIsClippedToItNotDropped) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    fillRed(surface);
    ScopedTargetDc target(80, 40);

    surface.paint(target.dc, newui::Rect(0, 0, 80, 40));  // wider and taller than the image

    EXPECT_EQ(::GetPixel(target.dc, 5, 5), RGB(255, 0, 0)) << "the part inside the buffer must be copied";
    EXPECT_EQ(::GetPixel(target.dc, 63, 31), RGB(255, 0, 0)) << "right up to the buffer's last pixel";
    EXPECT_EQ(::GetPixel(target.dc, 70, 5), RGB(0, 0, 255)) << "beyond the buffer's right edge: untouched";
    EXPECT_EQ(::GetPixel(target.dc, 5, 35), RGB(0, 0, 255)) << "beyond the buffer's bottom edge: untouched";
}

TEST(GdiPresentSurfacePaint, RectHangingOffTheTopLeftIsClippedToo) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    fillRed(surface);
    ScopedTargetDc target(64, 32);

    surface.paint(target.dc, newui::Rect(-10, -10, 30, 30));  // covers (0,0)-(20,20) of the buffer

    EXPECT_EQ(::GetPixel(target.dc, 0, 0), RGB(255, 0, 0));
    EXPECT_EQ(::GetPixel(target.dc, 19, 19), RGB(255, 0, 0));
    EXPECT_EQ(::GetPixel(target.dc, 25, 25), RGB(0, 0, 255));
}

TEST(GdiPresentSurfacePaint, RectEntirelyOutsideTheImageTouchesNothing) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    fillRed(surface);
    ScopedTargetDc target(128, 64);

    surface.paint(target.dc, newui::Rect(100, 50, 10, 10));  // well past both edges
    surface.paint(target.dc, newui::Rect(64, 0, 10, 10));    // starts exactly at the right edge
    surface.paint(target.dc, newui::Rect(0, 32, 10, 10));    // starts exactly at the bottom edge
    surface.paint(target.dc, newui::Rect(-50, -50, 10, 10)); // entirely above/left of it

    for (POINT p : { POINT{ 105, 55 }, POINT{ 66, 5 }, POINT{ 5, 34 }, POINT{ 0, 0 } }) {
        EXPECT_EQ(::GetPixel(target.dc, p.x, p.y), RGB(0, 0, 255)) << "at (" << p.x << "," << p.y << ")";
    }
}

TEST(GdiPresentSurfacePaint, ZeroSizedRectIsANoOp) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    fillRed(surface);
    ScopedTargetDc target(64, 32);

    surface.paint(target.dc, newui::Rect(10, 10, 0, 0));

    EXPECT_EQ(::GetPixel(target.dc, 10, 10), RGB(0, 0, 255));
}

TEST(GdiPresentSurfacePaint, AFractionalRectIsSnappedOutwardToWholePixels) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(64, 32, BL_FORMAT_XRGB32));
    fillRed(surface);
    ScopedTargetDc target(64, 32);

    surface.paint(target.dc, newui::Rect(10.5f, 10.5f, 4.0f, 4.0f));  // spans 10.5..14.5 -> pixels 10..14

    EXPECT_EQ(::GetPixel(target.dc, 10, 10), RGB(255, 0, 0)) << "the partially covered leading pixel";
    EXPECT_EQ(::GetPixel(target.dc, 14, 14), RGB(255, 0, 0)) << "the partially covered trailing pixel";
    EXPECT_EQ(::GetPixel(target.dc, 9, 9), RGB(0, 0, 255));
    EXPECT_EQ(::GetPixel(target.dc, 15, 15), RGB(0, 0, 255));
}

TEST(GdiPresentSurfacePaint, DoesNothingWhenNoBufferIsAllocated) {
    newui::GdiPresentSurface surface;
    ScopedTargetDc target(16, 16);

    surface.paint(target.dc, newui::Rect(0, 0, 16, 16));

    EXPECT_EQ(::GetPixel(target.dc, 5, 5), RGB(0, 0, 255));
}

TEST(GdiPresentSurfacePresent, WithoutAWindowIsANoOp) {
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(16, 16, BL_FORMAT_XRGB32));

    surface.present(newui::Rect(0, 0, 16, 16));  // must not crash
}

TEST(GdiPresentSurfacePresent, InvalidatesTheWindowsDirtyRegion) {
    ScopedWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    ::ValidateRect(window.hwnd, nullptr);
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(100, 100, BL_FORMAT_XRGB32));
    surface.setWindow(window.hwnd);
    ASSERT_FALSE(::GetUpdateRect(window.hwnd, nullptr, FALSE));  // premise: nothing pending

    surface.present(newui::Rect(10, 20, 30, 40));

    RECT pending = {};
    ASSERT_TRUE(::GetUpdateRect(window.hwnd, &pending, FALSE));
    EXPECT_EQ(pending.left, 10);
    EXPECT_EQ(pending.top, 20);
    EXPECT_EQ(pending.right, 40);
    EXPECT_EQ(pending.bottom, 60);
}

TEST(PresentSurfaceWindow, SetWindowIsReadBackAndClearable) {
    newui::GdiPresentSurface surface;
    EXPECT_EQ(surface.window(), nullptr);

    surface.setWindow(reinterpret_cast<HWND>(0x1234));
    EXPECT_EQ(surface.window(), reinterpret_cast<HWND>(0x1234));

    surface.setWindow(nullptr);
    EXPECT_EQ(surface.window(), nullptr);
}

TEST(GdiPresentSurfacePresent, AFractionalRectIsSnappedOutwardSoNoEdgePixelIsMissed) {
    ScopedWindow window;
    ASSERT_NE(window.hwnd, nullptr);
    ::ValidateRect(window.hwnd, nullptr);
    newui::GdiPresentSurface surface;
    ASSERT_TRUE(surface.resize(100, 100, BL_FORMAT_XRGB32));
    surface.setWindow(window.hwnd);

    surface.present(newui::Rect(10.5f, 20.5f, 30.2f, 40.2f));  // right = 40.7, bottom = 60.7

    RECT pending = {};
    ASSERT_TRUE(::GetUpdateRect(window.hwnd, &pending, FALSE));
    EXPECT_EQ(pending.left, 10);
    EXPECT_EQ(pending.top, 20);
    EXPECT_EQ(pending.right, 41) << "truncating 40.7 would leave the last column un-invalidated";
    EXPECT_EQ(pending.bottom, 61);
}

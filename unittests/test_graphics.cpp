#include "newui/graphics.h"

#include <gtest/gtest.h>

#include <utility>

namespace {

void WriteTestPNG(const std::string& path, int width, int height) {
    BLImage image(width, height, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(0, 200, 0, 180));
    ctx.fill_all();
    ctx.end();
    ASSERT_EQ(image.write_to_file(path.c_str()), BL_SUCCESS);
}

}  // namespace

TEST(Image, DefaultConstructedIsInvalid) {
    newui::Image image;

    EXPECT_FALSE(image.isValid());
    EXPECT_EQ(image.width(), 0);
    EXPECT_EQ(image.height(), 0);
}

TEST(Image, BlankCanvasIsValidAndZeroFilled) {
    newui::Image image(16, 8);

    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.width(), 16);
    EXPECT_EQ(image.height(), 8);

    BLImageData data;
    image.blImage().get_data(&data);
    const uint8_t* row0 = static_cast<const uint8_t*>(data.pixel_data);
    for (int x = 0; x < 16 * 4; ++x) {
        EXPECT_EQ(row0[x], 0);
    }
}

TEST(Image, InvalidForZeroOrNegativeSize) {
    newui::Image zero(0, 8);
    newui::Image negative(8, -1);

    EXPECT_FALSE(zero.isValid());
    EXPECT_FALSE(negative.isValid());
}

TEST(Image, BlImageIsLiveReadWriteAndAffectsMemDCBits) {
    newui::Image image(4, 4);
    ASSERT_TRUE(image.isValid());

    {
        BLContext ctx(image.blImage());
        ctx.set_fill_style(BLRgba32(255, 0, 0, 255));
        ctx.fill_all();
        ctx.end();
    }

    BLImageData data;
    image.blImage().get_data(&data);
    const uint32_t* pixel = static_cast<const uint32_t*>(data.pixel_data);
    EXPECT_EQ(*pixel, 0xFFFF0000u);  // premultiplied ARGB, opaque red
}

TEST(Image, LoadFromFileFailsForAMissingFile) {
    newui::Image image("NoSuchImageFile.png");

    EXPECT_FALSE(image.isValid());
}

TEST(Image, LoadFromFileRoundTripsARealPNGFile) {
    const std::string path = "graphics_test_image.png";
    WriteTestPNG(path, 10, 6);

    newui::Image image(path);

    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.width(), 10);
    EXPECT_EQ(image.height(), 6);

    ::DeleteFileA(path.c_str());
}

TEST(Image, ConstructFromExistingBLImageCopiesItsPixels) {
    BLImage source(4, 4, BL_FORMAT_PRGB32);
    {
        BLContext ctx(source);
        ctx.set_fill_style(BLRgba32(0, 0, 255, 255));
        ctx.fill_all();
        ctx.end();
    }

    newui::Image image(source);

    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.width(), 4);
    EXPECT_EQ(image.height(), 4);

    BLImageData data;
    image.blImage().get_data(&data);
    const uint32_t* pixel = static_cast<const uint32_t*>(data.pixel_data);
    EXPECT_EQ(*pixel, 0xFF0000FFu);  // premultiplied ARGB, opaque blue
}

TEST(Image, ConstructFromEmptyBLImageIsInvalid) {
    BLImage source;  // never create()'d

    newui::Image image(source);

    EXPECT_FALSE(image.isValid());
}

TEST(Image, MemDCReturnsNullptrWhenInvalid) {
    newui::Image image;

    EXPECT_EQ(image.memDC(), nullptr);
}

TEST(Image, MemDCReturnsARealDCSelectedWithTheBackingDIB) {
    newui::Image image(8, 8);
    ASSERT_TRUE(image.isValid());

    HDC dc = image.memDC();
    ASSERT_NE(dc, nullptr);

    // Same HDC every call - created lazily once, then cached.
    EXPECT_EQ(image.memDC(), dc);
}

TEST(Image, GdiDrawingIntoMemDCIsVisibleThroughBlImage) {
    newui::Image image(8, 8);
    ASSERT_TRUE(image.isValid());

    HDC dc = image.memDC();
    ASSERT_NE(dc, nullptr);

    // A plain GDI fill (no blend2d involved at all) into memDC() should
    // land in the exact same buffer blImage() reads - the whole point of
    // sharing one DIB section between the two.
    RECT rect{0, 0, 8, 8};
    HBRUSH brush = ::CreateSolidBrush(RGB(0, 255, 0));
    ::FillRect(dc, &rect, brush);
    ::DeleteObject(brush);
    ::GdiFlush();

    BLImageData data;
    image.blImage().get_data(&data);
    const uint32_t* pixel = static_cast<const uint32_t*>(data.pixel_data);
    // GDI writes plain (non-premultiplied, alpha-untouched) BGR bytes -
    // green channel should be 0xFF, red/blue 0.
    EXPECT_EQ(*pixel & 0x00FFFFFF, 0x0000FF00u);
}

TEST(Image, MoveConstructorTransfersStateAndLeavesSourceInvalid) {
    newui::Image source(4, 4);
    ASSERT_TRUE(source.isValid());
    HDC dc = source.memDC();
    ASSERT_NE(dc, nullptr);

    newui::Image moved(std::move(source));

    EXPECT_TRUE(moved.isValid());
    EXPECT_EQ(moved.width(), 4);
    EXPECT_EQ(moved.memDC(), dc);

    EXPECT_FALSE(source.isValid());
    EXPECT_EQ(source.memDC(), nullptr);
}

TEST(Image, MoveAssignmentReleasesTheTargetsOwnedResourcesFirst) {
    newui::Image a(4, 4);
    ASSERT_TRUE(a.isValid());
    ASSERT_NE(a.memDC(), nullptr);  // give a a live memDC_/dibSection_ pair to release

    newui::Image b(6, 6);
    ASSERT_TRUE(b.isValid());
    HDC bDC = b.memDC();

    // a's originally-owned GDI resources should be released here, not
    // leaked - no directly observable side effect beyond "doesn't
    // crash", same caveat as Cursor's own replace-an-owned-handle tests.
    a = std::move(b);

    EXPECT_EQ(a.width(), 6);
    EXPECT_EQ(a.memDC(), bDC);
}

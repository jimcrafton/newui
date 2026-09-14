#include "newui/graphics.h"
#include "newui/svgimage.h"

#include <gtest/gtest.h>

#include <fstream>
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

// A tiny, fast, deterministic SVG - a single opaque red rect filling its
// own 10x10 viewBox - rather than svgandme's own (much larger, much
// slower to parse/render) gallery samples, since these tests only care
// that ".svg" actually routes through renderSvgFile() and comes back with
// real, correctly-sized/colored pixels.
void WriteTestSVG(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
            "<rect width=\"10\" height=\"10\" fill=\"#ff0000\"/></svg>";
}

}  // namespace

TEST(Image, DefaultConstructedIsInvalid) {
    newui::gfx::Image image;

    EXPECT_FALSE(image.isValid());
    EXPECT_EQ(image.width(), 0);
    EXPECT_EQ(image.height(), 0);
}

TEST(Image, BlankCanvasIsValidAndZeroFilled) {
    newui::gfx::Image image(16, 8);

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
    newui::gfx::Image zero(0, 8);
    newui::gfx::Image negative(8, -1);

    EXPECT_FALSE(zero.isValid());
    EXPECT_FALSE(negative.isValid());
}

TEST(Image, BlImageIsLiveReadWriteAndAffectsMemDCBits) {
    newui::gfx::Image image(4, 4);
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
    newui::gfx::Image image("NoSuchImageFile.png");

    EXPECT_FALSE(image.isValid());
}

TEST(Image, LoadFromFileRoundTripsARealPNGFile) {
    const std::string path = "graphics_test_image.png";
    WriteTestPNG(path, 10, 6);

    newui::gfx::Image image(path);

    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.width(), 10);
    EXPECT_EQ(image.height(), 6);

    ::DeleteFileA(path.c_str());
}

TEST(Image, SvgPathWithNoSizeRasterizesAtTheFixedDefaultSize) {
    const std::string path = "graphics_test_image.svg";
    WriteTestSVG(path);

    newui::gfx::Image image(path);

    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.width(), newui::kDefaultSvgRasterSize);
    EXPECT_EQ(image.height(), newui::kDefaultSvgRasterSize);

    ::DeleteFileA(path.c_str());
}

TEST(Image, SvgPathWithExplicitSizeRasterizesAtThatSize) {
    const std::string path = "graphics_test_image_sized.svg";
    WriteTestSVG(path);

    newui::gfx::Image image(path, 20, 12);

    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.width(), 20);
    EXPECT_EQ(image.height(), 12);

    // The whole viewBox is a solid opaque red rect, so the center pixel of
    // the rasterized result should come back opaque red regardless of the
    // requested size.
    BLImageData data;
    image.blImage().get_data(&data);
    const auto* row = static_cast<const uint8_t*>(data.pixel_data) + 6 * data.stride;
    const uint32_t centerPixel = reinterpret_cast<const uint32_t*>(row)[10];
    EXPECT_EQ(centerPixel, 0xFFFF0000u);  // premultiplied ARGB, opaque red

    ::DeleteFileA(path.c_str());
}

TEST(Image, SvgPathFailsForAMissingFile) {
    newui::gfx::Image image("NoSuchImageFile.svg", 16, 16);

    EXPECT_FALSE(image.isValid());
}

TEST(Image, SizedConstructorRescalesANonSvgFormatToo) {
    const std::string path = "graphics_test_image_rescale.png";
    WriteTestPNG(path, 10, 6);

    newui::gfx::Image image(path, 20, 12);

    ASSERT_TRUE(image.isValid());
    EXPECT_EQ(image.width(), 20);
    EXPECT_EQ(image.height(), 12);

    ::DeleteFileA(path.c_str());
}

TEST(Image, SizedConstructorInvalidForZeroOrNegativeSize) {
    const std::string path = "graphics_test_image_badsize.png";
    WriteTestPNG(path, 10, 6);

    newui::gfx::Image zero(path, 0, 10);
    newui::gfx::Image negative(path, 10, -1);

    EXPECT_FALSE(zero.isValid());
    EXPECT_FALSE(negative.isValid());

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

    newui::gfx::Image image(source);

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

    newui::gfx::Image image(source);

    EXPECT_FALSE(image.isValid());
}

TEST(Image, MemDCReturnsNullptrWhenInvalid) {
    newui::gfx::Image image;

    EXPECT_EQ(image.memDC(), nullptr);
}

TEST(Image, MemDCReturnsARealDCSelectedWithTheBackingDIB) {
    newui::gfx::Image image(8, 8);
    ASSERT_TRUE(image.isValid());

    HDC dc = image.memDC();
    ASSERT_NE(dc, nullptr);

    // Same HDC every call - created lazily once, then cached.
    EXPECT_EQ(image.memDC(), dc);
}

TEST(Image, GdiDrawingIntoMemDCIsVisibleThroughBlImage) {
    newui::gfx::Image image(8, 8);
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
    newui::gfx::Image source(4, 4);
    ASSERT_TRUE(source.isValid());
    HDC dc = source.memDC();
    ASSERT_NE(dc, nullptr);

    newui::gfx::Image moved(std::move(source));

    EXPECT_TRUE(moved.isValid());
    EXPECT_EQ(moved.width(), 4);
    EXPECT_EQ(moved.memDC(), dc);

    EXPECT_FALSE(source.isValid());
    EXPECT_EQ(source.memDC(), nullptr);
}

// Real bug, live-reported downstream (cpp_codetools' gradient editor dialog): Linear/Radial/
// Conic's own geometry (linearStart_/linearEnd_ etc.) used to be absolute local-space pixel
// coordinates, frozen at whatever they were set to - applying a gradient looked right once, but
// resizing whatever it was painted onto left the geometry stale (a gradient authored to span a
// 100px-wide box rendered compressed into the leftmost 100px of a much wider one, padding the
// rest with the last stop's color). Now proportional [0,1] fractions of localBounds, resolved
// fresh on every toBLVar() call - these tests drive the real public API (toBLVar()), not the
// private toBLGradient() directly, and check the same box twice at two different sizes to prove
// it isn't a cached/frozen snapshot.
TEST(Gradient, LinearDefaultSpansTheFullWidthOfWhateverBoxItsResolvedAgainst) {
    newui::gfx::Gradient gradient;
    gradient.setKind(newui::gfx::GradientKind::Linear);
    gradient.stops().push_back(newui::gfx::GradientStop(0.0f, newui::Color(1.0f, 0.0f, 0.0f)));
    gradient.stops().push_back(newui::gfx::GradientStop(1.0f, newui::Color(0.0f, 0.0f, 1.0f)));

    BLVar smallBox = gradient.toBLVar(newui::Rect(0.0f, 0.0f, 100.0f, 50.0f));
    ASSERT_TRUE(smallBox.is_gradient());
    const BLLinearGradientValues& smallValues = smallBox.as<BLGradient>().linear();
    EXPECT_DOUBLE_EQ(smallValues.x0, 0.0);
    EXPECT_DOUBLE_EQ(smallValues.x1, 100.0);

    // A much wider box, same Gradient object, no re-authoring - the whole point of resolving
    // proportionally at paint time instead of baking absolute coordinates in once.
    BLVar wideBox = gradient.toBLVar(newui::Rect(0.0f, 0.0f, 400.0f, 50.0f));
    ASSERT_TRUE(wideBox.is_gradient());
    const BLLinearGradientValues& wideValues = wideBox.as<BLGradient>().linear();
    EXPECT_DOUBLE_EQ(wideValues.x0, 0.0);
    EXPECT_DOUBLE_EQ(wideValues.x1, 400.0);
}

TEST(Gradient, LinearGeometryOffsetsWithANonZeroOriginBox) {
    newui::gfx::Gradient gradient;
    gradient.setKind(newui::gfx::GradientKind::Linear);

    BLVar result = gradient.toBLVar(newui::Rect(10.0f, 20.0f, 200.0f, 60.0f));
    ASSERT_TRUE(result.is_gradient());
    const BLLinearGradientValues& values = result.as<BLGradient>().linear();
    EXPECT_DOUBLE_EQ(values.x0, 10.0);
    EXPECT_DOUBLE_EQ(values.y0, 20.0);
    EXPECT_DOUBLE_EQ(values.x1, 210.0);
    EXPECT_DOUBLE_EQ(values.y1, 20.0);
}

TEST(Gradient, RadialDefaultIsACircleCenteredInAndTouchingTheBoxsShorterSide) {
    newui::gfx::Gradient gradient;
    gradient.setKind(newui::gfx::GradientKind::Radial);

    BLVar result = gradient.toBLVar(newui::Rect(0.0f, 0.0f, 100.0f, 50.0f));
    ASSERT_TRUE(result.is_gradient());
    const BLRadialGradientValues& values = result.as<BLGradient>().radial();
    EXPECT_DOUBLE_EQ(values.x0, 50.0);
    EXPECT_DOUBLE_EQ(values.y0, 25.0);
    EXPECT_DOUBLE_EQ(values.r0, 25.0);  // half the shorter side (50), not the longer (100)
}

TEST(Gradient, ConicDefaultIsCenteredInTheBox) {
    newui::gfx::Gradient gradient;
    gradient.setKind(newui::gfx::GradientKind::Conic);

    BLVar result = gradient.toBLVar(newui::Rect(0.0f, 0.0f, 100.0f, 50.0f));
    ASSERT_TRUE(result.is_gradient());
    const BLConicGradientValues& values = result.as<BLGradient>().conic();
    EXPECT_DOUBLE_EQ(values.x0, 50.0);
    EXPECT_DOUBLE_EQ(values.y0, 25.0);
}

TEST(Image, MoveAssignmentReleasesTheTargetsOwnedResourcesFirst) {
    newui::gfx::Image a(4, 4);
    ASSERT_TRUE(a.isValid());
    ASSERT_NE(a.memDC(), nullptr);  // give a a live memDC_/dibSection_ pair to release

    newui::gfx::Image b(6, 6);
    ASSERT_TRUE(b.isValid());
    HDC bDC = b.memDC();

    // a's originally-owned GDI resources should be released here, not
    // leaked - no directly observable side effect beyond "doesn't
    // crash", same caveat as Cursor's own replace-an-owned-handle tests.
    a = std::move(b);

    EXPECT_EQ(a.width(), 6);
    EXPECT_EQ(a.memDC(), bDC);
}

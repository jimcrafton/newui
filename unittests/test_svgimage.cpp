#include "newui/svgimage.h"

#include <gtest/gtest.h>

#include <windows.h>

#include <fstream>

namespace {

// A tiny, fast, deterministic SVG - a single opaque red rect filling its
// own 10x10 viewBox - rather than svgandme's own (much larger, much
// slower to parse/render) gallery samples; these tests only care that
// renderSvgFile() actually parses/rasterizes/copies pixels correctly, not
// about real-world rendering fidelity.
void WriteTestSVG(const std::string& path) {
    std::ofstream file(path, std::ios::binary);
    file << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
            "<rect width=\"10\" height=\"10\" fill=\"#ff0000\"/></svg>";
}

}  // namespace

TEST(RenderSvgFile, FailsForAMissingFile) {
    BLImage image;
    EXPECT_FALSE(newui::renderSvgFile("NoSuchFile.svg", 16, 16, image));
}

TEST(RenderSvgFile, FailsForZeroOrNegativeSize) {
    const std::string path = "svgimage_test_badsize.svg";
    WriteTestSVG(path);

    BLImage image;
    EXPECT_FALSE(newui::renderSvgFile(path, 0, 16, image));
    EXPECT_FALSE(newui::renderSvgFile(path, 16, -1, image));

    ::DeleteFileA(path.c_str());
}

TEST(RenderSvgFile, FailsForAFileThatIsNotValidSvg) {
    const std::string path = "svgimage_test_notsvg.svg";
    {
        std::ofstream file(path, std::ios::binary);
        file << "this is not xml at all { } < >";
    }

    BLImage image;
    EXPECT_FALSE(newui::renderSvgFile(path, 16, 16, image));

    ::DeleteFileA(path.c_str());
}

TEST(RenderSvgFile, RasterizesAtTheRequestedSize) {
    const std::string path = "svgimage_test_size.svg";
    WriteTestSVG(path);

    BLImage image;
    ASSERT_TRUE(newui::renderSvgFile(path, 24, 16, image));
    EXPECT_EQ(image.size().w, 24);
    EXPECT_EQ(image.size().h, 16);
    EXPECT_EQ(image.format(), BL_FORMAT_PRGB32);

    ::DeleteFileA(path.c_str());
}

TEST(RenderSvgFile, RasterizesTheActualDocumentContent) {
    const std::string path = "svgimage_test_content.svg";
    WriteTestSVG(path);

    BLImage image;
    ASSERT_TRUE(newui::renderSvgFile(path, 10, 10, image));

    BLImageData data;
    image.get_data(&data);
    // The whole 10x10 viewBox is a solid opaque red rect - every corner
    // and the center should come back the same premultiplied ARGB value.
    const auto pixelAt = [&](int x, int y) {
        const auto* row = static_cast<const uint8_t*>(data.pixel_data) + size_t(y) * data.stride;
        return reinterpret_cast<const uint32_t*>(row)[x];
    };
    EXPECT_EQ(pixelAt(0, 0), 0xFFFF0000u);
    EXPECT_EQ(pixelAt(9, 9), 0xFFFF0000u);
    EXPECT_EQ(pixelAt(5, 5), 0xFFFF0000u);

    ::DeleteFileA(path.c_str());
}

TEST(RenderSvgFile, LeavesAreasOutsideTheDocumentTransparentNotGarbage) {
    // A viewBox narrower than the requested canvas - preserveAspectRatio's
    // default (xMidYMid meet) letterboxes rather than stretching, so the
    // left/right margins should come back transparent - this is what
    // exercises the explicit Surface-clear in renderSvgFile() (a freshly
    // allocated Surface's buffer is NOT zero-initialized on its own).
    const std::string path = "svgimage_test_letterbox.svg";
    {
        std::ofstream file(path, std::ios::binary);
        file << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 10 10\">"
                "<rect width=\"10\" height=\"10\" fill=\"#ff0000\"/></svg>";
    }

    BLImage image;
    ASSERT_TRUE(newui::renderSvgFile(path, 40, 10, image));  // 4:1, way wider than the 1:1 viewBox

    BLImageData data;
    image.get_data(&data);
    const auto* row0 = static_cast<const uint8_t*>(data.pixel_data);
    const uint32_t leftMargin = reinterpret_cast<const uint32_t*>(row0)[0];
    EXPECT_EQ(leftMargin, 0x00000000u);  // fully transparent, not uninitialized memory

    ::DeleteFileA(path.c_str());
}

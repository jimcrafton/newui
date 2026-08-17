#include "newui/cursor.h"

#include <gtest/gtest.h>

#include <blend2d/blend2d.h>

#include <utility>

// View's own use of Cursor (cursor()/setCursor(Cursor)/cursorKind()/
// resolvedCursor()) is covered by ViewCursor.* in test_view.cpp - this
// file only covers the Cursor class itself (cursor.h). resolveCursor()/
// createCursorFromImage()/loadCursorFromFile() used to be separately
// testable free functions but are now private implementation details of
// Cursor (anonymous namespace in cursor.cpp) - every case below that used
// to call one of those directly goes through the matching Cursor
// constructor/setter instead.

namespace {

// A small in-memory image with real per-pixel alpha (a solid half-
// transparent fill) - enough to exercise Cursor::setImage()'s alpha-DIB
// path without needing a real file on disk.
BLImage MakeTestImage(int width, int height) {
    BLImage image(width, height, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(255, 0, 0, 128));
    ctx.fill_all();
    ctx.end();
    return image;
}

// write_to_file()'s codec is inferred from path's extension - ".png"
// picks blend2d's built-in PNG encoder, the same codec set
// BLImage::read_from_file() decodes (see Bundle::loadImage() in
// bundle.cpp - PNG support isn't new here, Cursor::setPath() just reuses
// blend2d's existing codecs).
void WriteTestPNG(const std::string& path, int width, int height) {
    BLImage image = MakeTestImage(width, height);
    ASSERT_EQ(image.write_to_file(path.c_str()), BL_SUCCESS);
}

}  // namespace

TEST(Cursor, CursorKindStringRoundTripsThroughToStringFromString) {
    for (newui::CursorKind kind : {newui::CursorKind::Arrow, newui::CursorKind::IBeam, newui::CursorKind::Wait,
            newui::CursorKind::Cross, newui::CursorKind::Hand, newui::CursorKind::SizeNS, newui::CursorKind::SizeWE,
            newui::CursorKind::SizeNWSE, newui::CursorKind::SizeNESW, newui::CursorKind::SizeAll, newui::CursorKind::No,
            newui::CursorKind::AppStarting, newui::CursorKind::Help}) {
        EXPECT_EQ(newui::Cursor::cursorKindFromString(newui::Cursor::cursorKindToString(kind), newui::CursorKind::Custom), kind);
    }
}

// Separate from the loop above (which uses Custom as every case's
// "fallback if not recognized" default) - a defaultValue of Arrow here
// means this only passes if "Custom" is genuinely recognized as its own
// string, not by accidentally matching the fallback.
TEST(Cursor, CursorKindFromStringRecognizesCustomItself) {
    EXPECT_EQ(newui::Cursor::cursorKindFromString(newui::Cursor::cursorKindToString(newui::CursorKind::Custom), newui::CursorKind::Arrow),
        newui::CursorKind::Custom);
}

TEST(Cursor, CursorKindFromStringFallsBackToDefaultOnUnknownString) {
    EXPECT_EQ(newui::Cursor::cursorKindFromString("NotARealCursor", newui::CursorKind::Wait), newui::CursorKind::Wait);
}

TEST(Cursor, KindConstructorResolvesToTheMatchingSystemHandle) {
    auto h = newui::Cursor(newui::CursorKind::Hand);
    EXPECT_EQ(h.handle(), ::LoadCursorW(nullptr, IDC_HAND));
    auto ib = newui::Cursor(newui::CursorKind::IBeam);
    EXPECT_EQ(ib.handle(), ::LoadCursorW(nullptr, IDC_IBEAM));
}

TEST(Cursor, ImageConstructorBuildsARealCursorWithinTheSizeLimit) {
    BLImage image = MakeTestImage(16, 16);

    auto h = newui::Cursor(image);

    // h's own destructor (~Cursor() -> releaseOwnedHandle()) frees this -
    // no explicit ::DestroyCursor() here, that would double-free it.
    EXPECT_NE(h.handle(), nullptr);
    EXPECT_EQ(h.kind(), newui::CursorKind::Custom);
}

TEST(Cursor, ImageConstructorFailsWhenEitherDimensionExceedsMaxSize) {
    BLImage tooWide = MakeTestImage(40, 16);
    BLImage tooTall = MakeTestImage(16, 40);

    // Cursor's "load failed" contract is "stay at whatever it was before"
    // (the untouched default, Arrow, here) - not a null handle(), since
    // handle() always resolves to a real, showable cursor. isNull()/
    // kind() are what actually reflect a failed load.
    auto h = newui::Cursor(tooWide);
    EXPECT_EQ(h.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(h.isNull());

    auto j = newui::Cursor(tooTall);
    EXPECT_EQ(j.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(j.isNull());
}

TEST(Cursor, ImageConstructorAllowsExactlyMaxSize) {
    BLImage image = MakeTestImage(32, 32);

    auto h = newui::Cursor(image);

    EXPECT_NE(h.handle(), nullptr);
    EXPECT_EQ(h.kind(), newui::CursorKind::Custom);
}

TEST(Cursor, ImageConstructorFailsOnAnEmptyImage) {
    BLImage image;  // never create()'d - empty, format() == BL_FORMAT_NONE

    auto h = newui::Cursor(image);

    EXPECT_EQ(h.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(h.isNull());
}

TEST(Cursor, PathConstructorFailsForAMissingFile) {
    auto h = newui::Cursor("NoSuchCursorFile.png");

    EXPECT_EQ(h.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(h.isNull());
}

TEST(Cursor, PathConstructorRoundTripsARealPNGFile) {
    const std::string path = "cursor_test_small.png";
    WriteTestPNG(path, 16, 16);

    auto h = newui::Cursor(path);

    EXPECT_NE(h.handle(), nullptr);
    EXPECT_EQ(h.kind(), newui::CursorKind::Custom);
    ::DeleteFileA(path.c_str());
}

TEST(Cursor, PathConstructorFailsWhenThePNGExceedsMaxSize) {
    const std::string path = "cursor_test_large.png";
    WriteTestPNG(path, 40, 40);

    auto h = newui::Cursor(path);

    EXPECT_EQ(h.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(h.isNull());
    ::DeleteFileA(path.c_str());
}

// ---------------------------------------------------------------------------
// The Cursor class itself (cursor.h) - kind()/path()/handle()/isNull()/
// ownsHandle(), each setCursorKind()/setPath()/setImage(), move semantics.
// ---------------------------------------------------------------------------

TEST(CursorClass, DefaultConstructedIsArrowAndNull) {
    newui::Cursor cursor;

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(cursor.path().empty());
    EXPECT_TRUE(cursor.isNull());
    EXPECT_TRUE(cursor.empty());
    EXPECT_FALSE(cursor.ownsHandle());
    // Still resolves to a real, showable handle despite isNull() - isNull()
    // is specifically about the Custom-cursor payload (path/handle), not
    // about whether handle() has something to show.
    EXPECT_EQ(cursor.handle(), ::LoadCursorW(nullptr, IDC_ARROW));
}

TEST(CursorClass, KindConstructorSetsKindAndStaysNull) {
    newui::Cursor cursor(newui::CursorKind::Hand);

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Hand);
    EXPECT_TRUE(cursor.isNull());
    EXPECT_FALSE(cursor.ownsHandle());
    EXPECT_EQ(cursor.handle(), ::LoadCursorW(nullptr, IDC_HAND));
}

TEST(CursorClass, PathConstructorLoadsAndOwnsTheHandle) {
    const std::string path = "cursor_class_test_ctor.png";
    WriteTestPNG(path, 16, 16);

    newui::Cursor cursor(path);

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Custom);
    EXPECT_EQ(cursor.path(), path);
    EXPECT_FALSE(cursor.isNull());
    EXPECT_TRUE(cursor.ownsHandle());
    EXPECT_NE(cursor.handle(), nullptr);

    ::DeleteFileA(path.c_str());
}

TEST(CursorClass, PathConstructorFailureLeavesADefaultCursor) {
    newui::Cursor cursor("NoSuchCursorFile.png");

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(cursor.isNull());
}

TEST(CursorClass, SetImageBuildsAndOwnsTheHandle) {
    BLImage image = MakeTestImage(16, 16);

    newui::Cursor cursor;
    EXPECT_TRUE(cursor.setImage(image));

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Custom);
    EXPECT_TRUE(cursor.path().empty());  // no file path for an in-memory image
    EXPECT_FALSE(cursor.isNull());
    EXPECT_TRUE(cursor.ownsHandle());
    EXPECT_NE(cursor.handle(), nullptr);
}

TEST(CursorClass, SetImageFailureLeavesTheCursorEntirelyUnchanged) {
    BLImage tooBig = MakeTestImage(40, 40);
    newui::Cursor cursor(newui::CursorKind::Wait);

    EXPECT_FALSE(cursor.setImage(tooBig));

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Wait);
    EXPECT_EQ(cursor.handle(), ::LoadCursorW(nullptr, IDC_WAIT));
}

TEST(CursorClass, SetCursorKindClearsAPreviouslySetPath) {
    const std::string path = "cursor_class_test_clear.png";
    WriteTestPNG(path, 16, 16);

    newui::Cursor cursor;
    ASSERT_TRUE(cursor.setPath(path));

    cursor.setCursorKind(newui::CursorKind::Wait);

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Wait);
    EXPECT_TRUE(cursor.path().empty());
    EXPECT_TRUE(cursor.isNull());
    EXPECT_FALSE(cursor.ownsHandle());
    EXPECT_EQ(cursor.handle(), ::LoadCursorW(nullptr, IDC_WAIT));

    ::DeleteFileA(path.c_str());
}

TEST(CursorClass, SetPathFailureLeavesTheCursorEntirelyUnchanged) {
    newui::Cursor cursor(newui::CursorKind::Wait);

    EXPECT_FALSE(cursor.setPath("NoSuchCursorFile.png"));

    EXPECT_EQ(cursor.kind(), newui::CursorKind::Wait);
    EXPECT_TRUE(cursor.path().empty());
}

TEST(CursorClass, MoveConstructorTransfersStateAndLeavesSourceAsDefault) {
    const std::string path = "cursor_class_test_move.png";
    WriteTestPNG(path, 16, 16);

    newui::Cursor source;
    ASSERT_TRUE(source.setPath(path));
    HCURSOR handle = source.handle();

    newui::Cursor moved(std::move(source));

    EXPECT_EQ(moved.kind(), newui::CursorKind::Custom);
    EXPECT_EQ(moved.path(), path);
    EXPECT_EQ(moved.handle(), handle);

    EXPECT_EQ(source.kind(), newui::CursorKind::Arrow);
    EXPECT_TRUE(source.path().empty());
    EXPECT_TRUE(source.isNull());

    ::DeleteFileA(path.c_str());
}

TEST(CursorClass, MoveAssignmentReleasesTheTargetsOwnedHandleFirst) {
    const std::string path1 = "cursor_class_test_move_assign1.png";
    const std::string path2 = "cursor_class_test_move_assign2.png";
    WriteTestPNG(path1, 16, 16);
    WriteTestPNG(path2, 16, 16);

    newui::Cursor a;
    ASSERT_TRUE(a.setPath(path1));  // a owns an HCURSOR here

    newui::Cursor b;
    ASSERT_TRUE(b.setPath(path2));
    HCURSOR bHandle = b.handle();

    // a's originally-owned handle should be destroyed here, not leaked -
    // no directly observable side effect beyond "doesn't crash", same
    // caveat as View's own replace-an-owned-cursor tests in test_view.cpp.
    a = std::move(b);

    EXPECT_EQ(a.path(), path2);
    EXPECT_EQ(a.handle(), bHandle);

    ::DeleteFileA(path1.c_str());
    ::DeleteFileA(path2.c_str());
}

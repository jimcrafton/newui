#include "newui/viewstyle.h"

#include <gtest/gtest.h>

namespace {

// Real BLContext shared by all cases below - ViewStyle::paint() needs one
// to draw into, even though these tests only care about the clientBounds
// side effect, not the pixels.
BLContext& SharedContext() {
    static BLImage image(64, 64, BL_FORMAT_PRGB32);
    static BLContext ctx(image);
    return ctx;
}

// Test-local subclasses exposing ThemedButtonStyle/ThemedCheckBoxStyle's
// protected partId()/stateId() for direct assertions on their state-
// precedence logic - no live HWND/HTHEME needed for that, unlike paint()
// itself.
class TestableThemedButtonStyle : public newui::ThemedButtonStyle {
public:
    using newui::ThemedButtonStyle::partId;
    using newui::ThemedButtonStyle::stateId;
};

class TestableThemedCheckBoxStyle : public newui::ThemedCheckBoxStyle {
public:
    using newui::ThemedCheckBoxStyle::partId;
    using newui::ThemedCheckBoxStyle::stateId;
};

}  // namespace

TEST(ViewStyle, NoBorderLeavesClientBoundsAtFullSize) {
    newui::ViewStyle style;
    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.left(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.top(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ViewStyle, BorderDeflatesClientBoundsByBorderWidth) {
    newui::ViewStyle style;
    style.backgroundFill = BLRgba32(255, 0, 0);
    style.borderFill = BLRgba32(0, 0, 255);
    style.borderWidth = 2.0f;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.left(), 2.0f);
    EXPECT_FLOAT_EQ(clientBounds.top(), 2.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 60.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 60.0f);
}

TEST(ButtonStyle, RaisedEdgeDeflatesClientBoundsByEdgeWidth) {
    newui::ButtonStyle btn;
    btn.edgeStyle = newui::Edge3DStyle::Raised;
    btn.edgeWidth = 2.0f;
    btn.edgeHighlightColor = BLRgba32(255, 255, 255);
    btn.edgeShadowColor = BLRgba32(64, 64, 64);

    newui::Rect clientBounds;
    btn.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.left(), 2.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 60.0f);
}

// Etched/Bump are two nested bevels of edgeWidth each, so they occupy
// 2*edgeWidth inward from the outer edge - unlike Raised/Sunken's one.
TEST(ButtonStyle, EtchedEdgeDeflatesClientBoundsByTwiceEdgeWidth) {
    newui::ButtonStyle btn;
    btn.edgeStyle = newui::Edge3DStyle::Etched;
    btn.edgeWidth = 2.0f;
    btn.edgeHighlightColor = BLRgba32(255, 255, 255);
    btn.edgeShadowColor = BLRgba32(64, 64, 64);

    newui::Rect clientBounds;
    btn.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.left(), 4.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 56.0f);
}

TEST(ButtonStyle, BorderAndEdgeDeflationsAreAdditive) {
    newui::ButtonStyle btn;
    btn.borderFill = BLRgba32(0, 0, 0);
    btn.borderWidth = 1.0f;
    btn.edgeStyle = newui::Edge3DStyle::Raised;
    btn.edgeWidth = 2.0f;
    btn.edgeHighlightColor = BLRgba32(255, 255, 255);
    btn.edgeShadowColor = BLRgba32(64, 64, 64);

    newui::Rect clientBounds;
    btn.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.left(), 3.0f);  // 1 (border) + 2 (edge)
}

TEST(ButtonStyle, ZeroEdgeWidthLeavesBaseClientBoundsUnchanged) {
    newui::ButtonStyle btn;
    btn.edgeWidth = 0.0f;

    newui::Rect clientBounds;
    btn.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.left(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
}

TEST(CheckBoxStyle, BoxAndSpacingDeflateOnlyTheLeftSide) {
    newui::CheckBoxStyle cb;
    cb.boxSize = 13.0f;
    cb.boxLabelSpacing = 4.0f;
    cb.boxFill = BLRgba32(255, 255, 255);
    cb.checkColor = BLRgba32(0, 0, 0);

    newui::Rect clientBounds;
    cb.paint(SharedContext(), newui::Size(100, 20), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.left(), 17.0f);   // boxSize + boxLabelSpacing
    EXPECT_FLOAT_EQ(clientBounds.top(), 0.0f);     // no border, top untouched
    EXPECT_FLOAT_EQ(clientBounds.size().width, 83.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 20.0f);
}

TEST(CheckBoxStyle, CheckedStatePaintsWithoutAlteringClientBounds) {
    newui::CheckBoxStyle cb;
    cb.boxSize = 13.0f;
    cb.checkColor = BLRgba32(0, 0, 0);

    newui::Rect uncheckedBounds;
    cb.checked = false;
    cb.paint(SharedContext(), newui::Size(100, 20), false, uncheckedBounds);

    newui::Rect checkedBounds;
    cb.checked = true;
    cb.paint(SharedContext(), newui::Size(100, 20), false, checkedBounds);

    EXPECT_FLOAT_EQ(uncheckedBounds.left(), checkedBounds.left());
    EXPECT_FLOAT_EQ(uncheckedBounds.size().width, checkedBounds.size().width);
}

// ---------------------------------------------------------------------------
// computeClientBounds() - the paint-free equivalent of paint()'s
// clientBounds out-parameter (see ViewStyle::computeClientBounds()'s
// comment). No BLContext/SharedContext() needed for any of these.
// ---------------------------------------------------------------------------

TEST(ViewStyle, ComputeClientBoundsMatchesNoBorderCase) {
    newui::ViewStyle style;
    newui::Rect clientBounds = style.computeClientBounds(newui::Size(64, 64));

    EXPECT_FLOAT_EQ(clientBounds.left(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
}

TEST(ViewStyle, ComputeClientBoundsDeflatesByBorderWidth) {
    newui::ViewStyle style;
    style.borderWidth = 2.0f;

    newui::Rect clientBounds = style.computeClientBounds(newui::Size(64, 64));

    EXPECT_FLOAT_EQ(clientBounds.left(), 2.0f);
    EXPECT_FLOAT_EQ(clientBounds.top(), 2.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 60.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 60.0f);
}

TEST(ViewStyle, ComputeClientBoundsAgreesWithPaintsOutParameter) {
    newui::ViewStyle style;
    style.borderFill = BLRgba32(0, 0, 255);
    style.borderWidth = 3.0f;

    newui::Rect computed = style.computeClientBounds(newui::Size(64, 64));

    newui::Rect painted;
    style.paint(SharedContext(), newui::Size(64, 64), false, painted);

    EXPECT_EQ(computed, painted);
}

TEST(ButtonStyle, ComputeClientBoundsDeflatesByEdgeWidth) {
    newui::ButtonStyle btn;
    btn.edgeStyle = newui::Edge3DStyle::Raised;
    btn.edgeWidth = 2.0f;
    btn.edgeHighlightColor = BLRgba32(255, 255, 255);
    btn.edgeShadowColor = BLRgba32(64, 64, 64);

    newui::Rect clientBounds = btn.computeClientBounds(newui::Size(64, 64));

    EXPECT_FLOAT_EQ(clientBounds.left(), 2.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 60.0f);
}

TEST(ButtonStyle, ComputeClientBoundsAgreesWithPaintsOutParameter) {
    newui::ButtonStyle btn;
    btn.edgeStyle = newui::Edge3DStyle::Etched;
    btn.edgeWidth = 2.0f;
    btn.edgeHighlightColor = BLRgba32(255, 255, 255);
    btn.edgeShadowColor = BLRgba32(64, 64, 64);

    newui::Rect computed = btn.computeClientBounds(newui::Size(64, 64));

    newui::Rect painted;
    btn.paint(SharedContext(), newui::Size(64, 64), false, painted);

    EXPECT_EQ(computed, painted);
}

TEST(CheckBoxStyle, ComputeClientBoundsDeflatesOnlyTheLeftSide) {
    newui::CheckBoxStyle cb;
    cb.boxSize = 13.0f;
    cb.boxLabelSpacing = 4.0f;

    newui::Rect clientBounds = cb.computeClientBounds(newui::Size(100, 20));

    EXPECT_FLOAT_EQ(clientBounds.left(), 17.0f);  // boxSize + boxLabelSpacing
    EXPECT_FLOAT_EQ(clientBounds.top(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 83.0f);
}

TEST(CheckBoxStyle, ComputeClientBoundsAgreesWithPaintsOutParameter) {
    newui::CheckBoxStyle cb;
    cb.boxSize = 13.0f;
    cb.boxLabelSpacing = 4.0f;
    cb.boxFill = BLRgba32(255, 255, 255);
    cb.checkColor = BLRgba32(0, 0, 0);

    newui::Rect computed = cb.computeClientBounds(newui::Size(100, 20));

    newui::Rect painted;
    cb.paint(SharedContext(), newui::Size(100, 20), false, painted);

    EXPECT_EQ(computed, painted);
}

// ---------------------------------------------------------------------------
// LabelStyle
// ---------------------------------------------------------------------------

TEST(LabelStyle, EmptyTextDoesNotCrashAndLeavesClientBoundsAtFullSize) {
    newui::LabelStyle style;
    style.textColor = BLRgba32(0, 0, 0);

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(LabelStyle, NullTextColorDoesNotCrash) {
    newui::LabelStyle style;
    style.text = "Hello";

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
}

TEST(LabelStyle, UnresolvedFontDoesNotCrash) {
    newui::LabelStyle style;
    style.text = "Hello";
    style.textColor = BLRgba32(0, 0, 0);
    // style.font is default-constructed (empty name), so blFont() -> nullptr.

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
}

// Renders LabelStyle's text into its own image (rather than SharedContext(),
// since it needs to read pixels back afterward) and checks that the
// centroid of the drawn ("ink") pixels lands near the image's geometric
// center - a font/rendering-agnostic way to confirm the text is centered
// rather than, say, stuck in a corner, without hard-coding exact pixel
// positions that would be fragile across installed fonts.
TEST(LabelStyle, TextIsCenteredWithinClientBounds) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u) << "need at least one system font to test with";

    const int width = 200;
    const int height = 80;

    BLImage image(width, height, BL_FORMAT_PRGB32);
    BLContext ctx(image);

    newui::LabelStyle style;
    style.text = "Test";
    style.textColor = BLRgba32(0, 0, 0, 255);
    style.font.setName(fonts[0].name);
    style.font.setSize(24.0f);

    newui::Rect clientBounds;
    style.paint(ctx, newui::Size(float(width), float(height)), false, clientBounds);
    ctx.end();

    BLImageData data;
    image.get_data(&data);

    double sumX = 0.0, sumY = 0.0;
    long inkCount = 0;
    const uint8_t* base = static_cast<const uint8_t*>(data.pixel_data);
    for (int y = 0; y < height; ++y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(base + y * data.stride);
        for (int x = 0; x < width; ++x) {
            uint8_t alpha = uint8_t(row[x] >> 24);
            if (alpha > 32) {
                sumX += x;
                sumY += y;
                ++inkCount;
            }
        }
    }

    ASSERT_GT(inkCount, 0) << "expected paint() to have drawn some ink pixels";

    double centroidX = sumX / double(inkCount);
    double centroidY = sumY / double(inkCount);

    EXPECT_NEAR(centroidX, width * 0.5, width * 0.25);
    EXPECT_NEAR(centroidY, height * 0.5, height * 0.35);
}

// ---------------------------------------------------------------------------
// ThemedButtonStyle / ThemedCheckBoxStyle - real theme rendering needs a
// live HWND (same constraint this project already accepts for Frame/
// RootView - see HANDOFF.md), so these only cover what's verifiable
// headlessly: paint()'s graceful no-op with no live window behind it yet,
// computeClientBounds()'s no-theme-cached fallback, and stateId()'s
// precedence logic. Serialization round-tripping is covered in
// test_serialization.cpp, matching how ButtonStyle/CheckBoxStyle's own
// round-trip is tested there rather than here.
// ---------------------------------------------------------------------------

TEST(ThemedButtonStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedButtonStyle style;
    // setView() never called - view() stays nullptr, so paint() has no
    // HWND to open a theme against.

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedButtonStyle, ComputeClientBoundsFallsBackToFullSizeWithNoCachedTheme) {
    newui::ThemedButtonStyle style;

    newui::Rect clientBounds = style.computeClientBounds(newui::Size(80, 24));

    EXPECT_FLOAT_EQ(clientBounds.left(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 80.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 24.0f);
}

TEST(ThemedButtonStyle, StateIdPrecedenceIsDisabledThenPressedThenHotThenNormal) {
    TestableThemedButtonStyle style;

    EXPECT_EQ(style.partId(), BP_PUSHBUTTON);
    EXPECT_EQ(style.stateId(false), PBS_NORMAL);
    EXPECT_EQ(style.stateId(true), PBS_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), PBS_PRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), PBS_DISABLED);  // disabled beats everything
}

TEST(ThemedCheckBoxStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedCheckBoxStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedCheckBoxStyle, StateIdPrecedenceAndCheckedDoubling) {
    TestableThemedCheckBoxStyle style;

    EXPECT_EQ(style.partId(), BP_CHECKBOX);
    EXPECT_EQ(style.stateId(false), CBS_UNCHECKEDNORMAL);
    EXPECT_EQ(style.stateId(true), CBS_UNCHECKEDHOT);

    style.checked = true;
    EXPECT_EQ(style.stateId(false), CBS_CHECKEDNORMAL);
    EXPECT_EQ(style.stateId(true), CBS_CHECKEDHOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), CBS_CHECKEDPRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), CBS_CHECKEDDISABLED);  // disabled beats everything
}

// ---------------------------------------------------------------------------
// Rect::deflated()
// ---------------------------------------------------------------------------

TEST(RectDeflated, UniformInsetOnAllSides) {
    newui::Rect r(0.0f, 0.0f, 20.0f, 10.0f);
    newui::Rect d = r.deflated(2.0f);

    EXPECT_FLOAT_EQ(d.left(), 2.0f);
    EXPECT_FLOAT_EQ(d.top(), 2.0f);
    EXPECT_FLOAT_EQ(d.size().width, 16.0f);
    EXPECT_FLOAT_EQ(d.size().height, 6.0f);
}

TEST(RectDeflated, PerSideInset) {
    newui::Rect r(0.0f, 0.0f, 20.0f, 20.0f);
    newui::Rect d = r.deflated(5.0f, 0.0f, 0.0f, 0.0f);

    EXPECT_FLOAT_EQ(d.left(), 5.0f);
    EXPECT_FLOAT_EQ(d.top(), 0.0f);
    EXPECT_FLOAT_EQ(d.size().width, 15.0f);
    EXPECT_FLOAT_EQ(d.size().height, 20.0f);
}

TEST(RectDeflated, OverlappingInsetsClampToZeroNotNegative) {
    newui::Rect r(0.0f, 0.0f, 10.0f, 10.0f);
    newui::Rect d = r.deflated(8.0f);

    EXPECT_GE(d.size().width, 0.0f);
    EXPECT_GE(d.size().height, 0.0f);
}

TEST(RectDeflated, NegativeAmountInflates) {
    newui::Rect r(0.0f, 0.0f, 10.0f, 10.0f);
    newui::Rect d = r.deflated(-2.0f);

    EXPECT_FLOAT_EQ(d.left(), -2.0f);
    EXPECT_FLOAT_EQ(d.size().width, 14.0f);
}

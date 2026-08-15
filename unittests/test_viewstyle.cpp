#include "newui/viewstyle.h"
#include "newui/subview.h"

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

class TestableThemedRadioButtonStyle : public newui::ThemedRadioButtonStyle {
public:
    using newui::ThemedRadioButtonStyle::partId;
    using newui::ThemedRadioButtonStyle::stateId;
};

class TestableThemedGroupBoxStyle : public newui::ThemedGroupBoxStyle {
public:
    using newui::ThemedGroupBoxStyle::partId;
    using newui::ThemedGroupBoxStyle::stateId;
};

class TestableThemedToolbarButtonStyle : public newui::ThemedToolbarButtonStyle {
public:
    using newui::ThemedToolbarButtonStyle::partId;
    using newui::ThemedToolbarButtonStyle::stateId;
};

class TestableThemedToolbarDropDownButtonStyle : public newui::ThemedToolbarDropDownButtonStyle {
public:
    using newui::ThemedToolbarDropDownButtonStyle::partId;
    using newui::ThemedToolbarDropDownButtonStyle::stateId;
};

class TestableThemedToolbarDropDownButtonGlyphStyle : public newui::ThemedToolbarDropDownButtonGlyphStyle {
public:
    using newui::ThemedToolbarDropDownButtonGlyphStyle::partId;
    using newui::ThemedToolbarDropDownButtonGlyphStyle::stateId;
};

class TestableThemedToolbarSplitButtonStyle : public newui::ThemedToolbarSplitButtonStyle {
public:
    using newui::ThemedToolbarSplitButtonStyle::partId;
    using newui::ThemedToolbarSplitButtonStyle::stateId;
};

class TestableThemedToolbarSplitButtonDropDownStyle : public newui::ThemedToolbarSplitButtonDropDownStyle {
public:
    using newui::ThemedToolbarSplitButtonDropDownStyle::partId;
    using newui::ThemedToolbarSplitButtonDropDownStyle::stateId;
};

class TestableThemedToolbarSeparatorStyle : public newui::ThemedToolbarSeparatorStyle {
public:
    using newui::ThemedToolbarSeparatorStyle::partId;
    using newui::ThemedToolbarSeparatorStyle::stateId;
};

class TestableThemedRebarChevronStyle : public newui::ThemedRebarChevronStyle {
public:
    using newui::ThemedRebarChevronStyle::partId;
    using newui::ThemedRebarChevronStyle::stateId;
};

class TestableThemedStatusPaneStyle : public newui::ThemedStatusPaneStyle {
public:
    using newui::ThemedStatusPaneStyle::partId;
    using newui::ThemedStatusPaneStyle::stateId;
};

class TestableThemedRebarBandStyle : public newui::ThemedRebarBandStyle {
public:
    using newui::ThemedRebarBandStyle::partId;
    using newui::ThemedRebarBandStyle::stateId;
};

class TestableThemedTooltipStyle : public newui::ThemedTooltipStyle {
public:
    using newui::ThemedTooltipStyle::partId;
    using newui::ThemedTooltipStyle::stateId;
};

class TestableThemedSpinButtonStyle : public newui::ThemedSpinButtonStyle {
public:
    using newui::ThemedSpinButtonStyle::partId;
    using newui::ThemedSpinButtonStyle::stateId;
};

class TestableThemedEditStyle : public newui::ThemedEditStyle {
public:
    using newui::ThemedEditStyle::partId;
    using newui::ThemedEditStyle::stateId;
};

class TestableThemedListItemStyle : public newui::ThemedListItemStyle {
public:
    using newui::ThemedListItemStyle::partId;
    using newui::ThemedListItemStyle::stateId;
};

class TestableThemedHeaderItemStyle : public newui::ThemedHeaderItemStyle {
public:
    using newui::ThemedHeaderItemStyle::partId;
    using newui::ThemedHeaderItemStyle::stateId;
};

class TestableThemedHeaderSortArrowStyle : public newui::ThemedHeaderSortArrowStyle {
public:
    using newui::ThemedHeaderSortArrowStyle::partId;
    using newui::ThemedHeaderSortArrowStyle::stateId;
};

class TestableThemedTreeItemStyle : public newui::ThemedTreeItemStyle {
public:
    using newui::ThemedTreeItemStyle::partId;
    using newui::ThemedTreeItemStyle::stateId;
};

class TestableThemedTreeGlyphStyle : public newui::ThemedTreeGlyphStyle {
public:
    using newui::ThemedTreeGlyphStyle::partId;
    using newui::ThemedTreeGlyphStyle::stateId;
};

class TestableThemedTabItemStyle : public newui::ThemedTabItemStyle {
public:
    using newui::ThemedTabItemStyle::partId;
    using newui::ThemedTabItemStyle::stateId;
};

class TestableThemedTabPaneStyle : public newui::ThemedTabPaneStyle {
public:
    using newui::ThemedTabPaneStyle::partId;
    using newui::ThemedTabPaneStyle::stateId;
};

class TestableThemedTrackbarTrackStyle : public newui::ThemedTrackbarTrackStyle {
public:
    using newui::ThemedTrackbarTrackStyle::partId;
    using newui::ThemedTrackbarTrackStyle::stateId;
};

class TestableThemedTrackbarThumbStyle : public newui::ThemedTrackbarThumbStyle {
public:
    using newui::ThemedTrackbarThumbStyle::partId;
    using newui::ThemedTrackbarThumbStyle::stateId;
};

class TestableThemedTrackbarTicksStyle : public newui::ThemedTrackbarTicksStyle {
public:
    using newui::ThemedTrackbarTicksStyle::partId;
    using newui::ThemedTrackbarTicksStyle::stateId;
};

class TestableThemedProgressBarTrackStyle : public newui::ThemedProgressBarTrackStyle {
public:
    using newui::ThemedProgressBarTrackStyle::partId;
    using newui::ThemedProgressBarTrackStyle::stateId;
};

class TestableThemedProgressBarFillStyle : public newui::ThemedProgressBarFillStyle {
public:
    using newui::ThemedProgressBarFillStyle::partId;
    using newui::ThemedProgressBarFillStyle::stateId;
};

class TestableThemedScrollbarThumbStyle : public newui::ThemedScrollbarThumbStyle {
public:
    using newui::ThemedScrollbarThumbStyle::partId;
    using newui::ThemedScrollbarThumbStyle::stateId;
};

class TestableThemedScrollbarArrowStyle : public newui::ThemedScrollbarArrowStyle {
public:
    using newui::ThemedScrollbarArrowStyle::partId;
    using newui::ThemedScrollbarArrowStyle::stateId;
};

class TestableThemedScrollbarTrackStyle : public newui::ThemedScrollbarTrackStyle {
public:
    using newui::ThemedScrollbarTrackStyle::partId;
    using newui::ThemedScrollbarTrackStyle::stateId;
};

class TestableThemedMenuBarItemStyle : public newui::ThemedMenuBarItemStyle {
public:
    using newui::ThemedMenuBarItemStyle::partId;
    using newui::ThemedMenuBarItemStyle::stateId;
};

class TestableThemedMenuBarBackgroundStyle : public newui::ThemedMenuBarBackgroundStyle {
public:
    using newui::ThemedMenuBarBackgroundStyle::partId;
    using newui::ThemedMenuBarBackgroundStyle::stateId;
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

// Regression test: view_->rootView() is null for any View not yet
// attached to a live window tree (e.g. a freshly-constructed
// TabControl/MenuBar in a headless test, or any composite still being
// assembled before addChild() into a real RootView-rooted tree) -
// markDirty() used to dereference it unconditionally once view_ itself
// was set (via setStyle()), crashing. Found via TabControl::selectTab()
// crashing in a headless test (see tabcontrol.cpp) - every pre-existing
// caller only ever ran from a live message pump where a RootView was
// already guaranteed to exist, so this went unnoticed until then.
TEST(ViewStyle, MarkDirtyDoesNotCrashWithNoLiveRootView) {
    auto* view = new newui::SubView();
    view->setStyle(std::make_unique<newui::ViewStyle>());
    // Deliberately never attached to any RootView - rootView() stays null.

    view->style().markDirty();

    delete view;
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

TEST(ViewStyle, SetBackgroundImageFromBLImageSetsAPatternFill) {
    newui::ViewStyle style;
    EXPECT_TRUE(style.backgroundFill.is_null());

    BLImage image(4, 4, BL_FORMAT_PRGB32);
    {
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(255, 0, 0, 255));
        ctx.fill_all();
        ctx.end();
    }

    style.setBackgroundImage(image);

    EXPECT_FALSE(style.backgroundFill.is_null());
    EXPECT_TRUE(style.backgroundFill.is_pattern());
}

TEST(ViewStyle, SetBackgroundImageSurvivesTheSourceImageGoingOutOfScope) {
    newui::ViewStyle style;

    {
        BLImage image(4, 4, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(0, 255, 0, 255));
        ctx.fill_all();
        ctx.end();
        style.setBackgroundImage(image);
    }  // image destroyed here - BLPattern's own refcounted copy should be unaffected

    // A real paint pass, not just checking is_pattern() - confirms the
    // pattern's own image data is still valid/drawable after the source
    // BLImage local went out of scope (the whole point of not taking a
    // newui::Image here - see setBackgroundImage()'s comment).
    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(8, 8), false, clientBounds);
    SUCCEED();
}

TEST(ViewStyle, SetBackgroundImageFromPathLoadsARealFile) {
    const std::string path = "viewstyle_test_background.png";
    {
        BLImage image(4, 4, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(0, 0, 255, 255));
        ctx.fill_all();
        ctx.end();
        ASSERT_EQ(image.write_to_file(path.c_str()), BL_SUCCESS);
    }

    newui::ViewStyle style;
    EXPECT_TRUE(style.setBackgroundImage(path));

    EXPECT_TRUE(style.backgroundFill.is_pattern());

    ::DeleteFileA(path.c_str());
}

TEST(ViewStyle, SetBackgroundImageFromPathFailsForAMissingFileAndLeavesFillUnchanged) {
    newui::ViewStyle style;
    style.backgroundFill = BLRgba32(1, 2, 3);

    EXPECT_FALSE(style.setBackgroundImage("NoSuchBackgroundImage.png"));

    ASSERT_TRUE(style.backgroundFill.is_rgba32());
    BLRgba32 rgba;
    style.backgroundFill.to_rgba32(&rgba);
    EXPECT_EQ(rgba.value, BLRgba32(1, 2, 3).value);
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
// ThemedRadioButtonStyle / ThemedGroupBoxStyle / ThemedToolbarButtonStyle /
// ThemedStatusPaneStyle / ThemedRebarBandStyle / ThemedTooltipStyle /
// ThemedSpinButtonStyle / ThemedEditStyle - same live-HWND constraint and
// scope as ThemedButtonStyle/ThemedCheckBoxStyle above: paint()'s graceful
// no-op, and stateId()/partId() precedence logic. Serialization round-
// tripping is covered in test_serialization.cpp.
// ---------------------------------------------------------------------------

TEST(ThemedRadioButtonStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedRadioButtonStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedRadioButtonStyle, StateIdPrecedenceAndCheckedDoubling) {
    TestableThemedRadioButtonStyle style;

    EXPECT_EQ(style.partId(), BP_RADIOBUTTON);
    EXPECT_EQ(style.stateId(false), RBS_UNCHECKEDNORMAL);
    EXPECT_EQ(style.stateId(true), RBS_UNCHECKEDHOT);

    style.checked = true;
    EXPECT_EQ(style.stateId(false), RBS_CHECKEDNORMAL);
    EXPECT_EQ(style.stateId(true), RBS_CHECKEDHOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), RBS_CHECKEDPRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), RBS_CHECKEDDISABLED);  // disabled beats everything
}

TEST(ThemedGroupBoxStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedGroupBoxStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedGroupBoxStyle, StateIdIsJustEnabledVsDisabled) {
    TestableThemedGroupBoxStyle style;

    EXPECT_EQ(style.partId(), BP_GROUPBOX);
    EXPECT_EQ(style.stateId(false), GBS_NORMAL);
    EXPECT_EQ(style.stateId(true), GBS_NORMAL);  // highlighted has no effect on a group box

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), GBS_DISABLED);
}

TEST(ThemedToolbarButtonStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedToolbarButtonStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedToolbarButtonStyle, StateIdPrecedenceAndCheckedDoubling) {
    TestableThemedToolbarButtonStyle style;

    EXPECT_EQ(style.partId(), TP_BUTTON);
    EXPECT_EQ(style.stateId(false), TS_NORMAL);
    EXPECT_EQ(style.stateId(true), TS_HOT);

    style.checked = true;
    EXPECT_EQ(style.stateId(false), TS_CHECKED);
    EXPECT_EQ(style.stateId(true), TS_HOTCHECKED);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), TS_PRESSED);  // pressed beats checked+hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TS_DISABLED);  // disabled beats everything
}

TEST(ThemedToolbarDropDownButtonStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedToolbarDropDownButtonStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedToolbarDropDownButtonStyle, StateIdPrecedenceAndCheckedDoubling) {
    TestableThemedToolbarDropDownButtonStyle style;

    EXPECT_EQ(style.partId(), TP_DROPDOWNBUTTON);
    EXPECT_EQ(style.stateId(false), TS_NORMAL);
    EXPECT_EQ(style.stateId(true), TS_HOT);

    style.checked = true;
    EXPECT_EQ(style.stateId(false), TS_CHECKED);
    EXPECT_EQ(style.stateId(true), TS_HOTCHECKED);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), TS_PRESSED);  // pressed beats checked+hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TS_DISABLED);  // disabled beats everything
}

TEST(ThemedToolbarDropDownButtonGlyphStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedToolbarDropDownButtonGlyphStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(16, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 16.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedToolbarDropDownButtonGlyphStyle, StateIdPrecedence) {
    TestableThemedToolbarDropDownButtonGlyphStyle style;

    EXPECT_EQ(style.partId(), TP_DROPDOWNBUTTONGLYPH);
    EXPECT_EQ(style.stateId(false), TS_NORMAL);
    EXPECT_EQ(style.stateId(true), TS_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), TS_PRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TS_DISABLED);  // disabled beats everything
}

TEST(ThemedToolbarSplitButtonStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedToolbarSplitButtonStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedToolbarSplitButtonStyle, StateIdPrecedenceAndCheckedDoubling) {
    TestableThemedToolbarSplitButtonStyle style;

    EXPECT_EQ(style.partId(), TP_SPLITBUTTON);
    EXPECT_EQ(style.stateId(false), TS_NORMAL);
    EXPECT_EQ(style.stateId(true), TS_HOT);

    style.checked = true;
    EXPECT_EQ(style.stateId(false), TS_CHECKED);
    EXPECT_EQ(style.stateId(true), TS_HOTCHECKED);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), TS_PRESSED);  // pressed beats checked+hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TS_DISABLED);  // disabled beats everything
}

TEST(ThemedToolbarSplitButtonDropDownStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedToolbarSplitButtonDropDownStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(16, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 16.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedToolbarSplitButtonDropDownStyle, StateIdPrecedenceAndCheckedDoubling) {
    TestableThemedToolbarSplitButtonDropDownStyle style;

    EXPECT_EQ(style.partId(), TP_SPLITBUTTONDROPDOWN);
    EXPECT_EQ(style.stateId(false), TS_NORMAL);
    EXPECT_EQ(style.stateId(true), TS_HOT);

    style.checked = true;
    EXPECT_EQ(style.stateId(false), TS_CHECKED);
    EXPECT_EQ(style.stateId(true), TS_HOTCHECKED);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), TS_PRESSED);  // pressed beats checked+hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TS_DISABLED);  // disabled beats everything
}

TEST(ThemedToolbarSeparatorStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedToolbarSeparatorStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(6, 24), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 6.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 24.0f);
}

TEST(ThemedToolbarSeparatorStyle, PartIdReflectsOrientationAndStateIsAlwaysNormal) {
    TestableThemedToolbarSeparatorStyle style;

    EXPECT_EQ(style.partId(), TP_SEPARATOR);
    EXPECT_EQ(style.stateId(false), TS_NORMAL);
    EXPECT_EQ(style.stateId(true), TS_NORMAL);  // ignores highlighted - see class comment

    style.horizontal = false;
    EXPECT_EQ(style.partId(), TP_SEPARATORVERT);
}

TEST(ThemedStatusPaneStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedStatusPaneStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedStatusPaneStyle, PartHasNoStateVariants) {
    TestableThemedStatusPaneStyle style;

    EXPECT_EQ(style.partId(), SP_PANE);
    EXPECT_EQ(style.stateId(false), 0);
    EXPECT_EQ(style.stateId(true), 0);
}

TEST(ThemedRebarBandStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedRebarBandStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedRebarBandStyle, PartHasNoStateVariants) {
    TestableThemedRebarBandStyle style;

    EXPECT_EQ(style.partId(), RP_BAND);
    EXPECT_EQ(style.stateId(false), 0);
    EXPECT_EQ(style.stateId(true), 0);
}

TEST(ThemedRebarChevronStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedRebarChevronStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(16, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 16.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedRebarChevronStyle, PartIdReflectsOrientation) {
    TestableThemedRebarChevronStyle style;

    EXPECT_EQ(style.partId(), RP_CHEVRON);

    style.horizontal = false;
    EXPECT_EQ(style.partId(), RP_CHEVRONVERT);
}

TEST(ThemedRebarChevronStyle, StateIdPrecedencePerOrientationHasNoDisabledVariant) {
    TestableThemedRebarChevronStyle style;

    EXPECT_EQ(style.stateId(false), CHEVS_NORMAL);
    EXPECT_EQ(style.stateId(true), CHEVS_HOT);
    style.pressed = true;
    EXPECT_EQ(style.stateId(true), CHEVS_PRESSED);  // pressed beats hot

    style.horizontal = false;
    style.pressed = false;
    EXPECT_EQ(style.stateId(false), CHEVSV_NORMAL);
    EXPECT_EQ(style.stateId(true), CHEVSV_HOT);
    style.pressed = true;
    EXPECT_EQ(style.stateId(true), CHEVSV_PRESSED);
}

TEST(ThemedTooltipStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTooltipStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(64, 64), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 64.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 64.0f);
}

TEST(ThemedTooltipStyle, StateIdReflectsLinked) {
    TestableThemedTooltipStyle style;

    EXPECT_EQ(style.partId(), TTP_STANDARD);
    EXPECT_EQ(style.stateId(false), TTSS_NORMAL);
    EXPECT_EQ(style.stateId(true), TTSS_NORMAL);  // highlighted has no effect on a tooltip

    style.linked = true;
    EXPECT_EQ(style.stateId(false), TTSS_LINK);
}

TEST(ThemedSpinButtonStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedSpinButtonStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(16, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 16.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedSpinButtonStyle, UpButtonPartIdAndStatePrecedence) {
    TestableThemedSpinButtonStyle style;
    style.isUpButton = true;

    EXPECT_EQ(style.partId(), SPNP_UP);
    EXPECT_EQ(style.stateId(false), UPS_NORMAL);
    EXPECT_EQ(style.stateId(true), UPS_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), UPS_PRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), UPS_DISABLED);  // disabled beats everything
}

TEST(ThemedSpinButtonStyle, DownButtonUsesTheSeparateDownPartAndStates) {
    TestableThemedSpinButtonStyle style;
    style.isUpButton = false;

    EXPECT_EQ(style.partId(), SPNP_DOWN);
    EXPECT_EQ(style.stateId(false), DNS_NORMAL);
    EXPECT_EQ(style.stateId(true), DNS_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), DNS_PRESSED);

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), DNS_DISABLED);
}

TEST(ThemedEditStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedEditStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(120, 24), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 120.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 24.0f);
}

TEST(ThemedEditStyle, StateIdPrecedence) {
    TestableThemedEditStyle style;

    EXPECT_EQ(style.partId(), EP_EDITTEXT);
    EXPECT_EQ(style.stateId(false), ETS_NORMAL);
    EXPECT_EQ(style.stateId(true), ETS_HOT);

    style.focused = true;
    EXPECT_EQ(style.stateId(true), ETS_FOCUSED);  // focused beats hot

    style.readOnly = true;
    EXPECT_EQ(style.stateId(true), ETS_READONLY);  // readOnly beats focused

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), ETS_DISABLED);  // disabled beats everything
}

// ---------------------------------------------------------------------------
// Batch 2: ThemedListItemStyle / ThemedHeaderItemStyle /
// ThemedHeaderSortArrowStyle / ThemedTreeItemStyle / ThemedTreeGlyphStyle /
// ThemedTabItemStyle / ThemedTabPaneStyle / ThemedTrackbarTrackStyle /
// ThemedTrackbarThumbStyle / ThemedTrackbarTicksStyle /
// ThemedProgressBarTrackStyle / ThemedProgressBarFillStyle /
// ThemedScrollbarThumbStyle / ThemedScrollbarArrowStyle /
// ThemedScrollbarTrackStyle - same live-HWND constraint/scope as batch 1
// above.
// ---------------------------------------------------------------------------

TEST(ThemedListItemStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedListItemStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(120, 20), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 120.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 20.0f);
}

TEST(ThemedListItemStyle, StateIdPrecedenceAndSelectedDoubling) {
    TestableThemedListItemStyle style;

    EXPECT_EQ(style.partId(), LVP_LISTITEM);
    EXPECT_EQ(style.stateId(false), LISS_NORMAL);
    EXPECT_EQ(style.stateId(true), LISS_HOT);

    style.selected = true;
    EXPECT_EQ(style.stateId(false), LISS_SELECTED);
    EXPECT_EQ(style.stateId(true), LISS_HOTSELECTED);

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), LISS_DISABLED);  // disabled beats everything
}

TEST(ThemedHeaderItemStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedHeaderItemStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(80, 20), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 80.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 20.0f);
}

TEST(ThemedHeaderItemStyle, StateIdPrecedenceAndSortedDoubling) {
    TestableThemedHeaderItemStyle style;

    EXPECT_EQ(style.partId(), HP_HEADERITEM);
    EXPECT_EQ(style.stateId(false), HIS_NORMAL);
    EXPECT_EQ(style.stateId(true), HIS_HOT);

    style.sorted = true;
    EXPECT_EQ(style.stateId(false), HIS_SORTEDNORMAL);
    EXPECT_EQ(style.stateId(true), HIS_SORTEDHOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), HIS_SORTEDPRESSED);  // pressed beats hot
}

TEST(ThemedHeaderSortArrowStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedHeaderSortArrowStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(12, 12), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 12.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 12.0f);
}

TEST(ThemedHeaderSortArrowStyle, StateIdReflectsDirectionOnly) {
    TestableThemedHeaderSortArrowStyle style;

    EXPECT_EQ(style.partId(), HP_HEADERSORTARROW);
    EXPECT_EQ(style.stateId(false), HSAS_SORTEDUP);
    EXPECT_EQ(style.stateId(true), HSAS_SORTEDUP);  // highlighted has no effect

    style.sortedAscending = false;
    EXPECT_EQ(style.stateId(false), HSAS_SORTEDDOWN);
}

TEST(ThemedTreeItemStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTreeItemStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(120, 18), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 120.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 18.0f);
}

TEST(ThemedTreeItemStyle, StateIdPrecedenceAndSelectedDoubling) {
    TestableThemedTreeItemStyle style;

    EXPECT_EQ(style.partId(), TVP_TREEITEM);
    EXPECT_EQ(style.stateId(false), TREIS_NORMAL);
    EXPECT_EQ(style.stateId(true), TREIS_HOT);

    style.selected = true;
    EXPECT_EQ(style.stateId(false), TREIS_SELECTED);
    EXPECT_EQ(style.stateId(true), TREIS_HOTSELECTED);

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TREIS_DISABLED);  // disabled beats everything
}

TEST(ThemedTreeGlyphStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTreeGlyphStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(16, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 16.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedTreeGlyphStyle, StateIdReflectsExpandedOnly) {
    TestableThemedTreeGlyphStyle style;

    EXPECT_EQ(style.partId(), TVP_GLYPH);
    EXPECT_EQ(style.stateId(false), GLPS_CLOSED);
    EXPECT_EQ(style.stateId(true), GLPS_CLOSED);  // highlighted has no effect (see class comment)

    style.expanded = true;
    EXPECT_EQ(style.stateId(false), GLPS_OPENED);
}

TEST(ThemedTabItemStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTabItemStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(80, 24), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 80.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 24.0f);
}

TEST(ThemedTabItemStyle, PartIdReflectsPositionForTopAlignmentByDefault) {
    TestableThemedTabItemStyle style;

    // alignment defaults to Top - the TOPTABITEM* group, not plain
    // TABITEM* (that group is Bottom/Left/Right's, see PartIdReflectsAlignment).
    EXPECT_EQ(style.partId(), TABP_TOPTABITEM);

    style.position = newui::ThemedTabItemStyle::Position::Left;
    EXPECT_EQ(style.partId(), TABP_TOPTABITEMLEFTEDGE);

    style.position = newui::ThemedTabItemStyle::Position::Right;
    EXPECT_EQ(style.partId(), TABP_TOPTABITEMRIGHTEDGE);

    style.position = newui::ThemedTabItemStyle::Position::Only;
    EXPECT_EQ(style.partId(), TABP_TOPTABITEMBOTHEDGE);
}

TEST(ThemedTabItemStyle, PartIdReflectsAlignment) {
    TestableThemedTabItemStyle style;

    // Bottom/Left/Right all map to the same plain TABITEM* group -
    // uxtheme itself has no further per-edge distinction (verified
    // against vsstyle.h - see the class comment).
    for (auto alignment : { newui::ThemedTabItemStyle::TabAlignment::Bottom,
                             newui::ThemedTabItemStyle::TabAlignment::Left,
                             newui::ThemedTabItemStyle::TabAlignment::Right }) {
        style.alignment = alignment;
        EXPECT_EQ(style.partId(), TABP_TABITEM);

        style.position = newui::ThemedTabItemStyle::Position::Left;
        EXPECT_EQ(style.partId(), TABP_TABITEMLEFTEDGE);

        style.position = newui::ThemedTabItemStyle::Position::Right;
        EXPECT_EQ(style.partId(), TABP_TABITEMRIGHTEDGE);

        style.position = newui::ThemedTabItemStyle::Position::Only;
        EXPECT_EQ(style.partId(), TABP_TABITEMBOTHEDGE);

        style.position = newui::ThemedTabItemStyle::Position::Middle;
    }

    style.alignment = newui::ThemedTabItemStyle::TabAlignment::Top;
    EXPECT_EQ(style.partId(), TABP_TOPTABITEM);
}

TEST(ThemedTabItemStyle, StateIdPrecedence) {
    TestableThemedTabItemStyle style;

    EXPECT_EQ(style.stateId(false), TIS_NORMAL);
    EXPECT_EQ(style.stateId(true), TIS_HOT);

    style.selected = true;
    EXPECT_EQ(style.stateId(true), TIS_SELECTED);  // selected beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TIS_DISABLED);  // disabled beats everything
}

TEST(ThemedTabPaneStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTabPaneStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(200, 150), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 200.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 150.0f);
}

TEST(ThemedTabPaneStyle, PartHasNoStateVariants) {
    TestableThemedTabPaneStyle style;

    EXPECT_EQ(style.partId(), TABP_PANE);
    EXPECT_EQ(style.stateId(false), 0);
    EXPECT_EQ(style.stateId(true), 0);
}

TEST(ThemedTrackbarTrackStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTrackbarTrackStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(120, 6), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 120.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 6.0f);
}

TEST(ThemedTrackbarTrackStyle, PartIdAndStateIdReflectOrientation) {
    TestableThemedTrackbarTrackStyle style;

    EXPECT_EQ(style.partId(), TKP_TRACK);
    EXPECT_EQ(style.stateId(false), TRS_NORMAL);
    EXPECT_EQ(style.stateId(true), TRS_NORMAL);  // highlighted has no effect

    style.horizontal = false;
    EXPECT_EQ(style.partId(), TKP_TRACKVERT);
    EXPECT_EQ(style.stateId(false), TRVS_NORMAL);
}

TEST(ThemedTrackbarThumbStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTrackbarThumbStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(12, 20), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 12.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 20.0f);
}

TEST(ThemedTrackbarThumbStyle, PartIdReflectsOrientationAndStateIdPrecedence) {
    TestableThemedTrackbarThumbStyle style;

    EXPECT_EQ(style.partId(), TKP_THUMB);
    EXPECT_EQ(style.stateId(false), TUS_NORMAL);
    EXPECT_EQ(style.stateId(true), TUS_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), TUS_PRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), TUS_DISABLED);  // disabled beats everything

    style.horizontal = false;
    EXPECT_EQ(style.partId(), TKP_THUMBVERT);
}

TEST(ThemedTrackbarTicksStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedTrackbarTicksStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(120, 6), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 120.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 6.0f);
}

TEST(ThemedTrackbarTicksStyle, PartIdAndStateIdReflectOrientation) {
    TestableThemedTrackbarTicksStyle style;

    EXPECT_EQ(style.partId(), TKP_TICS);
    EXPECT_EQ(style.stateId(false), TSS_NORMAL);
    EXPECT_EQ(style.stateId(true), TSS_NORMAL);  // highlighted has no effect

    style.horizontal = false;
    EXPECT_EQ(style.partId(), TKP_TICSVERT);
    EXPECT_EQ(style.stateId(false), TSVS_NORMAL);
}

TEST(ThemedProgressBarTrackStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedProgressBarTrackStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(160, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 160.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedProgressBarTrackStyle, PartIdAndStateIdReflectOrientation) {
    TestableThemedProgressBarTrackStyle style;

    EXPECT_EQ(style.partId(), PP_BAR);
    EXPECT_EQ(style.stateId(false), 0);
    EXPECT_EQ(style.stateId(true), 0);  // highlighted has no effect

    style.horizontal = false;
    EXPECT_EQ(style.partId(), PP_BARVERT);
    EXPECT_EQ(style.stateId(false), 0);
}

TEST(ThemedProgressBarFillStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedProgressBarFillStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(80, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 80.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedProgressBarFillStyle, PartIdAndStateIdReflectOrientationAndState) {
    TestableThemedProgressBarFillStyle style;

    EXPECT_EQ(style.partId(), PP_FILL);
    EXPECT_EQ(style.stateId(false), PBFS_NORMAL);
    EXPECT_EQ(style.stateId(true), PBFS_NORMAL);  // highlighted has no effect

    style.state = newui::ThemedProgressBarFillStyle::FillState::Error;
    EXPECT_EQ(style.stateId(false), PBFS_ERROR);

    style.state = newui::ThemedProgressBarFillStyle::FillState::Paused;
    EXPECT_EQ(style.stateId(false), PBFS_PAUSED);

    style.horizontal = false;
    EXPECT_EQ(style.partId(), PP_FILLVERT);
    EXPECT_EQ(style.stateId(false), PBFVS_PAUSED);
}

TEST(ThemedScrollbarThumbStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedScrollbarThumbStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(20, 40), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 20.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 40.0f);
}

TEST(ThemedScrollbarThumbStyle, PartIdReflectsOrientationAndStateIdPrecedence) {
    TestableThemedScrollbarThumbStyle style;

    EXPECT_EQ(style.partId(), SBP_THUMBBTNHORZ);
    EXPECT_EQ(style.stateId(false), SCRBS_NORMAL);
    EXPECT_EQ(style.stateId(true), SCRBS_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), SCRBS_PRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), SCRBS_DISABLED);  // disabled beats everything

    style.horizontal = false;
    EXPECT_EQ(style.partId(), SBP_THUMBBTNVERT);
}

TEST(ThemedScrollbarArrowStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedScrollbarArrowStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(16, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 16.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedScrollbarArrowStyle, PartIdIsAlwaysArrowBtn) {
    TestableThemedScrollbarArrowStyle style;
    EXPECT_EQ(style.partId(), SBP_ARROWBTN);
}

TEST(ThemedScrollbarArrowStyle, StateIdPerDirectionPrecedence) {
    TestableThemedScrollbarArrowStyle style;

    style.direction = newui::ThemedScrollbarArrowStyle::Direction::Up;
    EXPECT_EQ(style.stateId(false), ABS_UPNORMAL);
    EXPECT_EQ(style.stateId(true), ABS_UPHOT);
    style.pressed = true;
    EXPECT_EQ(style.stateId(true), ABS_UPPRESSED);
    style.enabled = false;
    EXPECT_EQ(style.stateId(true), ABS_UPDISABLED);  // disabled beats pressed+hot

    style.pressed = false;
    style.enabled = true;

    style.direction = newui::ThemedScrollbarArrowStyle::Direction::Down;
    EXPECT_EQ(style.stateId(false), ABS_DOWNNORMAL);
    EXPECT_EQ(style.stateId(true), ABS_DOWNHOT);

    style.direction = newui::ThemedScrollbarArrowStyle::Direction::Left;
    EXPECT_EQ(style.stateId(false), ABS_LEFTNORMAL);
    EXPECT_EQ(style.stateId(true), ABS_LEFTHOT);

    style.direction = newui::ThemedScrollbarArrowStyle::Direction::Right;
    EXPECT_EQ(style.stateId(false), ABS_RIGHTNORMAL);
    EXPECT_EQ(style.stateId(true), ABS_RIGHTHOT);
}

TEST(ThemedScrollbarTrackStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedScrollbarTrackStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(120, 16), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 120.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 16.0f);
}

TEST(ThemedScrollbarTrackStyle, PartIdReflectsOrientationAndPosition) {
    TestableThemedScrollbarTrackStyle style;

    EXPECT_EQ(style.partId(), SBP_LOWERTRACKHORZ);

    style.position = newui::ThemedScrollbarTrackStyle::Position::Upper;
    EXPECT_EQ(style.partId(), SBP_UPPERTRACKHORZ);

    style.horizontal = false;
    style.position = newui::ThemedScrollbarTrackStyle::Position::Lower;
    EXPECT_EQ(style.partId(), SBP_LOWERTRACKVERT);

    style.position = newui::ThemedScrollbarTrackStyle::Position::Upper;
    EXPECT_EQ(style.partId(), SBP_UPPERTRACKVERT);
}

TEST(ThemedScrollbarTrackStyle, StateIdPrecedence) {
    TestableThemedScrollbarTrackStyle style;

    EXPECT_EQ(style.stateId(false), SCRBS_NORMAL);
    EXPECT_EQ(style.stateId(true), SCRBS_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), SCRBS_PRESSED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), SCRBS_DISABLED);  // disabled beats everything
}

// ---------------------------------------------------------------------------
// ThemedMenuBarItemStyle / ThemedMenuBarBackgroundStyle - used by MenuBar's
// own SubView-tree menu bar (menus.h), not a native HMENU bar - same
// live-HWND-needed-for-real-paint() constraint as every other Themed*Style
// in this file.
// ---------------------------------------------------------------------------

TEST(ThemedMenuBarItemStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedMenuBarItemStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(60, 32), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 60.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 32.0f);
}

// paint() hand-draws the hot/pressed highlight itself (see its own doc
// comment in viewstyle.cpp) rather than calling DrawThemeBackground, so -
// unlike every other ThemedViewStyle subclass - this is fully verifiable
// headlessly, no live HWND/HTHEME needed at all: paints into its own image
// (same "read pixels back afterward" pattern LabelStyle's
// TextIsCenteredWithinClientBounds test already uses) and checks the
// corner is untouched (inset + rounded corner clip away MENU_BARITEM's own
// square edge) while the center is filled.
TEST(ThemedMenuBarItemStyle, PaintDrawsInsetRoundedHighlightWhenHot) {
    const int size = 40;
    BLImage image(size, size, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    // BLImage's own buffer starts uninitialized, not transparent - clear
    // it explicitly first (same pattern themes1.cpp's WriteDotCursorPNG()
    // already uses) so an untouched pixel reads as alpha 0, not leftover
    // garbage.
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0, 0, 0, 0));
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

    newui::ThemedMenuBarItemStyle style;
    newui::Rect clientBounds;
    style.paint(ctx, newui::Size(float(size), float(size)), /*highlighted=*/true, clientBounds);
    ctx.end();

    BLImageData data;
    image.get_data(&data);
    const uint8_t* base = static_cast<const uint8_t*>(data.pixel_data);
    auto alphaAt = [&](int x, int y) {
        const uint32_t* row = reinterpret_cast<const uint32_t*>(base + y * data.stride);
        return uint8_t(row[x] >> 24);
    };

    EXPECT_EQ(alphaAt(0, 0), 0) << "top-left corner should be clipped away by the inset + rounded corner";
    EXPECT_GT(alphaAt(size / 2, size / 2), 0) << "center should be filled by the hot highlight";
}

// MBI_NORMAL (not highlighted, not pressed) draws nothing at all - same
// native look DrawThemeBackground's own MBI_NORMAL already had (see
// invertLightnessInPlace()'s doc comment in viewstyle.cpp).
TEST(ThemedMenuBarItemStyle, PaintDrawsNothingWhenNotHighlighted) {
    const int size = 40;
    BLImage image(size, size, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);
    ctx.fill_all(BLRgba32(0, 0, 0, 0));
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);

    newui::ThemedMenuBarItemStyle style;
    newui::Rect clientBounds;
    style.paint(ctx, newui::Size(float(size), float(size)), /*highlighted=*/false, clientBounds);
    ctx.end();

    BLImageData data;
    image.get_data(&data);
    const uint8_t* base = static_cast<const uint8_t*>(data.pixel_data);
    const uint32_t* centerRow = reinterpret_cast<const uint32_t*>(base + (size / 2) * data.stride);
    EXPECT_EQ(uint8_t(centerRow[size / 2] >> 24), 0);
}

TEST(ThemedMenuBarItemStyle, StateIdPrecedenceIsDisabledThenPressedThenHotThenNormal) {
    TestableThemedMenuBarItemStyle style;

    EXPECT_EQ(style.partId(), MENU_BARITEM);
    EXPECT_EQ(style.stateId(false), MBI_NORMAL);
    EXPECT_EQ(style.stateId(true), MBI_HOT);

    style.pressed = true;
    EXPECT_EQ(style.stateId(true), MBI_PUSHED);  // pressed beats hot

    style.enabled = false;
    EXPECT_EQ(style.stateId(true), MBI_DISABLED);  // disabled beats everything
}

TEST(ThemedMenuBarBackgroundStyle, PaintWithNoAttachedViewDoesNotCrash) {
    newui::ThemedMenuBarBackgroundStyle style;

    newui::Rect clientBounds;
    style.paint(SharedContext(), newui::Size(400, 32), false, clientBounds);

    EXPECT_FLOAT_EQ(clientBounds.size().width, 400.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 32.0f);
}

// Regression guard: MenuBar (menus.h) arranges its own button children via
// FlexLayout using getClientBounds() - unlike ThemedButtonStyle/etc. (leaf
// controls, no children of their own to arrange), a shrinking clientBounds
// after the theme gets cached by a first real paint() would visibly shift
// MenuBar's buttons a few pixels on the next relayout (e.g. a window
// resize) - reported and confirmed live. computeClientBounds() overrides
// the base ThemedViewStyle behavior specifically to never deflate, exactly
// so this can't happen - this only re-confirms "always full rect", not the
// full before/after-caching distinction (that needs a live HWND, same gap
// as everywhere else theme-caching is involved in this project).
TEST(ThemedMenuBarBackgroundStyle, ComputeClientBoundsAlwaysReturnsFullRect) {
    newui::ThemedMenuBarBackgroundStyle style;

    newui::Rect clientBounds = style.computeClientBounds(newui::Size(400, 32));

    EXPECT_FLOAT_EQ(clientBounds.left(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.top(), 0.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().width, 400.0f);
    EXPECT_FLOAT_EQ(clientBounds.size().height, 32.0f);
}

TEST(ThemedMenuBarBackgroundStyle, PartAndStateAreFixed) {
    TestableThemedMenuBarBackgroundStyle style;

    EXPECT_EQ(style.partId(), MENU_BARBACKGROUND);
    EXPECT_EQ(style.stateId(false), MB_ACTIVE);
    EXPECT_EQ(style.stateId(true), MB_ACTIVE);
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

// ---------------------------------------------------------------------------
// Point/Rect <-> Win32 POINT/RECT (geometry.h) - implicit both ways.
// ---------------------------------------------------------------------------

TEST(PointConversion, ConvertsToWin32Point) {
    newui::Point p(3.0f, -5.0f);
    POINT native = p;
    EXPECT_EQ(native.x, 3);
    EXPECT_EQ(native.y, -5);
}

TEST(PointConversion, ConvertsFromWin32Point) {
    POINT native = { 7, 9 };
    newui::Point p = native;
    EXPECT_FLOAT_EQ(p.x, 7.0f);
    EXPECT_FLOAT_EQ(p.y, 9.0f);
}

TEST(RectConversion, ConvertsToWin32Rect) {
    newui::Rect r(2.0f, 4.0f, 10.0f, 20.0f);
    RECT native = r;
    EXPECT_EQ(native.left, 2);
    EXPECT_EQ(native.top, 4);
    EXPECT_EQ(native.right, 12);
    EXPECT_EQ(native.bottom, 24);
}

TEST(RectConversion, ConvertsFromWin32Rect) {
    RECT native = { 1, 2, 11, 22 };
    newui::Rect r = native;
    EXPECT_FLOAT_EQ(r.left(), 1.0f);
    EXPECT_FLOAT_EQ(r.top(), 2.0f);
    EXPECT_FLOAT_EQ(r.size().width, 10.0f);
    EXPECT_FLOAT_EQ(r.size().height, 20.0f);
}

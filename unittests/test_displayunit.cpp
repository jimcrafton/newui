#include "newui/displayunit.h"

#include <gtest/gtest.h>

using newui::DisplayMetrics;
using newui::DisplayPoint;
using newui::DisplaySize;
using newui::DisplayUnit;
using newui::DisplayValue;

// ---------------------------------------------------------------------------
// DisplayMetrics: Px/Dip scaling
// ---------------------------------------------------------------------------

TEST(DisplayMetrics, PxNeverScalesRegardlessOfDpi) {
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(newui::kBaselineDpi).toPixelsX(28.0f, DisplayUnit::Px), 28.0f);
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(144).toPixelsX(28.0f, DisplayUnit::Px), 28.0f);
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(192).toPixelsY(28.0f, DisplayUnit::Px), 28.0f);
}

TEST(DisplayMetrics, DipScalesLinearlyWithDpi) {
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(newui::kBaselineDpi).toPixelsX(28.0f, DisplayUnit::Dip), 28.0f);
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(144).toPixelsX(28.0f, DisplayUnit::Dip), 42.0f);  // 1.5x
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(192).toPixelsY(28.0f, DisplayUnit::Dip), 56.0f);  // 2x
}

TEST(DisplayMetrics, DpiScaleReportsDpiOverBaseline) {
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(newui::kBaselineDpi).dpiScale(), 1.0f);
    EXPECT_FLOAT_EQ(DisplayMetrics::forDpi(192).dpiScale(), 2.0f);
}

// ---------------------------------------------------------------------------
// DisplayMetrics: Dlu (font-relative) - real font metrics, so exact pixel
// values aren't fixed constants; check the shape of the math instead.
// ---------------------------------------------------------------------------

TEST(DisplayMetrics, DluResolvesToAPositivePixelSize) {
    DisplayMetrics m = DisplayMetrics::forDpi(newui::kBaselineDpi);
    EXPECT_GT(m.toPixelsX(1.0f, DisplayUnit::Dlu), 0.0f);
    EXPECT_GT(m.toPixelsY(1.0f, DisplayUnit::Dlu), 0.0f);
}

TEST(DisplayMetrics, DluScalesUpWithDpiLikeDipDoes) {
    // Not necessarily an exact 2x (font hinting/rounding can nudge shaped
    // widths slightly) - just needs to grow roughly proportionally, the
    // same "bigger DPI, bigger DLU pixel size" property Dip has exactly.
    float atBaseline = DisplayMetrics::forDpi(newui::kBaselineDpi).toPixelsX(10.0f, DisplayUnit::Dlu);
    float at192 = DisplayMetrics::forDpi(192).toPixelsX(10.0f, DisplayUnit::Dlu);
    EXPECT_GT(at192, atBaseline * 1.5f);
    EXPECT_LT(at192, atBaseline * 2.5f);
}

TEST(DisplayMetrics, DluXAndYUseDifferentBaseMetrics) {
    // The classic DLU formula divides by different constants (4 vs 8) off
    // different base measurements (avg char width vs line height) - so the
    // same numeric Dlu value should not resolve to the same pixel size on
    // both axes (this would have been silently wrong if toPixelsX/Y shared
    // one axis-agnostic implementation - see displayunit.h's own comment on
    // why DisplayValue needs separate toPixelsX()/toPixelsY() rather than
    // one toPixels()).
    DisplayMetrics m = DisplayMetrics::forDpi(newui::kBaselineDpi);
    EXPECT_NE(m.toPixelsX(8.0f, DisplayUnit::Dlu), m.toPixelsY(8.0f, DisplayUnit::Dlu));
}

// ---------------------------------------------------------------------------
// DisplayValue: fromString / toString
// ---------------------------------------------------------------------------

TEST(DisplayValue, FromStringParsesPxSuffix) {
    DisplayValue v;
    ASSERT_TRUE(DisplayValue::fromString("23.5px", v));
    EXPECT_FLOAT_EQ(v.value(), 23.5f);
    EXPECT_EQ(v.unit(), DisplayUnit::Px);
}

TEST(DisplayValue, FromStringParsesDipSuffix) {
    DisplayValue v;
    ASSERT_TRUE(DisplayValue::fromString("1.5dip", v));
    EXPECT_FLOAT_EQ(v.value(), 1.5f);
    EXPECT_EQ(v.unit(), DisplayUnit::Dip);
}

TEST(DisplayValue, FromStringParsesDuSuffixAsDlu) {
    DisplayValue v;
    ASSERT_TRUE(DisplayValue::fromString("45.6du", v));
    EXPECT_FLOAT_EQ(v.value(), 45.6f);
    EXPECT_EQ(v.unit(), DisplayUnit::Dlu);
}

TEST(DisplayValue, FromStringIsCaseInsensitive) {
    DisplayValue v;
    ASSERT_TRUE(DisplayValue::fromString("12PX", v));
    EXPECT_EQ(v.unit(), DisplayUnit::Px);

    ASSERT_TRUE(DisplayValue::fromString("12Du", v));
    EXPECT_EQ(v.unit(), DisplayUnit::Dlu);

    ASSERT_TRUE(DisplayValue::fromString("12DIP", v));
    EXPECT_EQ(v.unit(), DisplayUnit::Dip);
}

TEST(DisplayValue, FromStringWithNoSuffixDefaultsToPx) {
    DisplayValue v;
    ASSERT_TRUE(DisplayValue::fromString("28", v));
    EXPECT_FLOAT_EQ(v.value(), 28.0f);
    EXPECT_EQ(v.unit(), DisplayUnit::Px);
}

TEST(DisplayValue, FromStringAllowsWhitespaceBeforeSuffix) {
    DisplayValue v;
    ASSERT_TRUE(DisplayValue::fromString("10 du", v));
    EXPECT_FLOAT_EQ(v.value(), 10.0f);
    EXPECT_EQ(v.unit(), DisplayUnit::Dlu);
}

TEST(DisplayValue, FromStringRejectsUnknownSuffix) {
    DisplayValue v(999.0f, DisplayUnit::Px);
    EXPECT_FALSE(DisplayValue::fromString("12em", v));
    // Untouched on failure.
    EXPECT_FLOAT_EQ(v.value(), 999.0f);
}

TEST(DisplayValue, FromStringRejectsNonNumeric) {
    DisplayValue v;
    EXPECT_FALSE(DisplayValue::fromString("px", v));
    EXPECT_FALSE(DisplayValue::fromString("", v));
}

TEST(DisplayValue, ToStringAlwaysEmitsAnExplicitSuffix) {
    EXPECT_EQ(DisplayValue(28.0f, DisplayUnit::Px).toString(), "28px");
    EXPECT_EQ(DisplayValue(1.5f, DisplayUnit::Dip).toString(), "1.5dip");
    EXPECT_EQ(DisplayValue(45.6f, DisplayUnit::Dlu).toString(), "45.6du");
}

TEST(DisplayValue, ToStringRoundTripsThroughFromString) {
    for (DisplayValue original : {
             DisplayValue(28.0f, DisplayUnit::Px),
             DisplayValue(1.5f, DisplayUnit::Dip),
             DisplayValue(45.6f, DisplayUnit::Dlu),
         }) {
        DisplayValue parsed;
        ASSERT_TRUE(DisplayValue::fromString(original.toString(), parsed));
        EXPECT_EQ(parsed, original);
    }
}

// ---------------------------------------------------------------------------
// DisplayPoint / DisplaySize
// ---------------------------------------------------------------------------

TEST(DisplayPoint, ToPixelsResolvesEachAxisIndependently) {
    DisplayPoint pt{DisplayValue(10.0f, DisplayUnit::Dip), DisplayValue(20.0f, DisplayUnit::Dip)};
    newui::Point px = pt.toPixels(DisplayMetrics::forDpi(192));
    EXPECT_FLOAT_EQ(px.x, 20.0f);
    EXPECT_FLOAT_EQ(px.y, 40.0f);
}

TEST(DisplaySize, ToPixelsResolvesEachAxisIndependently) {
    DisplaySize size{DisplayValue(10.0f, DisplayUnit::Dip), DisplayValue(20.0f, DisplayUnit::Dip)};
    newui::Size px = size.toPixels(DisplayMetrics::forDpi(144));
    EXPECT_FLOAT_EQ(px.width, 15.0f);
    EXPECT_FLOAT_EQ(px.height, 30.0f);
}

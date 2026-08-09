#include "newui/color.h"

#include <gtest/gtest.h>

namespace {

// Loose enough to absorb float rounding through multi-stage conversions
// (e.g. RGB -> linear -> XYZ -> Lab), tight enough to catch a wrong
// coefficient or formula error.
constexpr float kTol = 0.01f;
constexpr float kLooseTol = 0.6f;  // for hand-derived reference values below

void ExpectColorNear(const newui::Color& c, float r, float g, float b, float a, float tol = kTol) {
    EXPECT_NEAR(c.r, r, tol);
    EXPECT_NEAR(c.g, g, tol);
    EXPECT_NEAR(c.b, b, tol);
    EXPECT_NEAR(c.a, a, tol);
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction, equality, packed-integer constructors
// ---------------------------------------------------------------------------

TEST(Color, DefaultConstructorIsOpaqueBlack) {
    newui::Color c;
    ExpectColorNear(c, 0.0f, 0.0f, 0.0f, 1.0f);
}

TEST(Color, FloatConstructorAndEquality) {
    newui::Color a(0.2f, 0.4f, 0.6f, 0.8f);
    newui::Color b(0.2f, 0.4f, 0.6f, 0.8f);
    newui::Color c(0.2f, 0.4f, 0.6f, 0.9f);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

// 0xAARRGGBB, matching blend2d's BLRgba32 packing - see color.h's
// Color(uint32_t) doc comment.
TEST(Color, Uint32ConstructorRgbaLayout) {
    newui::Color c(0x80336699u);
    ExpectColorNear(c, 0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 0x80 / 255.0f);
}

TEST(Color, Uint32ConstructorNoAlphaForcesOpaque) {
    newui::Color c(0x336699u, false);
    ExpectColorNear(c, 0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 1.0f);
}

TEST(Color, Uint64ConstructorRgbaLayout) {
    newui::Color c(0x8000330066009900ull);
    ExpectColorNear(c, 0x3300 / 65535.0f, 0x6600 / 65535.0f, 0x9900 / 65535.0f, 0x8000 / 65535.0f);
}

TEST(Color, Uint64ConstructorNoAlphaForcesOpaque) {
    newui::Color c(0x0000330066009900ull, false);
    ExpectColorNear(c, 0x3300 / 65535.0f, 0x6600 / 65535.0f, 0x9900 / 65535.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// HSL
//
// Known values: RapidTables' RGB<->HSL reference page gives red=(0,100%,50%),
// lime=(120,100%,50%), blue=(240,100%,50%).
// https://www.rapidtables.com/convert/color/rgb-to-hsl.html
// Algorithm: https://en.wikipedia.org/wiki/HSL_and_HSV
// ---------------------------------------------------------------------------

TEST(Color, ToHSLKnownPrimaries) {
    newui::HSLColor red = newui::Color(1.0f, 0.0f, 0.0f).toHSL();
    EXPECT_NEAR(red.h, 0.0f, kTol);
    EXPECT_NEAR(red.s, 1.0f, kTol);
    EXPECT_NEAR(red.l, 0.5f, kTol);

    newui::HSLColor lime = newui::Color(0.0f, 1.0f, 0.0f).toHSL();
    EXPECT_NEAR(lime.h, 120.0f, kTol);
    EXPECT_NEAR(lime.s, 1.0f, kTol);
    EXPECT_NEAR(lime.l, 0.5f, kTol);

    newui::HSLColor blue = newui::Color(0.0f, 0.0f, 1.0f).toHSL();
    EXPECT_NEAR(blue.h, 240.0f, kTol);
    EXPECT_NEAR(blue.s, 1.0f, kTol);
    EXPECT_NEAR(blue.l, 0.5f, kTol);
}

TEST(Color, FromHSLKnownPrimaries) {
    ExpectColorNear(newui::Color::fromHSL(0.0f, 1.0f, 0.5f), 1.0f, 0.0f, 0.0f, 1.0f);
    ExpectColorNear(newui::Color::fromHSL(120.0f, 1.0f, 0.5f), 0.0f, 1.0f, 0.0f, 1.0f);
    ExpectColorNear(newui::Color::fromHSL(240.0f, 1.0f, 0.5f), 0.0f, 0.0f, 1.0f, 1.0f);
}

TEST(Color, GrayHasZeroSaturation) {
    newui::HSLColor gray = newui::Color(0.5f, 0.5f, 0.5f).toHSL();
    EXPECT_NEAR(gray.s, 0.0f, kTol);
}

TEST(Color, HSLRoundTrip) {
    newui::Color orig(0.2f, 0.7f, 0.9f, 0.6f);
    newui::Color back = newui::Color::fromHSL(orig.toHSL());
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

TEST(Color, HueWrapsAndNegativeHueNormalizes) {
    newui::Color a = newui::Color::fromHSL(400.0f, 1.0f, 0.5f);
    newui::Color b = newui::Color::fromHSL(40.0f, 1.0f, 0.5f);
    ExpectColorNear(a, b.r, b.g, b.b, b.a);

    newui::Color c = newui::Color::fromHSL(-40.0f, 1.0f, 0.5f);
    newui::Color d = newui::Color::fromHSL(320.0f, 1.0f, 0.5f);
    ExpectColorNear(c, d.r, d.g, d.b, d.a);
}

// ---------------------------------------------------------------------------
// HSV
//
// Known values: pure saturated primaries are definitionally s=100%,v=100%
// in HSV, with the same hue circle as HSL (0/120/240 for red/lime/blue).
// https://en.wikipedia.org/wiki/HSL_and_HSV
// ---------------------------------------------------------------------------

TEST(Color, ToHSVKnownPrimaries) {
    newui::HSVColor red = newui::Color(1.0f, 0.0f, 0.0f).toHSV();
    EXPECT_NEAR(red.h, 0.0f, kTol);
    EXPECT_NEAR(red.s, 1.0f, kTol);
    EXPECT_NEAR(red.v, 1.0f, kTol);

    newui::HSVColor lime = newui::Color(0.0f, 1.0f, 0.0f).toHSV();
    EXPECT_NEAR(lime.h, 120.0f, kTol);
    EXPECT_NEAR(lime.s, 1.0f, kTol);
    EXPECT_NEAR(lime.v, 1.0f, kTol);
}

TEST(Color, ToHSVBlackIsZeroSaturationAndValue) {
    newui::HSVColor black = newui::Color(0.0f, 0.0f, 0.0f).toHSV();
    EXPECT_NEAR(black.s, 0.0f, kTol);
    EXPECT_NEAR(black.v, 0.0f, kTol);
}

TEST(Color, FromHSVKnownPrimaries) {
    ExpectColorNear(newui::Color::fromHSV(120.0f, 1.0f, 1.0f), 0.0f, 1.0f, 0.0f, 1.0f);
}

TEST(Color, HSVRoundTrip) {
    newui::Color orig(0.3f, 0.6f, 0.1f, 0.5f);
    newui::Color back = newui::Color::fromHSV(orig.toHSV());
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

// ---------------------------------------------------------------------------
// CIE L*a*b*
//
// D65 white/black endpoints (L=100/0, a=b=0) are the textbook definition:
// https://en.wikipedia.org/wiki/CIELAB_color_space
//
// The pure-sRGB-red reference value below was hand-derived (not read off a
// precomputed table - colorhexa.com/convertingcolors.com/easyrgb.com all
// blocked automated fetches) from two independently-verified sources: the
// sRGB->XYZ matrix and D65 white point Wikipedia's sRGB article gives
// (https://en.wikipedia.org/wiki/SRGB, matching this codebase's
// linearRgbToXyz() coefficients to the precision given), fed through the
// standard CIELAB f(t) nonlinearity
// (https://en.wikipedia.org/wiki/CIELAB_color_space#Converting_between_CIELAB_and_CIEXYZ_coordinates).
// That hand math gives red ~= (L 53.2, a 80.2, b 67.2), which also matches
// the commonly-published approximate value for sRGB red in Lab (~53.24,
// 80.09, 67.20) found across color-science references - kLooseTol reflects
// the hand-rounding in that derivation, not implementation uncertainty.
TEST(Color, ToLabKnownEndpoints) {
    newui::LabColor white = newui::Color(1.0f, 1.0f, 1.0f).toLab();
    EXPECT_NEAR(white.l, 100.0f, kTol);
    EXPECT_NEAR(white.a, 0.0f, kTol);
    EXPECT_NEAR(white.b, 0.0f, kTol);

    newui::LabColor black = newui::Color(0.0f, 0.0f, 0.0f).toLab();
    EXPECT_NEAR(black.l, 0.0f, kTol);
    EXPECT_NEAR(black.a, 0.0f, kTol);
    EXPECT_NEAR(black.b, 0.0f, kTol);
}

TEST(Color, ToLabKnownRed) {
    newui::LabColor red = newui::Color(1.0f, 0.0f, 0.0f).toLab();
    EXPECT_NEAR(red.l, 53.24f, kLooseTol);
    EXPECT_NEAR(red.a, 80.09f, kLooseTol);
    EXPECT_NEAR(red.b, 67.20f, kLooseTol);
}

TEST(Color, LabRoundTrip) {
    newui::Color orig(0.4f, 0.2f, 0.8f, 0.7f);
    newui::Color back = newui::Color::fromLab(orig.toLab());
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a, 0.02f);
}

// ---------------------------------------------------------------------------
// CMY / CMYK
//
// Known values: pure red has no cyan and full magenta+yellow with no black
// component - elementary subtractive-color facts, not something requiring
// an external source.
// ---------------------------------------------------------------------------

TEST(Color, ToCMYKnownRed) {
    newui::CMYColor cmy = newui::Color(1.0f, 0.0f, 0.0f).toCMY();
    EXPECT_NEAR(cmy.c, 0.0f, kTol);
    EXPECT_NEAR(cmy.m, 1.0f, kTol);
    EXPECT_NEAR(cmy.y, 1.0f, kTol);
}

TEST(Color, CMYRoundTrip) {
    newui::Color orig(0.3f, 0.6f, 0.9f, 0.4f);
    newui::Color back = newui::Color::fromCMY(orig.toCMY());
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

TEST(Color, ToCMYKKnownBlackAndWhite) {
    newui::CMYKColor black = newui::Color(0.0f, 0.0f, 0.0f).toCMYK();
    EXPECT_NEAR(black.k, 1.0f, kTol);
    EXPECT_NEAR(black.c, 0.0f, kTol);

    newui::CMYKColor white = newui::Color(1.0f, 1.0f, 1.0f).toCMYK();
    EXPECT_NEAR(white.k, 0.0f, kTol);
    EXPECT_NEAR(white.c, 0.0f, kTol);
    EXPECT_NEAR(white.m, 0.0f, kTol);
    EXPECT_NEAR(white.y, 0.0f, kTol);
}

TEST(Color, CMYKRoundTrip) {
    newui::Color orig(0.3f, 0.6f, 0.9f, 0.4f);
    newui::Color back = newui::Color::fromCMYK(orig.toCMYK());
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

// ---------------------------------------------------------------------------
// Y'UV (ITU-R BT.709)
// https://en.wikipedia.org/wiki/Y%E2%80%B2UV
// ---------------------------------------------------------------------------

TEST(Color, ToYUVKnownWhite) {
    newui::YUVColor yuv = newui::Color(1.0f, 1.0f, 1.0f).toYUV();
    EXPECT_NEAR(yuv.y, 1.0f, kTol);
    EXPECT_NEAR(yuv.u, 0.0f, kTol);
    EXPECT_NEAR(yuv.v, 0.0f, kTol);
}

TEST(Color, YUVRoundTrip) {
    newui::Color orig(0.3f, 0.6f, 0.9f, 0.4f);
    newui::Color back = newui::Color::fromYUV(orig.toYUV());
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

// ---------------------------------------------------------------------------
// Luminosity (WCAG relative luminance)
//
// The 0.2126/0.7152/0.0722 primary weights are defined verbatim in the
// WCAG spec, so a pure primary's luminosity equals its weight exactly.
// https://www.w3.org/TR/WCAG21/#dfn-relative-luminance
// ---------------------------------------------------------------------------

TEST(Color, LuminosityKnownEndpointsAndPrimaries) {
    EXPECT_NEAR(newui::Color(1.0f, 1.0f, 1.0f).luminosity(), 1.0f, kTol);
    EXPECT_NEAR(newui::Color(0.0f, 0.0f, 0.0f).luminosity(), 0.0f, kTol);
    EXPECT_NEAR(newui::Color(1.0f, 0.0f, 0.0f).luminosity(), 0.2126f, kTol);
    EXPECT_NEAR(newui::Color(0.0f, 1.0f, 0.0f).luminosity(), 0.7152f, kTol);
    EXPECT_NEAR(newui::Color(0.0f, 0.0f, 1.0f).luminosity(), 0.0722f, kTol);
}

TEST(Color, GreenReadsBrighterThanRed) {
    EXPECT_GT(newui::Color(0.0f, 1.0f, 0.0f).luminosity(), newui::Color(1.0f, 0.0f, 0.0f).luminosity());
}

TEST(Color, WithLuminosityHitsTargetForMildAdjustment) {
    newui::Color adjusted = newui::Color(0.6f, 0.35f, 0.35f, 0.9f).withLuminosity(0.3f);
    EXPECT_NEAR(adjusted.luminosity(), 0.3f, kTol);
    EXPECT_NEAR(adjusted.a, 0.9f, kTol);
}

TEST(Color, WithLuminosityZeroGivesBlack) {
    newui::Color c = newui::Color(0.7f, 0.2f, 0.9f).withLuminosity(0.0f);
    ExpectColorNear(c, 0.0f, 0.0f, 0.0f, 1.0f);
}

TEST(Color, WithLuminosityOnBlackGivesFlatGray) {
    newui::Color c = newui::Color(0.0f, 0.0f, 0.0f, 0.5f).withLuminosity(0.5f);
    EXPECT_NEAR(c.r, c.g, kTol);
    EXPECT_NEAR(c.g, c.b, kTol);
    EXPECT_NEAR(c.luminosity(), 0.5f, kTol);
    EXPECT_NEAR(c.a, 0.5f, kTol);
}

TEST(Color, GrayscalePreservesEndpointsAndAlpha) {
    newui::Color white = newui::Color(1.0f, 1.0f, 1.0f, 0.5f).grayscale();
    ExpectColorNear(white, 1.0f, 1.0f, 1.0f, 0.5f);

    newui::Color mixed = newui::Color(0.3f, 0.7f, 0.1f).grayscale();
    EXPECT_NEAR(mixed.r, mixed.g, kTol);
    EXPECT_NEAR(mixed.g, mixed.b, kTol);
}

// ---------------------------------------------------------------------------
// sRGB linear <-> gamma
//
// Endpoints (0/1 map to themselves) are definitional. The sRGB transfer
// function's exact piecewise-linear/power-law form is documented at
// https://en.wikipedia.org/wiki/SRGB#Transfer_function_(%22gamma%22)
// ---------------------------------------------------------------------------

TEST(Color, LinearGammaEndpoints) {
    EXPECT_NEAR(newui::Color(1.0f, 1.0f, 1.0f).toLinear().r, 1.0f, kTol);
    EXPECT_NEAR(newui::Color(0.0f, 0.0f, 0.0f).toLinear().r, 0.0f, kTol);
}

TEST(Color, LinearDarkensMidGray) {
    // sRGB's power-law segment darkens on the way to linear light - 0.5
    // gamma-encoded should map to well under 0.5 linear.
    EXPECT_LT(newui::Color(0.5f, 0.5f, 0.5f).toLinear().r, 0.25f);
}

TEST(Color, LinearGammaRoundTrip) {
    newui::Color c(0.2f, 0.5f, 0.9f, 0.6f);
    newui::Color back = c.toLinear().toGamma();
    ExpectColorNear(back, c.r, c.g, c.b, c.a);
}

TEST(Color, WithGammaIdentityAtOne) {
    newui::Color c(0.5f, 0.5f, 0.5f);
    ExpectColorNear(c.withGamma(1.0f), c.r, c.g, c.b, c.a);
}

TEST(Color, WithGammaDarkensAboveOneBrightensBelowOne) {
    newui::Color c(0.5f, 0.5f, 0.5f);
    EXPECT_LT(c.withGamma(2.2f).r, 0.5f);
    EXPECT_GT(c.withGamma(1.0f / 2.2f).r, 0.5f);
}

// ---------------------------------------------------------------------------
// Brightness / contrast (CSS Filter Effects definitions)
// https://www.w3.org/TR/filter-effects-1/#funcdef-filter-brightness
// https://www.w3.org/TR/filter-effects-1/#funcdef-filter-contrast
// ---------------------------------------------------------------------------

TEST(Color, WithBrightnessMultipliesChannels) {
    newui::Color c(0.4f, 0.4f, 0.4f, 0.9f);
    ExpectColorNear(c.withBrightness(1.0f), c.r, c.g, c.b, c.a);
    ExpectColorNear(c.withBrightness(2.0f), 0.8f, 0.8f, 0.8f, 0.9f);
}

TEST(Color, WithContrastScalesAroundMidpoint) {
    newui::Color c(0.6f, 0.6f, 0.6f, 0.9f);
    ExpectColorNear(c.withContrast(1.0f), c.r, c.g, c.b, c.a);
    // (0.6-0.5)*2+0.5 = 0.7
    ExpectColorNear(c.withContrast(2.0f), 0.7f, 0.7f, 0.7f, 0.9f);
}

// ---------------------------------------------------------------------------
// Complement / invert
// https://en.wikipedia.org/wiki/Complementary_colors
// ---------------------------------------------------------------------------

TEST(Color, ComplementOfRedIsCyan) {
    newui::Color comp = newui::Color(1.0f, 0.0f, 0.0f).complement();
    ExpectColorNear(comp, 0.0f, 1.0f, 1.0f, 1.0f);
}

TEST(Color, ComplementTwiceReturnsOriginal) {
    newui::Color orig(1.0f, 0.0f, 0.0f);
    newui::Color back = orig.complement().complement();
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

TEST(Color, InvertNegatesChannels) {
    newui::Color c(0.2f, 0.7f, 1.0f, 0.5f);
    ExpectColorNear(c.invert(), 0.8f, 0.3f, 0.0f, 0.5f);
}

TEST(Color, InvertTwiceReturnsOriginal) {
    newui::Color orig(0.2f, 0.7f, 1.0f, 0.5f);
    newui::Color back = orig.invert().invert();
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

TEST(Color, InterpolateEndpointsAndMidpoint) {
    newui::Color a(0.0f, 0.0f, 0.0f, 1.0f);
    newui::Color b(1.0f, 1.0f, 1.0f, 0.0f);

    ExpectColorNear(a.interpolate(b, 0.0f), a.r, a.g, a.b, a.a);
    ExpectColorNear(a.interpolate(b, 1.0f), b.r, b.g, b.b, b.a);
    ExpectColorNear(a.interpolate(b, 0.5f), 0.5f, 0.5f, 0.5f, 0.5f);
}

TEST(Color, InterpolateExtrapolatesPastEndpoints) {
    newui::Color a(0.0f, 0.0f, 0.0f);
    newui::Color b(1.0f, 1.0f, 1.0f);
    // t isn't clamped, matching this class's general no-clamping convention.
    EXPECT_NEAR(a.interpolate(b, 1.5f).r, 1.5f, kTol);
    EXPECT_NEAR(a.interpolate(b, -0.5f).r, -0.5f, kTol);
}

// ---------------------------------------------------------------------------
// Packed bytes (generic, RGB24, BGRA32)
// ---------------------------------------------------------------------------

TEST(Color, ToRGB24KnownValues) {
    uint8_t out[3];
    newui::Color(1.0f, 0.5f, 0.0f).toRGB24(out);
    EXPECT_EQ(out[0], 255);
    EXPECT_EQ(out[1], 128);
    EXPECT_EQ(out[2], 0);
}

TEST(Color, ToBGRA32KnownValues) {
    uint8_t out[4];
    newui::Color(1.0f, 0.5f, 0.25f, 0.75f).toBGRA32(out);
    EXPECT_EQ(out[0], 64);   // B
    EXPECT_EQ(out[1], 128);  // G
    EXPECT_EQ(out[2], 255);  // R
    EXPECT_EQ(out[3], 191);  // A
}

TEST(Color, FromRGB24Uint32) {
    newui::Color c = newui::Color::fromRGB24(0x336699u);
    ExpectColorNear(c, 0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 1.0f);
}

TEST(Color, FromBGRA32Uint32) {
    newui::Color c = newui::Color::fromBGRA32(0x80336699u);
    ExpectColorNear(c, 0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 0x80 / 255.0f);
}

TEST(Color, ToPackedBytesUndersizedBufferReportsSizeWithoutWriting) {
    newui::Color c(1.0f, 0.5f, 0.0f);
    uint8_t out[2] = {0xAA, 0xAA};
    newui::PackedChannel channels[] = {
        {newui::ColorChannel::Red, 8}, {newui::ColorChannel::Green, 8}, {newui::ColorChannel::Blue, 8}
    };
    size_t needed = c.toPackedBytes(channels, 3, out, 2);
    EXPECT_EQ(needed, 3u);
    EXPECT_EQ(out[0], 0xAA);
    EXPECT_EQ(out[1], 0xAA);
}

TEST(Color, PackedBytesRGB565KnownValues) {
    newui::PackedChannel channels[] = {
        {newui::ColorChannel::Red, 5}, {newui::ColorChannel::Green, 6}, {newui::ColorChannel::Blue, 5}
    };

    uint8_t redOut[2];
    newui::Color(1.0f, 0.0f, 0.0f).toPackedBytes(channels, 3, redOut, 2);
    uint16_t redPacked = (uint16_t(redOut[0]) << 8) | redOut[1];
    EXPECT_EQ(redPacked, 0xF800);

    uint8_t whiteOut[2];
    newui::Color(1.0f, 1.0f, 1.0f).toPackedBytes(channels, 3, whiteOut, 2);
    uint16_t whitePacked = (uint16_t(whiteOut[0]) << 8) | whiteOut[1];
    EXPECT_EQ(whitePacked, 0xFFFF);
}

TEST(Color, FromPackedBytesUndersizedInputFailsWithoutModifyingOutput) {
    newui::Color sentinel(0.11f, 0.22f, 0.33f, 0.44f);
    newui::Color out = sentinel;
    uint8_t in[2] = {1, 2};
    newui::PackedChannel channels[] = {
        {newui::ColorChannel::Red, 8}, {newui::ColorChannel::Green, 8}, {newui::ColorChannel::Blue, 8}
    };
    bool ok = newui::Color::fromPackedBytes(channels, 3, in, 2, out);
    EXPECT_FALSE(ok);
    ExpectColorNear(out, sentinel.r, sentinel.g, sentinel.b, sentinel.a, 0.0001f);
}

// ---------------------------------------------------------------------------
// toString() / fromString() (CSS hex notation)
// https://www.w3.org/TR/css-color-4/#hex-notation
// ---------------------------------------------------------------------------

TEST(Color, ToStringFormat) {
    EXPECT_EQ(newui::Color(1.0f, 0.5f, 0.0f, 0.75f).toString(), "#ff8000bf");
}

TEST(Color, FromStringEightDigitHex) {
    newui::Color out;
    ASSERT_TRUE(newui::Color::fromString("#ff8000bf", out));
    ExpectColorNear(out, 1.0f, 0x80 / 255.0f, 0.0f, 0xbf / 255.0f);
}

TEST(Color, FromStringSixDigitHexDefaultsOpaque) {
    newui::Color out;
    ASSERT_TRUE(newui::Color::fromString("336699", out));
    ExpectColorNear(out, 0x33 / 255.0f, 0x66 / 255.0f, 0x99 / 255.0f, 1.0f);
}

TEST(Color, FromStringThreeDigitShorthandDoublesNibbles) {
    newui::Color out;
    ASSERT_TRUE(newui::Color::fromString("#0f3", out));
    ExpectColorNear(out, 0.0f, 1.0f, 0.2f, 1.0f);
}

TEST(Color, FromStringInvalidHexLengthFails) {
    newui::Color sentinel(0.11f, 0.22f, 0.33f, 0.44f);
    newui::Color out = sentinel;
    EXPECT_FALSE(newui::Color::fromString("#12345", out));
    ExpectColorNear(out, sentinel.r, sentinel.g, sentinel.b, sentinel.a, 0.0001f);
}

TEST(Color, StringRoundTrip) {
    newui::Color orig(0.42f, 0.13f, 0.88f, 0.6f);
    newui::Color back;
    ASSERT_TRUE(newui::Color::fromString(orig.toString(), back));
    ExpectColorNear(back, orig.r, orig.g, orig.b, orig.a);
}

// ---------------------------------------------------------------------------
// Named colors (CSS Color Module Level 4)
//
// Verified against MDN's named-color reference page:
// https://developer.mozilla.org/en-US/docs/Web/CSS/named-color
// https://www.w3.org/TR/css-color-4/#named-colors
// ---------------------------------------------------------------------------

TEST(Color, FromNameKnownValues) {
    newui::Color red;
    ASSERT_TRUE(newui::Color::fromName("red", red));
    ExpectColorNear(red, 1.0f, 0.0f, 0.0f, 1.0f);

    newui::Color cornflowerblue;
    ASSERT_TRUE(newui::Color::fromName("cornflowerblue", cornflowerblue));
    ExpectColorNear(cornflowerblue, 0x64 / 255.0f, 0x95 / 255.0f, 0xed / 255.0f, 1.0f);

    newui::Color rebeccapurple;
    ASSERT_TRUE(newui::Color::fromName("rebeccapurple", rebeccapurple));
    ExpectColorNear(rebeccapurple, 0x66 / 255.0f, 0x33 / 255.0f, 0x99 / 255.0f, 1.0f);

    newui::Color forestgreen;
    ASSERT_TRUE(newui::Color::fromName("forestgreen", forestgreen));
    ExpectColorNear(forestgreen, 0x22 / 255.0f, 0x8b / 255.0f, 0x22 / 255.0f, 1.0f);
}

TEST(Color, FromNameTransparentHasZeroAlpha) {
    newui::Color t;
    ASSERT_TRUE(newui::Color::fromName("transparent", t));
    EXPECT_NEAR(t.a, 0.0f, kTol);
}

TEST(Color, FromNameIsCaseInsensitive) {
    newui::Color a, b, c;
    EXPECT_TRUE(newui::Color::fromName("RED", a));
    EXPECT_TRUE(newui::Color::fromName("Red", b));
    EXPECT_TRUE(newui::Color::fromName("rEd", c));
    ExpectColorNear(a, 1.0f, 0.0f, 0.0f, 1.0f);
    ExpectColorNear(b, 1.0f, 0.0f, 0.0f, 1.0f);
    ExpectColorNear(c, 1.0f, 0.0f, 0.0f, 1.0f);
}

TEST(Color, FromNameUnknownFailsWithoutModifyingOutput) {
    newui::Color sentinel(0.11f, 0.22f, 0.33f, 0.44f);
    newui::Color out = sentinel;
    EXPECT_FALSE(newui::Color::fromName("notacolor", out));
    ExpectColorNear(out, sentinel.r, sentinel.g, sentinel.b, sentinel.a, 0.0001f);
}

// Spread of names across the alphabet, including entries immediately
// adjacent to "transparent" (the one entry not sourced directly from the
// CSS spec table - it's a keyword rather than a color - and so the most
// likely spot for a sort-order mistake that would break the binary
// search in fromName()).
TEST(Color, FromNameLookupWorksAcrossTheAlphabet) {
    const char* names[] = {
        "aliceblue", "beige", "chocolate", "darkslategrey", "firebrick",
        "gainsboro", "honeydew", "indigo", "khaki", "lavenderblush",
        "mediumvioletred", "navajowhite", "olivedrab", "palevioletred",
        "rosybrown", "sandybrown", "thistle", "tomato", "turquoise",
        "violet", "wheat", "yellowgreen"
    };
    for (const char* name : names) {
        newui::Color c;
        EXPECT_TRUE(newui::Color::fromName(name, c)) << "lookup failed for '" << name << "'";
    }
}

TEST(Color, FromStringFallsBackToNameLookup) {
    newui::Color c;
    ASSERT_TRUE(newui::Color::fromString("cornflowerblue", c));
    ExpectColorNear(c, 0x64 / 255.0f, 0x95 / 255.0f, 0xed / 255.0f, 1.0f);
}

// Color(const std::string&) goes through fromName() only (not fromString()),
// so it accepts a CSS color name but not hex notation - unlike the free
// fromString() function above, which tries hex first and falls back to
// fromName().
TEST(Color, StringConstructorAcceptsColorName) {
    newui::Color viaName("cornflowerblue");
    ExpectColorNear(viaName, 0x64 / 255.0f, 0x95 / 255.0f, 0xed / 255.0f, 1.0f);
}

TEST(Color, StringConstructorDoesNotAcceptHex) {
    // "#ff0000" isn't a recognized color name, so fromName() fails inside
    // the constructor and it's left at its default-constructed value
    // (opaque black) rather than being parsed as hex red.
    newui::Color viaHex("#ff0000");
    ExpectColorNear(viaHex, 0.0f, 0.0f, 0.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// fromSystemColor() / SystemColor
// ---------------------------------------------------------------------------

// The strongest correctness check available here: fromSystemColor() should
// be exactly GetSysColor()'s COLORREF split into normalized r/g/b via
// GetRValue()/GetGValue()/GetBValue() - independent of whatever the current
// Windows theme's actual color values are.
TEST(Color, FromSystemColorMatchesRawGetSysColor) {
    COLORREF ref = ::GetSysColor(COLOR_WINDOW);
    newui::Color c = newui::Color::fromSystemColor(newui::SystemColor::Window);
    ExpectColorNear(c, GetRValue(ref) / 255.0f, GetGValue(ref) / 255.0f, GetBValue(ref) / 255.0f, 1.0f, 0.001f);
}

TEST(Color, FromSystemColorIsAlwaysOpaque) {
    newui::Color c = newui::Color::fromSystemColor(newui::SystemColor::Highlight);
    EXPECT_FLOAT_EQ(c.a, 1.0f);
}

// SystemColor is numbered on its own terms (not the same values as Win32's
// COLOR_* constants - see color_constants.h), so correctness of
// Color::fromSystemColor()'s internal translation back to COLOR_* can only
// be checked indirectly, by comparing its result against a raw GetSysColor()
// call using the COLOR_* constant each SystemColor is documented to mean.
TEST(Color, FromSystemColorTranslatesToTheCorrectWindowsConstant) {
    ExpectColorNear(newui::Color::fromSystemColor(newui::SystemColor::ButtonFace),
        GetRValue(::GetSysColor(COLOR_BTNFACE)) / 255.0f,
        GetGValue(::GetSysColor(COLOR_BTNFACE)) / 255.0f,
        GetBValue(::GetSysColor(COLOR_BTNFACE)) / 255.0f, 1.0f, 0.001f);

    ExpectColorNear(newui::Color::fromSystemColor(newui::SystemColor::Highlight),
        GetRValue(::GetSysColor(COLOR_HIGHLIGHT)) / 255.0f,
        GetGValue(::GetSysColor(COLOR_HIGHLIGHT)) / 255.0f,
        GetBValue(::GetSysColor(COLOR_HIGHLIGHT)) / 255.0f, 1.0f, 0.001f);

    ExpectColorNear(newui::Color::fromSystemColor(newui::SystemColor::WindowText),
        GetRValue(::GetSysColor(COLOR_WINDOWTEXT)) / 255.0f,
        GetGValue(::GetSysColor(COLOR_WINDOWTEXT)) / 255.0f,
        GetBValue(::GetSysColor(COLOR_WINDOWTEXT)) / 255.0f, 1.0f, 0.001f);
}

TEST(Color, FromSystemColorIsStableAcrossRepeatedCalls) {
    newui::Color first = newui::Color::fromSystemColor(newui::SystemColor::ButtonFace);
    newui::Color second = newui::Color::fromSystemColor(newui::SystemColor::ButtonFace);
    EXPECT_TRUE(first == second);
}

TEST(Color, FromSystemColorProducesValidComponentsForEveryEnumerator) {
    const newui::SystemColor colors[] = {
        newui::SystemColor::ScrollBar, newui::SystemColor::Desktop, newui::SystemColor::ActiveCaption,
        newui::SystemColor::InactiveCaption, newui::SystemColor::Menu, newui::SystemColor::Window,
        newui::SystemColor::WindowFrame, newui::SystemColor::MenuText, newui::SystemColor::WindowText,
        newui::SystemColor::CaptionText, newui::SystemColor::ActiveBorder, newui::SystemColor::InactiveBorder,
        newui::SystemColor::AppWorkspace, newui::SystemColor::Highlight, newui::SystemColor::HighlightText,
        newui::SystemColor::ButtonFace, newui::SystemColor::ButtonShadow, newui::SystemColor::GrayText,
        newui::SystemColor::ButtonText, newui::SystemColor::InactiveCaptionText, newui::SystemColor::ButtonHighlight,
        newui::SystemColor::DarkShadow3D, newui::SystemColor::Light3D, newui::SystemColor::InfoText,
        newui::SystemColor::InfoBackground, newui::SystemColor::Hotlight, newui::SystemColor::GradientActiveCaption,
        newui::SystemColor::GradientInactiveCaption, newui::SystemColor::MenuHighlight, newui::SystemColor::MenuBar,
    };

    for (newui::SystemColor sc : colors) {
        newui::Color c = newui::Color::fromSystemColor(sc);
        EXPECT_GE(c.r, 0.0f);
        EXPECT_LE(c.r, 1.0f);
        EXPECT_GE(c.g, 0.0f);
        EXPECT_LE(c.g, 1.0f);
        EXPECT_GE(c.b, 0.0f);
        EXPECT_LE(c.b, 1.0f);
        EXPECT_FLOAT_EQ(c.a, 1.0f);
    }
}

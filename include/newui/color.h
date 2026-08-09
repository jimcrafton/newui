#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include "newui/color_constants.h"
#include "newui/newui.h"

#include <blend2d/blend2d.h>

namespace newui {

    

    // Hue in degrees [0, 360), saturation/lightness/alpha in [0.0, 1.0].
    // See Color::fromHSL()/Color::toHSL().
    struct HSLColor {
        float h = 0.0f;
        float s = 0.0f;
        float l = 0.0f;
        float a = 1.0f;
    };

    // Hue in degrees [0, 360), saturation/value/alpha in [0.0, 1.0].
    // See Color::fromHSV()/Color::toHSV().
    struct HSVColor {
        float h = 0.0f;
        float s = 0.0f;
        float v = 0.0f;
        float a = 1.0f;
    };

    // CIE L*a*b* (D65 white point): l is lightness [0, 100], a/b are the
    // green-red and blue-yellow axes (unbounded, roughly [-128, 127] for
    // colors within the sRGB gamut), alpha is opacity [0.0, 1.0]. Named
    // alpha rather than a to not collide with the a axis.
    // See Color::fromLab()/Color::toLab().
    struct LabColor {
        float l = 0.0f;
        float a = 0.0f;
        float b = 0.0f;
        float alpha = 1.0f;
    };

    // Subtractive cyan/magenta/yellow, each [0.0, 1.0], alpha [0.0, 1.0].
    // See Color::fromCMY()/Color::toCMY().
    struct CMYColor {
        float c = 0.0f;
        float m = 0.0f;
        float y = 0.0f;
        float a = 1.0f;
    };

    // Subtractive cyan/magenta/yellow/key(black), each [0.0, 1.0], alpha
    // [0.0, 1.0]. See Color::fromCMYK()/Color::toCMYK().
    struct CMYKColor {
        float c = 0.0f;
        float m = 0.0f;
        float y = 0.0f;
        float k = 0.0f;
        float a = 1.0f;
    };

    // Y'UV (ITU-R BT.709): y is luma [0.0, 1.0], u/v are chroma, each
    // roughly [-0.5, 0.5], alpha [0.0, 1.0]. See Color::fromYUV()/
    // Color::toYUV().
    struct YUVColor {
        float y = 0.0f;
        float u = 0.0f;
        float v = 0.0f;
        float a = 1.0f;
    };

    // Identifies one of Color's r/g/b/a components, for describing a packed
    // pixel layout to Color::toPackedBytes().
    enum class ColorChannel : uint8_t {
        Red,
        Green,
        Blue,
        Alpha
    };

    // One channel within a packed pixel format: which component it holds,
    // and how many bits of the packed value it occupies. See
    // Color::toPackedBytes().
    struct PackedChannel {
        ColorChannel channel;
        uint8_t bits;
    };

    // RGBA color with components in [0.0, 1.0]. Other ways of specifying a
    // color can be constructed via the fromHSL()/fromHSV()/fromLab()/
    // fromCMY()/fromCMYK()/fromYUV() factory functions, which all resolve
    // down to this same r/g/b/a storage.
    class Color {
    public:
        Color() = default;
        constexpr Color(float r, float g, float b, float a = 1.0f) noexcept
            : r(r), g(g), b(b), a(a) {}

        // 32-bit packed color: 0xAARRGGBB when hasAlpha (matching
        // blend2d's BLRgba32 packing - alpha in the top byte), or
        // 0x00RRGGBB (top byte ignored, alpha forced fully opaque) when
        // not. Delegates to blend2d's own BLRgba32->BLRgba conversion
        // rather than re-deriving the byte->float math here.
        explicit Color(uint32_t packed, bool hasAlpha = true) noexcept {
            BLRgba rgba(BLRgba32(hasAlpha ? packed : (packed | 0xFF000000u)));
            r = rgba.r;
            g = rgba.g;
            b = rgba.b;
            a = rgba.a;
        }

        // 64-bit packed color: 0xAAAARRRRGGGGBBBB when hasAlpha (matching
        // blend2d's BLRgba64 packing - alpha in the top 16 bits), or the
        // low 48 bits as 0x0000RRRRGGGGBBBB (top 16 bits ignored, alpha
        // forced fully opaque) when not. Each channel is 16 bits.
        // Delegates to blend2d's own BLRgba64->BLRgba conversion rather
        // than re-deriving the word->float math here.
        explicit Color(uint64_t packed, bool hasAlpha = true) noexcept {
            BLRgba rgba(BLRgba64(hasAlpha ? packed : (packed | 0xFFFF000000000000ull)));
            r = rgba.r;
            g = rgba.g;
            b = rgba.b;
            a = rgba.a;
        }

        explicit Color(const std::string& colorStr) noexcept {
			Color::fromName(colorStr, *this);
        }

        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;

        // Standard HSL->RGB conversion. h wraps to [0, 360) automatically
        // (negative or >= 360 values are fine); s and l are clamped to
        // [0, 1] implicitly by the math below.
        //
        // Algorithm: https://en.wikipedia.org/wiki/HSL_and_HSV#HSL_to_RGB_alternative
        // (equivalent to the CSS Color spec's hsl-to-rgb:
        // https://www.w3.org/TR/css-color-3/#hsl-color)
        static Color fromHSL(float h, float s, float l, float a = 1.0f) noexcept {
            float c = (1.0f - std::fabs(2.0f * l - 1.0f)) * s;
            float m = l - c * 0.5f;
            return fromHueChroma(h, c, m, a);
        }

        static Color fromHSL(const HSLColor& hsl) noexcept {
            return fromHSL(hsl.h, hsl.s, hsl.l, hsl.a);
        }

        // Standard RGB->HSL conversion (inverse of fromHSL()).
        //
        // Algorithm: https://en.wikipedia.org/wiki/HSL_and_HSV#From_RGB
        HSLColor toHSL() const noexcept {
            float maxC = r > g ? (r > b ? r : b) : (g > b ? g : b);
            float minC = r < g ? (r < b ? r : b) : (g < b ? g : b);
            float delta = maxC - minC;

            HSLColor hsl;
            hsl.l = (maxC + minC) * 0.5f;
            hsl.a = a;
            hsl.h = hueFromRGB(r, g, b, maxC, delta);

            if (delta > kEpsilon) {
                hsl.s = delta / (1.0f - std::fabs(2.0f * hsl.l - 1.0f));
            }

            return hsl;
        }

        // Standard HSV->RGB conversion. Same hue handling as fromHSL(); s
        // and v are clamped to [0, 1] implicitly by the math below.
        //
        // Algorithm: https://en.wikipedia.org/wiki/HSL_and_HSV#HSV_to_RGB_alternative
        static Color fromHSV(float h, float s, float v, float a = 1.0f) noexcept {
            float c = v * s;
            float m = v - c;
            return fromHueChroma(h, c, m, a);
        }

        static Color fromHSV(const HSVColor& hsv) noexcept {
            return fromHSV(hsv.h, hsv.s, hsv.v, hsv.a);
        }

        // Standard RGB->HSV conversion (inverse of fromHSV()).
        //
        // Algorithm: https://en.wikipedia.org/wiki/HSL_and_HSV#From_RGB
        HSVColor toHSV() const noexcept {
            float maxC = r > g ? (r > b ? r : b) : (g > b ? g : b);
            float minC = r < g ? (r < b ? r : b) : (g < b ? g : b);
            float delta = maxC - minC;

            HSVColor hsv;
            hsv.v = maxC;
            hsv.a = a;
            hsv.h = hueFromRGB(r, g, b, maxC, delta);
            hsv.s = (maxC > 0.0f) ? (delta / maxC) : 0.0f;

            return hsv;
        }

        // CIE L*a*b* (D65) -> RGB, via CIE XYZ. Values outside the sRGB
        // gamut round-trip through r/g/b components outside [0, 1] rather
        // than being clamped, same as the rest of this class.
        //
        // Algorithm: https://en.wikipedia.org/wiki/CIELAB_color_space#Converting_between_CIELAB_and_CIEXYZ_coordinates
        // RGB<->XYZ matrix: http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
        static Color fromLab(float l, float a, float b, float alpha = 1.0f) noexcept {
            float fy = (l + kLabLOffset) / kLabLScale;
            float fx = fy + a / kLabAScale;
            float fz = fy - b / kLabBScale;

            float x = kD65WhiteX * labFInverse(fx);
            float y = kD65WhiteY * labFInverse(fy);
            float z = kD65WhiteZ * labFInverse(fz);

            float rl, gl, bl;
            xyzToLinearRgb(x, y, z, rl, gl, bl);

            return Color(linearToSrgb(rl), linearToSrgb(gl), linearToSrgb(bl), alpha);
        }

        static Color fromLab(const LabColor& lab) noexcept {
            return fromLab(lab.l, lab.a, lab.b, lab.alpha);
        }

        // RGB->CIE L*a*b* (D65) conversion (inverse of fromLab()).
        LabColor toLab() const noexcept {
            float x, y, z;
            linearRgbToXyz(srgbToLinear(r), srgbToLinear(g), srgbToLinear(b), x, y, z);

            float fx = labF(x / kD65WhiteX);
            float fy = labF(y / kD65WhiteY);
            float fz = labF(z / kD65WhiteZ);

            LabColor lab;
            lab.l = kLabLScale * fy - kLabLOffset;
            lab.a = kLabAScale * (fx - fy);
            lab.b = kLabBScale * (fy - fz);
            lab.alpha = a;

            return lab;
        }

        // Subtractive CMY->RGB conversion: each channel is just the
        // additive complement of its subtractive counterpart.
        static Color fromCMY(float c, float m, float y, float a = 1.0f) noexcept {
            return Color(1.0f - c, 1.0f - m, 1.0f - y, a);
        }

        static Color fromCMY(const CMYColor& cmy) noexcept {
            return fromCMY(cmy.c, cmy.m, cmy.y, cmy.a);
        }

        // RGB->CMY conversion (inverse of fromCMY()).
        CMYColor toCMY() const noexcept {
            return CMYColor{1.0f - r, 1.0f - g, 1.0f - b, a};
        }

        // CMYK->RGB conversion, via CMY undercolor addition: k is added
        // back into each CMY channel before the CMY->RGB complement.
        static Color fromCMYK(float c, float m, float y, float k, float a = 1.0f) noexcept {
            return fromCMY(c * (1.0f - k) + k, m * (1.0f - k) + k, y * (1.0f - k) + k, a);
        }

        static Color fromCMYK(const CMYKColor& cmyk) noexcept {
            return fromCMYK(cmyk.c, cmyk.m, cmyk.y, cmyk.k, cmyk.a);
        }

        // RGB->CMYK conversion (inverse of fromCMYK()), via undercolor
        // removal: k is the shared minimum pulled out of c/m/y so each
        // ends up representing only its channel's ink on top of the key.
        CMYKColor toCMYK() const noexcept {
            CMYColor cmy = toCMY();
            float k = cmy.c < cmy.m ? (cmy.c < cmy.y ? cmy.c : cmy.y) : (cmy.m < cmy.y ? cmy.m : cmy.y);

            if (k >= 1.0f - kEpsilon) {
                return CMYKColor{0.0f, 0.0f, 0.0f, 1.0f, a};
            }

            return CMYKColor{
                (cmy.c - k) / (1.0f - k),
                (cmy.m - k) / (1.0f - k),
                (cmy.y - k) / (1.0f - k),
                k,
                a
            };
        }

        // Y'UV (ITU-R BT.709) -> RGB. Operates directly on gamma-encoded
        // r/g/b (broadcast convention), unlike luminosity()'s linearized
        // weighting, even though both use the same channel weights.
        //
        // https://en.wikipedia.org/wiki/Y%E2%80%B2UV#Converting_between_Y%E2%80%B2UV_and_RGB
        static Color fromYUV(float y, float u, float v, float a = 1.0f) noexcept {
            float r = y + v / kYuvVScale;
            float b = y + u / kYuvUScale;
            float g = (y - kLuminanceWeightRed * r - kLuminanceWeightBlue * b) / kLuminanceWeightGreen;
            return Color(r, g, b, a);
        }

        static Color fromYUV(const YUVColor& yuv) noexcept {
            return fromYUV(yuv.y, yuv.u, yuv.v, yuv.a);
        }

        // RGB->Y'UV (ITU-R BT.709) conversion (inverse of fromYUV()).
        YUVColor toYUV() const noexcept {
            YUVColor yuv;
            yuv.y = kLuminanceWeightRed * r + kLuminanceWeightGreen * g + kLuminanceWeightBlue * b;
            yuv.u = kYuvUScale * (b - yuv.y);
            yuv.v = kYuvVScale * (r - yuv.y);
            yuv.a = a;
            return yuv;
        }

        // Relative luminance: how bright this color appears independent of
        // hue, from 0.0 (black) to 1.0 (white). r/g/b are treated as sRGB
        // gamma-encoded (the usual assumption for UI colors) and linearized
        // before weighting, per the WCAG definition; that linearization is
        // why this isn't the same value as toHSL().l. Useful for e.g.
        // picking a readable foreground color against this one as a
        // background, or for contrast-ratio calculations.
        //
        // https://www.w3.org/TR/WCAG21/#dfn-relative-luminance
        float luminosity() const noexcept {
            return kLuminanceWeightRed * srgbToLinear(r)
                + kLuminanceWeightGreen * srgbToLinear(g)
                + kLuminanceWeightBlue * srgbToLinear(b);
        }

        // Returns a color with the same hue/chroma but luminosity() scaled
        // to (approximately) target: since luminance is a linear function
        // of linear-light channel values, uniformly scaling r/g/b in
        // linear space scales luminosity() by the same factor. If *this is
        // black (no chroma to preserve), returns a flat gray at that
        // luminosity instead. The result is clamped to valid [0.0, 1.0]
        // channels, so a very saturated color or an extreme target may
        // clip and land close to, rather than exactly on, target - the
        // same gamut limit any hue-preserving lightness change runs into.
        Color withLuminosity(float target) const noexcept {
            float lr = srgbToLinear(r);
            float lg = srgbToLinear(g);
            float lb = srgbToLinear(b);

            float current = kLuminanceWeightRed * lr + kLuminanceWeightGreen * lg + kLuminanceWeightBlue * lb;

            if (current <= kEpsilon) {
                float gray = linearToSrgb(clamp01(target));
                return Color(gray, gray, gray, a);
            }

            float scale = target / current;
            return Color(
                linearToSrgb(clamp01(lr * scale)),
                linearToSrgb(clamp01(lg * scale)),
                linearToSrgb(clamp01(lb * scale)),
                a);
        }

        // Converts to grayscale: r=g=b=luminosity(), alpha unchanged. Goes
        // through luminosity()'s linearized weighting and re-encodes back
        // to sRGB gamma, rather than just splatting luminosity() straight
        // into r/g/b - that value is linear-light, so using it directly
        // would produce a gray that looks too dark once treated as
        // gamma-encoded (as every other Color in this class is).
        Color grayscale() const noexcept {
            float gray = linearToSrgb(luminosity());
            return Color(gray, gray, gray, a);
        }

        // sRGB gamma-encode <-> linear-light conversion (the same transfer
        // function luminosity() uses internally). *this is assumed to be
        // gamma-encoded (the usual assumption for UI colors); toLinear()
        // returns the linear-light equivalent, needed for physically
        // correct blending/interpolation (naively blending gamma-encoded
        // colors darkens the midpoint). toGamma() is the inverse - it
        // assumes *this holds linear values and re-encodes them. Alpha
        // passes through unchanged in both directions.
        //
        // https://en.wikipedia.org/wiki/SRGB#Transfer_function_(%22gamma%22)
        Color toLinear() const noexcept {
            return Color(srgbToLinear(r), srgbToLinear(g), srgbToLinear(b), a);
        }

        Color toGamma() const noexcept {
            return Color(linearToSrgb(r), linearToSrgb(g), linearToSrgb(b), a);
        }

        // Generic power-law gamma adjustment: raises each channel to the
        // given exponent (channel^gamma), alpha unchanged. Not tied to the
        // sRGB curve above - useful for ad hoc brightness/contrast-style
        // adjustment with a caller-chosen exponent. gamma > 1 darkens
        // midtones, gamma < 1 brightens them.
        Color withGamma(float gamma) const noexcept {
            return Color(std::pow(r, gamma), std::pow(g, gamma), std::pow(b, gamma), a);
        }

        // Multiplies each channel by amount, alpha unchanged: 1.0 is no
        // change, 0.0 is black, > 1.0 brighter. Same definition as the CSS
        // brightness() filter function.
        //
        // https://www.w3.org/TR/filter-effects-1/#funcdef-filter-brightness
        Color withBrightness(float amount) const noexcept {
            return Color(r * amount, g * amount, b * amount, a);
        }

        // Scales each channel around the 0.5 midpoint, alpha unchanged:
        // 1.0 is no change, 0.0 is flat mid-gray, > 1.0 more contrast. Same
        // definition as the CSS contrast() filter function.
        //
        // https://www.w3.org/TR/filter-effects-1/#funcdef-filter-contrast
        Color withContrast(float amount) const noexcept {
            return Color(
                (r - 0.5f) * amount + 0.5f,
                (g - 0.5f) * amount + 0.5f,
                (b - 0.5f) * amount + 0.5f,
                a);
        }

        // The color-wheel complement: same saturation/lightness, hue
        // rotated 180 degrees to the opposite side of the HSL wheel.
        //
        // https://en.wikipedia.org/wiki/Complementary_colors
        Color complement() const noexcept {
            HSLColor hsl = toHSL();
            hsl.h = std::fmod(hsl.h + kHueDegreesFull * 0.5f, kHueDegreesFull);
            return fromHSL(hsl);
        }

        // The photographic negative: 1-r, 1-g, 1-b, alpha unchanged.
        // Distinct from complement(): invert() is the literal per-channel
        // inversion (the "camera negative" look), while complement()
        // rotates hue 180 degrees in HSL space, keeping saturation and
        // lightness.
        Color invert() const noexcept {
            return Color(1.0f - r, 1.0f - g, 1.0f - b, a);
        }

        // Linearly interpolates each channel (including alpha) directly
        // between *this (t=0) and to (t=1) - the same direct-on-gamma-
        // values interpolation CSS uses by default for color transitions.
        // t isn't clamped: t<0 or t>1 extrapolates past either endpoint,
        // consistent with this class's general no-clamping convention
        // (clamping happens where values leave float space, e.g.
        // toPackedBytes()/toBLRgba32()).
        Color interpolate(const Color& to, float t) const noexcept {
            return Color(
                r + (to.r - r) * t,
                g + (to.g - g) * t,
                b + (to.b - b) * t,
                a + (to.a - a) * t);
        }

        // Inverse of toPackedBytes(): reads bytes per the given channel
        // list (same MSB-first layout toPackedBytes() writes) and fills in
        // outColor, normalizing each channel from its bit depth back to
        // [0.0, 1.0]. Channels not present in the list are left at
        // outColor's existing value - notably, if the list has no Alpha
        // entry (e.g. fromRGB24()), outColor.a is untouched.
        //
        // Returns false and leaves outColor untouched if bytesSize is too
        // small for the given channels (same failure mode toPackedBytes()
        // guards against, just on the read side); true otherwise. The sum
        // of all channel bits must not exceed 64.
        static bool fromPackedBytes(const PackedChannel* channels, size_t channelCount, const uint8_t* bytes, size_t bytesSize, Color& outColor) noexcept {
            size_t totalBits = 0;
            for (size_t i = 0; i < channelCount; ++i) {
                totalBits += channels[i].bits;
            }

            size_t byteCount = (totalBits + 7) / 8;
            if (byteCount > bytesSize) {
                return false;
            }

            uint64_t packed = 0;
            for (size_t i = 0; i < byteCount; ++i) {
                packed = (packed << 8) | bytes[i];
            }
            packed >>= (byteCount * 8 - totalBits);  // drop toPackedBytes()'s left-align padding

            size_t bitsRemaining = totalBits;
            for (size_t i = 0; i < channelCount; ++i) {
                uint32_t maxValue = (1u << channels[i].bits) - 1u;
                bitsRemaining -= channels[i].bits;

                uint32_t value = uint32_t(packed >> bitsRemaining) & maxValue;
                outColor.setComponent(channels[i].channel, float(value) / float(maxValue));
            }

            return true;
        }

        // 0x00RRGGBB (top byte ignored, alpha forced fully opaque). Same
        // layout as the Color(uint32_t, bool) constructor with
        // hasAlpha=false - this is just a more discoverable name for that
        // same conversion when the value came from 24-bit-RGB data.
        static Color fromRGB24(uint32_t rgb) noexcept {
            return Color(rgb, false);
        }

        // 0xAARRGGBB, matching blend2d's BLRgba32 packing - also the byte
        // order of 4 sequential bytes B,G,R,A in memory on a little-endian
        // machine, hence the name. Same as the Color(uint32_t) constructor;
        // a more discoverable name for that conversion when the value came
        // from BGRA pixel data.
        static Color fromBGRA32(uint32_t bgra) noexcept {
            return Color(bgra);
        }

        // Reads one of the current Windows UI theme's standard colors via
        // GetSysColor(), after translating color from our own SystemColor
        // numbering to the Win32 COLOR_* constant it corresponds to (see
        // toWin32SysColorIndex()). Always fully opaque - GetSysColor()'s
        // COLORREF result carries no alpha, so a is forced to 1.0.
        static Color fromSystemColor(SystemColor color) noexcept {
            COLORREF ref = ::GetSysColor(toWin32SysColorIndex(color));
            return Color(
                float(GetRValue(ref)) / 255.0f,
                float(GetGValue(ref)) / 255.0f,
                float(GetBValue(ref)) / 255.0f,
                1.0f);
        }

        // Case-insensitive CSS named-color lookup (the 147 CSS Color
        // Module Level 4 keywords, plus "transparent"), binary-searched
        // against a sorted table. Returns false and leaves outColor
        // untouched if name isn't a recognized keyword; true otherwise.
        // fromString() falls back to this when its argument isn't valid
        // hex, so fromString("cornflowerblue") also works.
        //
        // https://www.w3.org/TR/css-color-4/#named-colors
        static bool fromName(const std::string& name, Color& outColor) noexcept {
            size_t lo = 0;
            size_t hi = kNamedColorCount;

            while (lo < hi) {
                size_t mid = lo + (hi - lo) / 2;
                int cmp = compareNameCaseInsensitive(name, kNamedColors[mid].name);

                if (cmp == 0) {
                    outColor = Color(kNamedColors[mid].value);
                    return true;
                } else if (cmp < 0) {
                    hi = mid;
                } else {
                    lo = mid + 1;
                }
            }

            return false;
        }

        static Color fromName(const std::string& name) noexcept {
            Color result;
            Color::fromName(name, result);

            return result;
        }

        // Parses a CSS-style hex color: "#RGB", "#RGBA", "#RRGGBB", or
        // "#RRGGBBAA" (the leading '#' is optional). 3/4-digit shorthand
        // digits are doubled (CSS convention: "#0f3" means the same as
        // "#00ff33"). Alpha defaults to fully opaque for the 3/6-digit
        // forms that don't specify one. If str isn't valid hex, falls
        // back to fromName() (so e.g. "red" and "cornflowerblue" work
        // here too).
        //
        // Returns false and leaves outColor untouched if str is neither
        // valid hex nor a recognized color name; true otherwise. Pairs
        // with toString().
        //
        // https://www.w3.org/TR/css-color-4/#hex-notation
        static bool fromString(const std::string& str, Color& outColor) noexcept {
            if (fromHex(str, outColor)) {
                return true;
            }
            return fromName(str, outColor);
        }

        // Packs r/g/b/a into out per the given channel list: each channel
        // is independently quantized (clamped, then rounded) to its own
        // bit depth and written MSB-first, in the order the channels are
        // listed - so {{Red,8},{Green,8},{Blue,8}} packs as out[0]=R,
        // out[1]=G, out[2]=B, while {{Blue,8},{Green,8},{Red,8},{Alpha,8}}
        // packs as out[0]=B, out[1]=G, out[2]=R, out[3]=A. Also handles
        // non-byte-aligned formats, e.g. {{Red,5},{Green,6},{Blue,5}} packs
        // RGB565 into 2 bytes, big-endian (out[0] holds the top 8 bits).
        //
        // Always returns the number of bytes the given channels need,
        // regardless of outSize - same convention as snprintf(). If
        // outSize is too small, out is left untouched (never written past
        // outSize) and the caller can compare the return value against
        // outSize to detect that and retry with a bigger buffer. The sum
        // of all channel bits must not exceed 64.
        size_t toPackedBytes(const PackedChannel* channels, size_t channelCount, uint8_t* out, size_t outSize) const noexcept {
            uint64_t packed = 0;
            size_t totalBits = 0;

            for (size_t i = 0; i < channelCount; ++i) {
                float value = componentFor(channels[i].channel);
                uint32_t maxValue = (1u << channels[i].bits) - 1u;
                uint32_t quantized = uint32_t(std::round(clamp01(value) * float(maxValue)));

                packed = (packed << channels[i].bits) | quantized;
                totalBits += channels[i].bits;
            }

            size_t byteCount = (totalBits + 7) / 8;
            if (byteCount > outSize) {
                return byteCount;
            }

            packed <<= (byteCount * 8 - totalBits);  // left-align into whole bytes

            for (size_t i = 0; i < byteCount; ++i) {
                out[i] = uint8_t(packed >> ((byteCount - 1 - i) * 8));
            }

            return byteCount;
        }

        // Convenience wrapper for toPackedBytes(): 3 bytes, 8 bits each,
        // out[0]=R, out[1]=G, out[2]=B.
        void toRGB24(uint8_t out[3]) const noexcept {
            PackedChannel channels[] = {
                {ColorChannel::Red, 8}, {ColorChannel::Green, 8}, {ColorChannel::Blue, 8}
            };
            toPackedBytes(channels, 3, out, 3);
        }

        // Convenience wrapper for toPackedBytes(): 4 bytes, 8 bits each,
        // out[0]=B, out[1]=G, out[2]=R, out[3]=A.
        void toBGRA32(uint8_t out[4]) const noexcept {
            PackedChannel channels[] = {
                {ColorChannel::Blue, 8}, {ColorChannel::Green, 8}, {ColorChannel::Red, 8}, {ColorChannel::Alpha, 8}
            };
            toPackedBytes(channels, 4, out, 4);
        }

        // Formats as CSS-style hex: "#RRGGBBAA" (8 hex digits, lowercase,
        // always including alpha for a single predictable, round-trippable
        // format). Pairs with the more lenient fromString().
        //
        // https://www.w3.org/TR/css-color-4/#hex-notation
        std::string toString() const {
            uint8_t bytes[4];
            PackedChannel channels[] = {
                {ColorChannel::Red, 8}, {ColorChannel::Green, 8}, {ColorChannel::Blue, 8}, {ColorChannel::Alpha, 8}
            };
            toPackedBytes(channels, 4, bytes, 4);

            static const char kHexDigits[] = "0123456789abcdef";
            char out[10];
            out[0] = '#';
            for (int i = 0; i < 4; ++i) {
                out[1 + i * 2] = kHexDigits[bytes[i] >> 4];
                out[2 + i * 2] = kHexDigits[bytes[i] & 0xF];
            }
            out[9] = '\0';
            return std::string(out, 9);
        }

        BLRgba toBLRgba() const noexcept {
            return BLRgba(r, g, b, a);
        }

        BLRgba32 toBLRgba32() const noexcept {
            return toBLRgba().to_rgba32();
        }

        
        operator BLRgba() const noexcept {
            return toBLRgba();
        }        

        operator BLRgba32() const noexcept {
            return toBLRgba32();
        }

        operator std::string() const noexcept {
            return toString();
        }

        
        Color& operator =(const std::string& rhs) noexcept {
			Color::fromString(rhs, *this);
			return *this;
        }
        

        bool operator==(const Color& other) const noexcept {
            return r == other.r && g == other.g && b == other.b && a == other.a;
        }

        bool operator!=(const Color& other) const noexcept {
            return !(*this == other);
        }

    private:
        // Translates our own SystemColor numbering (see color_constants.h)
        // to the Win32 COLOR_* constant it corresponds to, for
        // fromSystemColor() to pass to GetSysColor(). Kept as a switch
        // (rather than baking Windows' own numbering into SystemColor) so
        // color_constants.h doesn't need windows.h just to declare the enum.
        static int toWin32SysColorIndex(SystemColor color) noexcept {
            switch (color) {
                case SystemColor::ScrollBar: return COLOR_SCROLLBAR;
                case SystemColor::Desktop: return COLOR_DESKTOP;
                case SystemColor::ActiveCaption: return COLOR_ACTIVECAPTION;
                case SystemColor::InactiveCaption: return COLOR_INACTIVECAPTION;
                case SystemColor::Menu: return COLOR_MENU;
                case SystemColor::Window: return COLOR_WINDOW;
                case SystemColor::WindowFrame: return COLOR_WINDOWFRAME;
                case SystemColor::MenuText: return COLOR_MENUTEXT;
                case SystemColor::WindowText: return COLOR_WINDOWTEXT;
                case SystemColor::CaptionText: return COLOR_CAPTIONTEXT;
                case SystemColor::ActiveBorder: return COLOR_ACTIVEBORDER;
                case SystemColor::InactiveBorder: return COLOR_INACTIVEBORDER;
                case SystemColor::AppWorkspace: return COLOR_APPWORKSPACE;
                case SystemColor::Highlight: return COLOR_HIGHLIGHT;
                case SystemColor::HighlightText: return COLOR_HIGHLIGHTTEXT;
                case SystemColor::ButtonFace: return COLOR_BTNFACE;
                case SystemColor::ButtonShadow: return COLOR_BTNSHADOW;
                case SystemColor::GrayText: return COLOR_GRAYTEXT;
                case SystemColor::ButtonText: return COLOR_BTNTEXT;
                case SystemColor::InactiveCaptionText: return COLOR_INACTIVECAPTIONTEXT;
                case SystemColor::ButtonHighlight: return COLOR_BTNHIGHLIGHT;
                case SystemColor::DarkShadow3D: return COLOR_3DDKSHADOW;
                case SystemColor::Light3D: return COLOR_3DLIGHT;
                case SystemColor::InfoText: return COLOR_INFOTEXT;
                case SystemColor::InfoBackground: return COLOR_INFOBK;
                case SystemColor::Hotlight: return COLOR_HOTLIGHT;
                case SystemColor::GradientActiveCaption: return COLOR_GRADIENTACTIVECAPTION;
                case SystemColor::GradientInactiveCaption: return COLOR_GRADIENTINACTIVECAPTION;
                case SystemColor::MenuHighlight: return COLOR_MENUHILIGHT;
                case SystemColor::MenuBar: return COLOR_MENUBAR;
            }
            return COLOR_WINDOW;  // unreachable for a valid SystemColor
        }

        static float clamp01(float v) noexcept {
            return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        }

        float componentFor(ColorChannel channel) const noexcept {
            switch (channel) {
                case ColorChannel::Red: return r;
                case ColorChannel::Green: return g;
                case ColorChannel::Blue: return b;
                case ColorChannel::Alpha: return a;
            }
            return 0.0f;
        }

        void setComponent(ColorChannel channel, float value) noexcept {
            switch (channel) {
                case ColorChannel::Red: r = value; break;
                case ColorChannel::Green: g = value; break;
                case ColorChannel::Blue: b = value; break;
                case ColorChannel::Alpha: a = value; break;
            }
        }

        // The hex-only half of fromString() - see that function's doc
        // comment for the accepted formats.
        static bool fromHex(const std::string& str, Color& outColor) noexcept {
            size_t start = (!str.empty() && str[0] == '#') ? 1 : 0;

            int digits[8];
            size_t digitCount = 0;
            for (size_t i = start; i < str.size(); ++i) {
                if (digitCount >= 8) {
                    return false;
                }

                char c = str[i];
                if (c >= '0' && c <= '9') {
                    digits[digitCount++] = c - '0';
                } else if (c >= 'a' && c <= 'f') {
                    digits[digitCount++] = c - 'a' + 10;
                } else if (c >= 'A' && c <= 'F') {
                    digits[digitCount++] = c - 'A' + 10;
                } else {
                    return false;
                }
            }

            uint8_t bytes[4];
            bool hasAlpha;

            if (digitCount == 3 || digitCount == 4) {
                for (size_t i = 0; i < digitCount; ++i) {
                    bytes[i] = uint8_t(digits[i] * 17);  // nibble N doubled is byte 0xNN = N*17
                }
                hasAlpha = (digitCount == 4);
            } else if (digitCount == 6 || digitCount == 8) {
                for (size_t i = 0; i < digitCount / 2; ++i) {
                    bytes[i] = uint8_t((digits[i * 2] << 4) | digits[i * 2 + 1]);
                }
                hasAlpha = (digitCount == 8);
            } else {
                return false;
            }

            if (!hasAlpha) {
                bytes[3] = 255;
            }

            PackedChannel channels[] = {
                {ColorChannel::Red, 8}, {ColorChannel::Green, 8}, {ColorChannel::Blue, 8}, {ColorChannel::Alpha, 8}
            };
            return fromPackedBytes(channels, 4, bytes, 4, outColor);
        }

        // Case-insensitive three-way compare of a std::string against a
        // NUL-terminated C string, for binary-searching kNamedColors.
        static int compareNameCaseInsensitive(const std::string& a, const char* b) noexcept {
            size_t i = 0;
            for (; i < a.size() && b[i] != '\0'; ++i) {
                char ca = (a[i] >= 'A' && a[i] <= 'Z') ? char(a[i] - 'A' + 'a') : a[i];
                char cb = (b[i] >= 'A' && b[i] <= 'Z') ? char(b[i] - 'A' + 'a') : b[i];
                if (ca != cb) {
                    return (ca < cb) ? -1 : 1;
                }
            }
            if (i < a.size()) {
                return 1;   // a longer than b
            }
            if (b[i] != '\0') {
                return -1;  // b longer than a
            }
            return 0;
        }

        
        static constexpr size_t kNamedColorCount = sizeof(kNamedColors) / sizeof(kNamedColors[0]);

        // HSL/HSV hue is a position around a 360-degree wheel split into 6
        // 60-degree hextants (RY, YG, GC, CB, BM, MR).
        static constexpr float kHueDegreesFull = 360.0f;
        static constexpr float kHueDegreesPerHextant = 60.0f;
        static constexpr float kHueHextantCount = 6.0f;

        // General "close enough to zero" threshold, used wherever a
        // conversion would otherwise divide by ~0: toHSL()/toHSV() treat a
        // max-min delta below this as fully desaturated (gray) and skip
        // the hue/saturation calculation; toCMYK() treats a CMY minimum
        // above (1 - this) as pure black and skips undercolor removal;
        // withLuminosity() treats a current luminosity below this as black
        // (no chroma to preserve).
        static constexpr float kEpsilon = 1e-6f;

        // WCAG relative luminance weights for linearized sRGB primaries.
        // https://www.w3.org/TR/WCAG21/#dfn-relative-luminance
        static constexpr float kLuminanceWeightRed = 0.2126f;
        static constexpr float kLuminanceWeightGreen = 0.7152f;
        static constexpr float kLuminanceWeightBlue = 0.0722f;

        // Y'UV (BT.709) chroma scale factors: half the reciprocal of
        // (1 - weight) for each of the blue/red weights above, so U/V land
        // in roughly [-0.5, 0.5] for r/g/b in [0, 1].
        static constexpr float kYuvUScale = 0.5f / (1.0f - kLuminanceWeightBlue);
        static constexpr float kYuvVScale = 0.5f / (1.0f - kLuminanceWeightRed);

        // Shared by fromHSL()/fromHSV(): given a hue and the chroma/min
        // ("c"/"m" in the HSL and HSV formulas - both spaces reduce to the
        // same hextant selection once you have those two), produces the
        // resulting RGB color.
        static Color fromHueChroma(float h, float c, float m, float a) noexcept {
            h = std::fmod(h, kHueDegreesFull);
            if (h < 0.0f) {
                h += kHueDegreesFull;
            }

            float hPrime = h / kHueDegreesPerHextant;
            float x = c * (1.0f - std::fabs(std::fmod(hPrime, 2.0f) - 1.0f));

            float r1, g1, b1;
            if (hPrime < 1.0f) {
                r1 = c; g1 = x; b1 = 0.0f;
            } else if (hPrime < 2.0f) {
                r1 = x; g1 = c; b1 = 0.0f;
            } else if (hPrime < 3.0f) {
                r1 = 0.0f; g1 = c; b1 = x;
            } else if (hPrime < 4.0f) {
                r1 = 0.0f; g1 = x; b1 = c;
            } else if (hPrime < 5.0f) {
                r1 = x; g1 = 0.0f; b1 = c;
            } else {
                r1 = c; g1 = 0.0f; b1 = x;
            }

            return Color(r1 + m, g1 + m, b1 + m, a);
        }

        // Shared by toHSL()/toHSV(): hue extraction is identical in both
        // color spaces - only saturation and lightness-or-value differ.
        // Returns 0 for gray (delta below kEpsilon).
        static float hueFromRGB(float r, float g, float b, float maxC, float delta) noexcept {
            if (delta <= kEpsilon) {
                return 0.0f;
            }

            float h;
            if (maxC == r) {
                h = std::fmod((g - b) / delta, kHueHextantCount);
            } else if (maxC == g) {
                h = ((b - r) / delta) + 2.0f;
            } else {
                h = ((r - g) / delta) + 4.0f;
            }

            h *= kHueDegreesPerHextant;
            if (h < 0.0f) {
                h += kHueDegreesFull;
            }

            return h;
        }

        // D65 reference white (CIE XYZ), used by the Lab<->XYZ conversion.
        static constexpr float kD65WhiteX = 0.95047f;
        static constexpr float kD65WhiteY = 1.0f;
        static constexpr float kD65WhiteZ = 1.08883f;

        // CIE L*a*b* nonlinearity constants.
        // https://en.wikipedia.org/wiki/CIELAB_color_space#Converting_between_CIELAB_and_CIEXYZ_coordinates
        static constexpr float kLabDelta = 6.0f / 29.0f;
        static constexpr float kLabFOffset = 4.0f / 29.0f;
        static constexpr float kLabLScale = 116.0f;
        static constexpr float kLabLOffset = 16.0f;
        static constexpr float kLabAScale = 500.0f;
        static constexpr float kLabBScale = 200.0f;

        static float labF(float t) noexcept {
            return (t > kLabDelta * kLabDelta * kLabDelta)
                ? std::cbrt(t)
                : (t / (3.0f * kLabDelta * kLabDelta) + kLabFOffset);
        }

        static float labFInverse(float t) noexcept {
            return (t > kLabDelta)
                ? (t * t * t)
                : (3.0f * kLabDelta * kLabDelta * (t - kLabFOffset));
        }

        // Linear sRGB (D65) <-> CIE 1931 XYZ. Matrix from the sRGB working
        // space definition (D65 white point).
        // http://www.brucelindbloom.com/index.html?Eqn_RGB_XYZ_Matrix.html
        static void linearRgbToXyz(float r, float g, float b, float& x, float& y, float& z) noexcept {
            x = 0.4124564f * r + 0.3575761f * g + 0.1804375f * b;
            y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
            z = 0.0193339f * r + 0.1191920f * g + 0.9503041f * b;
        }

        static void xyzToLinearRgb(float x, float y, float z, float& r, float& g, float& b) noexcept {
            r =  3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
            g = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
            b =  0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
        }

        // sRGB piecewise transfer function constants (IEC 61966-2-1).
        // https://en.wikipedia.org/wiki/SRGB#Transfer_function_(%22gamma%22)
        static constexpr float kSrgbLinearThreshold = 0.04045f;  // gamma-encoded value below which the curve is linear
        static constexpr float kSrgbEncodeThreshold = 0.0031308f;  // linear value below which the inverse curve is linear
        static constexpr float kSrgbLinearSlope = 12.92f;  // slope of the linear segment
        static constexpr float kSrgbGammaOffset = 0.055f;  // additive offset in the power segment
        static constexpr float kSrgbGammaScale = 1.055f;  // multiplicative scale in the power segment
        static constexpr float kSrgbGammaExponent = 2.4f;  // exponent of the power segment

        static float srgbToLinear(float c) noexcept {
            return (c <= kSrgbLinearThreshold)
                ? (c / kSrgbLinearSlope)
                : std::pow((c + kSrgbGammaOffset) / kSrgbGammaScale, kSrgbGammaExponent);
        }

        static float linearToSrgb(float c) noexcept {
            return (c <= kSrgbEncodeThreshold)
                ? (c * kSrgbLinearSlope)
                : (kSrgbGammaScale * std::pow(c, 1.0f / kSrgbGammaExponent) - kSrgbGammaOffset);
        }
    };

}

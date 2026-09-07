#pragma once

#include <newui/newui.h>
#include <newui/geometry.h>
#include <newui/color.h>

#include <blend2d/blend2d.h>

#include <string>
#include <vector>

namespace newui::gfx {

    // Wraps a BLImage backed by a real Win32 DIB section, so the exact
    // same pixel buffer blend2d reads/writes via blImage() can also be
    // drawn into (or read out of) via classic GDI, through a memory
    // device context - memDC(). Exists so code that has to call a
    // GDI-only API (DrawThemeBackground(), DrawIconEx(), ...) against a
    // blend2d-managed buffer doesn't have to re-derive the
    // CreateDIBSection()/CreateCompatibleDC()/SelectObject() dance from
    // scratch each time - see ThemedViewStyle::paint() (viewstyle.cpp,
    // its own hand-rolled BeginBufferedPaint()-based version of this) and
    // RootView::resizeImageBuffer() (rootview.cpp, the externally-owned-
    // buffer/create_from_data() pattern Image reuses) for the two
    // existing, separately-hand-rolled instances of a similar problem in
    // this codebase.
    //
    // Always a 32bpp top-down premultiplied-BGRA buffer (BL_FORMAT_PRGB32) -
    // byte-for-byte what both a 32bpp DIB section and blend2d's alpha-aware
    // format agree on (same fact ThemedViewStyle::paint() and cursor.cpp's
    // createCursorFromImage() already rely on), so no format parameter to
    // get wrong.
    //
    // Non-copyable (owns real GDI handles) - move-only.
    class Image {
    public:
        Image() = default;

        // Blank width x height canvas, fully transparent - CreateDIBSection()'s
        // returned bits aren't guaranteed zeroed, so this explicitly clears them.
        Image(int width, int height);

        // Decodes path (PNG/BMP/JPEG/QOI - whatever the file actually is,
        // via BLImage::read_from_file(), same codecs Bundle::loadImage()
        // already uses) into a fresh DIB-backed buffer, converting to
        // BL_FORMAT_PRGB32 first if the decoded image isn't already that
        // format. isValid() is false afterward if the file couldn't be
        // read/decoded.
        //
        // A ".svg" path instead rasterizes via renderSvgFile() (svgimage.h)
        // at a fixed kDefaultSvgRasterSize x kDefaultSvgRasterSize default -
        // svgandme (the vendored parser/renderer) has no reliable, canvas-
        // independent way to ask an SVG for its own "natural" size, so this
        // doesn't attempt to guess one.
        // Use the (path, width, height) constructor below for an actual
        // target size - the common case for a toolbar/button icon, since
        // those are almost always requested at a specific pixel size
        // anyway.
        explicit Image(const std::string& path);

        // Same decode as above, but rasterized/resized to an explicit
        // width x height instead of whatever size the source naturally
        // is. For a ".svg" path this is the real rasterization target
        // (renderSvgFile() renders directly at width x height - not a
        // post-hoc scale); for any other format, the file is decoded at
        // its native size first and then scaled into width x height via a
        // BLContext blit. isValid() is false afterward on any failure,
        // including a non-positive width or height.
        Image(const std::string& path, int width, int height);

        // Copies source's pixel data into a fresh DIB-backed buffer
        // (converting to BL_FORMAT_PRGB32 first if needed) - source keeps
        // whatever backing memory it already had; this Image gets its
        // own independent, GDI-selectable copy. isValid() is false
        // afterward if source is empty.
        explicit Image(const BLImage& source);

        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        Image(Image&& other) noexcept;
        Image& operator=(Image&& other) noexcept;

        bool isValid() const {
            return !image_.is_empty();
        }

        int width() const {
            return image_.size().w;
        }

        int height() const {
            return image_.size().h;
        }

        // Live, read/write access to the pixel data via blend2d - e.g.
        // BLContext ctx(image.blImage()); ...draws into the exact same
        // buffer memDC() GDI calls read/write too.
        BLImage& blImage() {
            return image_;
        }

        const BLImage& blImage() const {
            return image_;
        }

        // A memory DC selected with the DIB section backing blImage() -
        // any GDI call into this HDC writes straight into blImage()'s own
        // buffer, no blit/copy needed afterward (or beforehand, to read
        // out whatever GDI already drew). Created lazily on first call -
        // the same HDC is returned every time after that, until this
        // Image is destroyed or moved-from. Returns nullptr if isValid()
        // is false.
        HDC memDC();

    private:
        // Allocates a fresh width x height DIB section and wraps its bits
        // directly as image_ (via BLImage::create_from_data(), no
        // destroy_func - the DIB section, not blend2d, owns this memory;
        // releaseGdiResources() is what frees it). If copySrc is
        // non-null, copies copySrcStride-strided pixel rows in from it
        // (assumed already BL_FORMAT_PRGB32/BGRA); otherwise zero-fills
        // (a blank, fully transparent canvas). Returns false (this Image
        // left invalid) on any failure.
        bool createDibBackedImage(int width, int height, const void* copySrc = nullptr, intptr_t copySrcStride = 0);

        void releaseGdiResources();

        BLImage image_;
        HBITMAP dibSection_ = nullptr;
        HDC memDC_ = nullptr;
        HBITMAP oldBitmap_ = nullptr;  // memDC_'s original 1x1 monochrome bitmap - restored before DeleteDC()
    };

    enum class GradientKind {
        Linear,
        Radial,
        Conic,
        Point
    };

    // Mirrors blend2d's own BLExtendMode - same "wrap the raw blend2d enum
    // in a reflectable newui one" convention CompositingFlag/BLCompOp
    // already use (viewstyle.h/toBLCompOp()) - a plain BLExtendMode isn't
    // a registered newui::reflection::Enum, so a Gradient::extendMode()
    // property typed directly as BLExtendMode would silently fail to
    // write (same gap BLVar had) and throw std::bad_any_cast on read
    // (TypedProperty::set() has no has_value() guard around its
    // std::any_cast). Only the 3 "simple" (uniform-both-axes) modes are
    // exposed - blend2d's other 6 values let X/Y extend differently,
    // out of scope for a first pass.
    enum class ExtendMode {
        Pad,
        Repeat,
        Reflect
    };

    inline BLExtendMode toBLExtendMode(ExtendMode mode) {
        switch (mode) {
            case ExtendMode::Repeat: return BL_EXTEND_MODE_REPEAT;
            case ExtendMode::Reflect: return BL_EXTEND_MODE_REFLECT;
            case ExtendMode::Pad: default: return BL_EXTEND_MODE_PAD;
        }
    }

    inline ExtendMode toExtendMode(BLExtendMode mode) {
        switch (mode) {
            case BL_EXTEND_MODE_REPEAT: return ExtendMode::Repeat;
            case BL_EXTEND_MODE_REFLECT: return ExtendMode::Reflect;
            default: return ExtendMode::Pad;
        }
    }

    // One color stop along a Linear/Radial/Conic gradient's parametric
    // axis - offset in [0,1], analogous to blend2d's own BLGradientStop
    // but using newui::Color.
    class GradientStop {
    public:
        GradientStop() = default;
        GradientStop(float offset, const Color& color) : offset_(offset), color_(color) {}

        float offset() const { return offset_; }
        void setOffset(float value) { offset_ = value; }

        const Color& color() const { return color_; }
        void setColor(const Color& color) { color_ = color; }

    private:
        float offset_ = 0.0f;
        Color color_;
    };

    // One anchor of a GradientKind::Point gradient - see Gradient::
    // rasterizePoints()'s own comment (graphics.cpp) for how multiple of
    // these blend together.
    class GradientPoint {
    public:
        GradientPoint() = default;
        GradientPoint(const Point& position, const Color& color) : position_(position), color_(color) {}

        const Point& position() const { return position_; }
        void setPosition(const Point& p) { position_ = p; }

        const Color& color() const { return color_; }
        void setColor(const Color& color) { color_ = color; }

    private:
        Point position_;
        Color color_;
    };

    // Wraps BLGradient (Linear/Radial/Conic - blend2d natively
    // rasterizes these) plus a fourth kind, Point, blend2d has no
    // equivalent for: scattered 2D anchors, each with its own color,
    // blended by inverse-distance weighting instead of laid out along
    // one axis. Point is baked into a raster Image fresh on every
    // toBLVar() call (see rasterizePoints()'s own cost comment) rather
    // than cached, since points()/stops() are ordinary mutable
    // collections that may be under animation.
    //
    // Every field here is private + getter/setter (rather than a plain
    // public member) so it reflects the same way View's own value types
    // (Rect, Point, ...) already do, and so a future PropertyManager-
    // driven animation has real accessor methods to drive rather than a
    // raw field it has to reach past.
    class Gradient {
    public:
        Gradient() = default;

        GradientKind kind() const { return kind_; }
        void setKind(GradientKind kind) { kind_ = kind; }

        // Linear
        const Point& linearStart() const { return linearStart_; }
        void setLinearStart(const Point& p) { linearStart_ = p; }
        const Point& linearEnd() const { return linearEnd_; }
        void setLinearEnd(const Point& p) { linearEnd_ = p; }

        // Radial
        const Point& radialCenter() const { return radialCenter_; }
        void setRadialCenter(const Point& p) { radialCenter_ = p; }
        // Offset from radialCenter() - (0,0) means the focal point sits
        // exactly on the center (no hotspot skew).
        const Point& radialFocalOffset() const { return radialFocalOffset_; }
        void setRadialFocalOffset(const Point& p) { radialFocalOffset_ = p; }
        float radialRadius() const { return radialRadius_; }
        void setRadialRadius(float r) { radialRadius_ = r; }

        // Conic
        const Point& conicCenter() const { return conicCenter_; }
        void setConicCenter(const Point& p) { conicCenter_ = p; }
        float conicAngle() const { return conicAngle_; }  // radians - where the stops' 0.0 offset starts
        void setConicAngle(float radians) { conicAngle_ = radians; }
        float conicRepeat() const { return conicRepeat_; }  // number of full sweeps across [0,1] of stops
        void setConicRepeat(float repeat) { conicRepeat_ = repeat; }

        // Linear/Radial/Conic share one stop list and extend mode. A
        // non-const reference getter (not a plain public member) so this
        // stays consistent with every other field here, but is still
        // addressable enough for reflection.h's collection machinery to
        // treat it as a live, in-place-editable container.
        std::vector<GradientStop>& stops() { return stops_; }
        const std::vector<GradientStop>& stops() const { return stops_; }
        ExtendMode extendMode() const { return extendMode_; }
        void setExtendMode(ExtendMode mode) { extendMode_ = mode; }

        // Point only.
        std::vector<GradientPoint>& points() { return points_; }
        const std::vector<GradientPoint>& points() const { return points_; }
        float pointBlendPower() const { return pointBlendPower_; }  // inverse-distance exponent - higher = sharper transitions between points
        void setPointBlendPower(float power) { pointBlendPower_ = power; }
        int pointRasterMax() const { return pointRasterMax_; }  // longer side of the baked raster, in pixels
        void setPointRasterMax(int maxDimension) { pointRasterMax_ = maxDimension; }

        // Resolves this gradient to something a BLContext's
        // set_fill_style()/set_stroke_style() accepts: a real BLGradient
        // for Linear/Radial/Conic (cheap - copies stops/values into
        // blend2d's own object every call, no caching needed), or a
        // BLPattern wrapping a freshly-baked raster for Point (see
        // rasterizePoints() - not cached, so a Point gradient is real
        // per-paint cost; fine for an occasional design accent, worth
        // revisiting with a dirty-flagged cache if it's ever driven at
        // 60fps). localBounds is the shape's own local-space bounding
        // box - only used by the Point kind, to size/position the baked
        // raster; Linear/Radial/Conic ignore it (their own start/end/
        // center/radius fields already carry absolute local-space
        // coordinates, same as blend2d's own gradient values do).
        BLVar toBLVar(const Rect& localBounds) const;

    private:
        BLGradient toBLGradient() const;

        // A plain BLImage, not a gfx::Image - toBLVar() wraps this
        // straight into a BLPattern and hands it back inside the
        // returned BLVar, which the caller then holds onto (and actually
        // paints with) well after this function has returned. gfx::Image
        // is GDI/DIB-backed - its destructor deletes the DIB section the
        // moment the Image itself is destroyed, so a *local* gfx::Image
        // here would free the pixel buffer out from under the BLPattern
        // before blend2d ever actually rasterizes with it (the fill
        // happens later, when the caller's ctx.fill_path()/stroke_path()
        // runs - not here) - a real, reproduced use-after-free (crashed
        // inside blend2d's own fill_analytic()/fill_func dispatch,
        // rendercommandprocsync_p.h). A plain BLImage is blend2d's own
        // refcounted/COW type instead - copying it into the BLPattern
        // (toBLVar(), graphics.cpp) keeps the real pixel data alive for
        // as long as anything still holds a handle to it, no external
        // GDI object's lifetime to outlive at all.
        BLImage rasterizePoints(const Rect& localBounds) const;

        GradientKind kind_ = GradientKind::Linear;

        Point linearStart_;
        Point linearEnd_{100.0f, 0.0f};

        Point radialCenter_;
        Point radialFocalOffset_;
        float radialRadius_ = 50.0f;

        Point conicCenter_;
        float conicAngle_ = 0.0f;
        float conicRepeat_ = 1.0f;

        std::vector<GradientStop> stops_;
        ExtendMode extendMode_ = ExtendMode::Pad;

        std::vector<GradientPoint> points_;
        float pointBlendPower_ = 2.0f;
        int pointRasterMax_ = 64;
    };

    // What a Fill/Stroke actually paints with - never more than one of
    // these is meaningful at a time, selected by Fill::kind().
    enum class PaintKind {
        None,
        Color,
        Gradient,
        Image
    };

    // Everything a Shape (newui/shapes.h) paints its own fill (or, via
    // Stroke below, its outline) with - a POD-ish stand-in for a live
    // BLVar, which can't itself be reflected (writes silently drop it,
    // reflection.h) or animated (fails Property's IsPodLike check,
    // property.h - BLVar has a user-provided copy ctor/dtor). Resolves to
    // a real BLVar via toBLVar(), called fresh at paint time.
    // No virtual destructor - nothing ever deletes a Stroke through a
    // Fill* (ShapeStyle holds both as plain value members, never as base-
    // class pointers), so this stays a non-polymorphic base. That also
    // means no user-declared destructor needs to exist here at all, which
    // matters: a user-declared destructor (even a defaulted one) would
    // suppress the compiler's implicit move constructor entirely (not
    // just the copy ones - see [class.copy]/7), and with none declared,
    // Fill/Stroke both get real implicit move support "for free" (copy
    // stays implicitly deleted regardless, since imageCache_ below is a
    // move-only Image - see its own class comment).
    class Fill {
    public:
        Fill() = default;

        // The one and only way kind_ ever changes - color()/gradient()/
        // imagePath() below are pure storage, no kind_ side effect. That
        // used to seem like a convenience (setColor() implying "now paint
        // with color") until reflection's read path broke it: reading a
        // saved Fill back sets *every* property in registration order
        // regardless of which one was actually active when it was saved
        // (gradient/imagePath still get read - with default/empty values -
        // even for a PaintKind::Color fill), so a setter that also
        // touched kind_ as a side effect meant whichever of color/
        // gradient/imagePath happened to be read *last* silently won,
        // clobbering the real saved kind. Callers that want the old
        // convenience now make two calls (setColor(...); setKind(Color);)
        // - see e.g. ShapesReflection.CircleRoundTripsItsOwnGeometryTransformAndStyle
        // (test_shapes.cpp) for where this was actually caught.
        PaintKind kind() const { return kind_; }
        void setKind(PaintKind kind) { kind_ = kind; }

        const Color& color() const { return color_; }
        void setColor(const Color& color) { color_ = color; }

        Gradient& gradient() { return gradient_; }
        const Gradient& gradient() const { return gradient_; }
        void setGradient(const Gradient& gradient) { gradient_ = gradient; }

        // A plain path (loaded the same way ViewStyle::setBackgroundImage()
        // already does, via BLImage::read_from_file()) rather than a live
        // Image, so this stays reflectable (a plain string) and
        // serializes/deserializes without embedding pixel data. Lazily
        // decoded and cached by toBLVar() - see resolvedImage().
        const std::string& imagePath() const { return imagePath_; }
        void setImagePath(const std::string& path) { imagePath_ = path; imageCacheValid_ = false; }

        float opacity() const { return opacity_; }
        void setOpacity(float opacity) { opacity_ = opacity; }

        // Resolves to whatever set_fill_style()/set_stroke_style() accepts
        // for whichever kind() is currently active: a solid color, a live
        // BLGradient (gradient().toBLVar(localBounds)), or imagePath()'s
        // decoded image wrapped in a BLPattern - a null BLVar for
        // PaintKind::None, or for a PaintKind::Image whose imagePath()
        // failed to load. localBounds is only meaningful for
        // PaintKind::Gradient (forwarded to Gradient::toBLVar() - see its
        // own comment).
        BLVar toBLVar(const Rect& localBounds) const;

    private:
        const BLImage& resolvedImage() const;

        PaintKind kind_ = PaintKind::None;
        Color color_{0.0f, 0.0f, 0.0f, 1.0f};
        Gradient gradient_;
        std::string imagePath_;
        float opacity_ = 1.0f;

        // A plain BLImage (blend2d's own copy-on-write, cheaply-copyable
        // image type), not a gfx::Image - the GDI/DIB backing gfx::Image
        // provides (memDC(), for interop with GDI-only APIs) is never
        // needed here, toBLVar() only ever wraps this in a BLPattern.
        // Deliberately not gfx::Image for a second reason too: gfx::Image
        // is non-copyable (it owns a live HBITMAP/HDC), and Fill needing
        // to stay copy-constructible is load-bearing - reflectgen's
        // Method::invoke()/ClassBuilder::property() both box values
        // through std::any, which requires it (see the comment on
        // ShapeStyle's fill()/stroke() accessors for what silently broke
        // before this was a plain BLImage).
        mutable BLImage imageCache_;
        mutable bool imageCacheValid_ = false;
    };

    // Fill plus stroke width - a Shape's own stroke is otherwise painted
    // exactly like its fill (same kind()/color()/gradient()/imagePath()/
    // opacity() vocabulary), just along the outline instead of the
    // interior.
    class Stroke : public Fill {
    public:
        Stroke() = default;

        float width() const { return width_; }
        void setWidth(float width) { width_ = width; }

    private:
        float width_ = 1.0f;
    };

}

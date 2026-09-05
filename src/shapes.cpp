#include "newui/shapes.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

    // Box-blurs pixels (a widthxheight, PRGB32-premultiplied BGRA buffer,
    // rowStride bytes per row) in place, radius pixels per pass, 3 passes -
    // the standard "3x box blur approximates a Gaussian" trick, cheap
    // enough to redo per paint() call for the small offscreen masks
    // Shape::paintEffect() builds. Each of the 4 bytes/pixel is blurred
    // independently via a clamp-to-edge sliding window sum; that's valid
    // even in premultiplied space here because every pixel in the mask
    // starts out the exact same flat color at varying alpha (see
    // paintEffect()), so there's no premultiplied-color-fringing hazard a
    // multi-color image could have.
    void boxBlurLine(uint8_t* first, int count, intptr_t elementStride, int radius) {
        if (radius <= 0 || count <= 0) {
            return;
        }

        int windowSize = radius * 2 + 1;
        std::vector<uint8_t> copy(size_t(count) * 4);
        for (int i = 0; i < count; ++i) {
            std::memcpy(&copy[size_t(i) * 4], first + intptr_t(i) * elementStride, 4);
        }

        int sum[4] = {0, 0, 0, 0};
        for (int d = -radius; d <= radius; ++d) {
            int s = d < 0 ? 0 : (d >= count ? count - 1 : d);
            for (int c = 0; c < 4; ++c) {
                sum[c] += copy[size_t(s) * 4 + c];
            }
        }

        for (int i = 0; i < count; ++i) {
            uint8_t* dst = first + intptr_t(i) * elementStride;
            for (int c = 0; c < 4; ++c) {
                dst[c] = uint8_t(sum[c] / windowSize);
            }

            int addI = i + radius + 1;
            addI = addI >= count ? count - 1 : addI;
            int subI = i - radius;
            subI = subI < 0 ? 0 : subI;
            for (int c = 0; c < 4; ++c) {
                sum[c] += int(copy[size_t(addI) * 4 + c]) - int(copy[size_t(subI) * 4 + c]);
            }
        }
    }

    void boxBlurPass(uint8_t* pixels, int width, int height, intptr_t stride, int radius) {
        if (radius <= 0) {
            return;
        }
        for (int y = 0; y < height; ++y) {
            boxBlurLine(pixels + intptr_t(y) * stride, width, 4, radius);
        }
        for (int x = 0; x < width; ++x) {
            boxBlurLine(pixels + intptr_t(x) * 4, height, stride, radius);
        }
    }

    void boxBlur3(uint8_t* pixels, int width, int height, intptr_t stride, float softness) {
        int radius = int(std::lround(softness));
        if (radius <= 0) {
            return;
        }
        boxBlurPass(pixels, width, height, stride, radius);
        boxBlurPass(pixels, width, height, stride, radius);
        boxBlurPass(pixels, width, height, stride, radius);
    }

    // How far past localBounds() a box-blurred mask can still show visible
    // (> ~1/255 alpha) coverage, in units of softness - matches boxBlur3()
    // above (3 passes of a radius derived 1:1 from softness), padded a
    // little further since a box blur's own tail isn't infinitely sharp.
    constexpr float kBlurPadFactor = 3.0f;

    bool rectsIntersect(const newui::Rect& a, const newui::Rect& b) {
        return a.left() < b.right() && a.right() > b.left()
            && a.top() < b.bottom() && a.bottom() > b.top();
    }

    // The cubic Bezier control points for one segment of a Catmull-Rom
    // spline through a point list - p0/p3 are the two points this segment
    // actually runs between (a curve waypoint, e.g. Curve's own points()
    // entries), c1/c2 the control points derived from their neighbors.
    // Shared by Curve::buildPath() (which just emits these into a BLPath)
    // and TextOnPath::buildPath() (which walks them by arc length
    // instead) - single source of truth for the curve both draw, so they
    // can never visually disagree with each other.
    struct CubicSegment {
        newui::Point p0, c1, c2, p3;
    };

    // Same Catmull-Rom-to-cubic-Bezier conversion Curve::buildPath() used
    // to do inline - see its own (now-shared) comment there for the
    // clamped/wrapped-neighbor construction at open/closed path ends.
    std::vector<CubicSegment> catmullRomSegments(const std::vector<newui::Point>& points, bool closed) {
        std::vector<CubicSegment> segments;

        size_t n = points.size();
        if (n < 2) {
            return segments;
        }

        auto at = [&](long i) -> const newui::Point& {
            if (closed) {
                long m = long(n);
                i = ((i % m) + m) % m;
                return points[size_t(i)];
            }
            i = i < 0 ? 0 : (i >= long(n) ? long(n) - 1 : i);
            return points[size_t(i)];
        };

        size_t segmentCount = closed ? n : (n - 1);
        segments.reserve(segmentCount);

        for (size_t i = 0; i < segmentCount; ++i) {
            const newui::Point& p0 = at(long(i) - 1);
            const newui::Point& p1 = at(long(i));
            const newui::Point& p2 = at(long(i) + 1);
            const newui::Point& p3 = at(long(i) + 2);

            newui::Point c1 = p1 + (p2 - p0) / 6.0f;
            newui::Point c2 = p2 - (p3 - p1) / 6.0f;

            segments.push_back(CubicSegment{p1, c1, c2, p2});
        }

        return segments;
    }

    newui::Point evalCubic(const CubicSegment& seg, float t) {
        float u = 1.0f - t;
        float uu = u * u;
        float tt = t * t;
        float uuu = uu * u;
        float ttt = tt * t;

        float x = uuu * seg.p0.x + 3.0f * uu * t * seg.c1.x + 3.0f * u * tt * seg.c2.x + ttt * seg.p3.x;
        float y = uuu * seg.p0.y + 3.0f * uu * t * seg.c1.y + 3.0f * u * tt * seg.c2.y + ttt * seg.p3.y;
        return newui::Point(x, y);
    }

    // One point of a dense, fixed-step flattening of the curve, tagged
    // with its own cumulative arc length from the curve's start -
    // TextOnPath::buildPath() walks this (via pointAndTangentAtDistance()
    // below) rather than the raw cubic segments, since arc length along a
    // cubic Bezier has no closed form. Fixed kSamplesPerSegment steps per
    // segment, not adaptive/curvature-aware - fine for the small,
    // gently-curving demo-scale paths this is meant for.
    struct CurveSample {
        newui::Point point;
        float distance;
    };

    constexpr int kSamplesPerSegment = 24;

    std::vector<CurveSample> sampleCurve(const std::vector<newui::Point>& points, bool closed) {
        std::vector<CurveSample> samples;

        std::vector<CubicSegment> segments = catmullRomSegments(points, closed);
        if (segments.empty()) {
            return samples;
        }

        newui::Point prev = segments.front().p0;
        float distance = 0.0f;
        samples.push_back(CurveSample{prev, 0.0f});

        for (const CubicSegment& seg : segments) {
            for (int i = 1; i <= kSamplesPerSegment; ++i) {
                float t = float(i) / float(kSamplesPerSegment);
                newui::Point p = evalCubic(seg, t);

                float dx = p.x - prev.x;
                float dy = p.y - prev.y;
                distance += std::sqrt(dx * dx + dy * dy);

                samples.push_back(CurveSample{p, distance});
                prev = p;
            }
        }

        return samples;
    }

    // Finds where distance (clamped to [0, the curve's own total length])
    // falls along samples and linearly interpolates both position and
    // tangent angle (via the straight line between the two bracketing
    // samples - dense enough sampling that this reads as smooth) between
    // them. Returns false only if samples has fewer than 2 entries (an
    // empty or single-point curve - nothing to walk).
    bool pointAndTangentAtDistance(const std::vector<CurveSample>& samples, float distance,
            newui::Point& outPoint, float& outAngleRadians) {
        if (samples.size() < 2) {
            return false;
        }

        float totalLength = samples.back().distance;
        distance = distance < 0.0f ? 0.0f : (distance > totalLength ? totalLength : distance);

        size_t index = 1;
        while (index < samples.size() - 1 && samples[index].distance < distance) {
            ++index;
        }

        const CurveSample& a = samples[index - 1];
        const CurveSample& b = samples[index];
        float segLength = b.distance - a.distance;
        float t = segLength > 0.0f ? (distance - a.distance) / segLength : 0.0f;

        outPoint = newui::Point(a.point.x + (b.point.x - a.point.x) * t, a.point.y + (b.point.y - a.point.y) * t);
        outAngleRadians = std::atan2(double(b.point.y - a.point.y), double(b.point.x - a.point.x));
        return true;
    }

    // Byte length of the UTF-8 codepoint starting at leadByte - used to
    // walk TextOnPath's text() one character at a time without splitting
    // a multi-byte sequence. Falls back to 1 for an invalid/continuation
    // lead byte rather than looping forever.
    int utf8CharLength(unsigned char leadByte) {
        if ((leadByte & 0x80u) == 0x00u) return 1;
        if ((leadByte & 0xE0u) == 0xC0u) return 2;
        if ((leadByte & 0xF0u) == 0xE0u) return 3;
        if ((leadByte & 0xF8u) == 0xF0u) return 4;
        return 1;
    }

}

namespace newui::shapes {

    Rect Shape::boundsWithEffects() const {
        Rect bounds = localBounds();

        float pad = style_.stroke().width() * 0.5f;

        if (style_.glow().enabled()) {
            float glowPad = style_.glow().softness() * kBlurPadFactor;
            pad = pad > glowPad ? pad : glowPad;
        }

        if (style_.dropShadow().enabled()) {
            const Point& offset = style_.dropShadow().offset();
            float offsetMag = std::fabs(offset.x) > std::fabs(offset.y) ? std::fabs(offset.x) : std::fabs(offset.y);
            float shadowPad = style_.dropShadow().softness() * kBlurPadFactor + offsetMag;
            pad = pad > shadowPad ? pad : shadowPad;
        }

        return bounds.deflate(-pad);
    }

    Rect Shape::worldBoundsWithEffects() const {
        Rect bounds = boundsWithEffects();

        BLMatrix2D m = BLMatrix2D::make_identity();
        m.translate(double(transform_.position().x), double(transform_.position().y));
        if (transform_.rotationRadians() != 0.0f) {
            m.rotate(double(transform_.rotationRadians()), double(transform_.pivot().x), double(transform_.pivot().y));
        }
        m.scale(double(transform_.scale().x), double(transform_.scale().y));

        BLPoint corners[4] = {
            m.map_point(bounds.left(), bounds.top()),
            m.map_point(bounds.right(), bounds.top()),
            m.map_point(bounds.right(), bounds.bottom()),
            m.map_point(bounds.left(), bounds.bottom())
        };

        double minX = corners[0].x, maxX = corners[0].x;
        double minY = corners[0].y, maxY = corners[0].y;
        for (int i = 1; i < 4; ++i) {
            minX = corners[i].x < minX ? corners[i].x : minX;
            maxX = corners[i].x > maxX ? corners[i].x : maxX;
            minY = corners[i].y < minY ? corners[i].y : minY;
            maxY = corners[i].y > maxY ? corners[i].y : maxY;
        }

        return Rect(float(minX), float(minY), float(maxX - minX), float(maxY - minY));
    }

    void Shape::paintFillAndStroke(BLContext& ctx, const BLPath& path) const {
        Rect bounds = localBounds();

        if (style_.fill().kind() != gfx::PaintKind::None) {
            BLVar fillVar = style_.fill().toBLVar(bounds);
            if (!fillVar.is_null()) {
                ctx.set_fill_style(fillVar);
                ctx.set_fill_alpha(double(style_.fill().opacity()));
                ctx.fill_path(path);
            }
        }

        if (style_.stroke().kind() != gfx::PaintKind::None && style_.stroke().width() > 0.0f) {
            BLVar strokeVar = style_.stroke().toBLVar(bounds);
            if (!strokeVar.is_null()) {
                ctx.set_stroke_style(strokeVar);
                ctx.set_stroke_alpha(double(style_.stroke().opacity()));
                ctx.set_stroke_width(double(style_.stroke().width()));
                ctx.stroke_path(path);
            }
        }
    }

    void Shape::paintEffect(BLContext& ctx, const BLPath& path, const Color& color,
            CompositingFlag compOp, float amount, float softness, const Point& offset) const {
        if (amount <= 0.0f) {
            return;
        }

        Rect bounds = localBounds();

        float offsetMag = std::fabs(offset.x) > std::fabs(offset.y) ? std::fabs(offset.x) : std::fabs(offset.y);
        float pad = style_.stroke().width() * 0.5f + softness * kBlurPadFactor + offsetMag + 2.0f;

        Rect maskBoundsLocal = bounds.deflate(-pad);
        // Floored to a whole pixel - blit_image() below hands this
        // straight to BLContext as its destination position, and a
        // fractional one trips a real Blend2D JIT limitation: with no
        // rotation/extend mode in play, FetchSimplePatternPart::_init_part()
        // takes its "aligned blit" fast path (is_aligned_blit()) based only
        // on scale/rotation, but a sub-pixel-offset blit still needs
        // bilinear resampling - is_rect_fill() then disagrees, and
        // BL_ASSERT(is_rect_fill()) fires in a debug build (rootview.cpp's
        // own snappedToPixels() hit and documented this same assertion for
        // its dirty-rect clip; this is the same fix, applied here instead).
        double maskLeft = std::floor(maskBoundsLocal.left());
        double maskTop = std::floor(maskBoundsLocal.top());
        int maskW = int(std::ceil(maskBoundsLocal.right() - maskLeft));
        int maskH = int(std::ceil(maskBoundsLocal.bottom() - maskTop));
        if (maskW <= 0 || maskH <= 0) {
            return;
        }

        BLImage mask;
        if (mask.create(maskW, maskH, BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return;
        }

        {
            BLContext maskCtx(mask);
            maskCtx.clear_all();
            maskCtx.translate(double(-maskLeft + offset.x), double(-maskTop + offset.y));

            BLRgba32 solid = color.toBLRgba32();
            if (style_.fill().kind() != gfx::PaintKind::None) {
                maskCtx.set_fill_style(solid);
                maskCtx.fill_path(path);
            }
            if (style_.stroke().kind() != gfx::PaintKind::None && style_.stroke().width() > 0.0f) {
                maskCtx.set_stroke_style(solid);
                maskCtx.set_stroke_width(double(style_.stroke().width()));
                maskCtx.stroke_path(path);
            }
            maskCtx.end();
        }

        BLImageData data;
        mask.get_data(&data);
        boxBlur3(static_cast<uint8_t*>(data.pixel_data), maskW, maskH, data.stride, softness);

        ctx.save();
        ctx.set_comp_op(toBLCompOp(compOp));
        ctx.set_global_alpha(double(amount));
        ctx.blit_image(BLPoint(maskLeft, maskTop), mask);
        ctx.restore();
    }

    void Shape::render(BLContext& ctx) const {
        if (style_.opacity() <= 0.0f) {
            return;
        }

        BLPath path;
        buildPath(path);

        ctx.save();
        transform_.applyTo(ctx);
        ctx.set_comp_op(toBLCompOp(style_.compositingOp()));
        ctx.set_global_alpha(double(style_.opacity()));

        if (style_.dropShadow().enabled()) {
            paintEffect(ctx, path, style_.dropShadow().color(), style_.dropShadow().compOp(),
                style_.dropShadow().amount(), style_.dropShadow().softness(), style_.dropShadow().offset());
        }

        if (style_.glow().enabled()) {
            paintEffect(ctx, path, style_.glow().color(), style_.glow().compOp(),
                style_.glow().amount(), style_.glow().softness(), Point());
        }

        paintFillAndStroke(ctx, path);

        ctx.restore();
    }

    Rect Line::localBounds() const {
        float left = p0_.x < p1_.x ? p0_.x : p1_.x;
        float top = p0_.y < p1_.y ? p0_.y : p1_.y;
        float right = p0_.x > p1_.x ? p0_.x : p1_.x;
        float bottom = p0_.y > p1_.y ? p0_.y : p1_.y;
        return Rect(left, top, right - left, bottom - top);
    }

    void Line::buildPath(BLPath& path) const {
        path.move_to(double(p0_.x), double(p0_.y));
        path.line_to(double(p1_.x), double(p1_.y));
    }

    void Rectangle::buildPath(BLPath& path) const {
        path.add_rect(double(x_), double(y_), double(width_), double(height_));
    }

    void RoundRect::buildPath(BLPath& path) const {
        path.add_round_rect(BLRoundRect(double(x()), double(y()), double(width()), double(height()), double(radiusX_), double(radiusY_)));
    }

    void Circle::buildPath(BLPath& path) const {
        path.add_circle(BLCircle(double(centerX_), double(centerY_), double(radius_)));
    }

    void Arc::buildPath(BLPath& path) const {
        BLArc arc{double(centerX()), double(centerY()), double(radius()), double(radius()), double(startRadians_), double(sweepRadians_)};
        if (pie_) {
            path.add_pie(arc);
        } else {
            path.add_arc(arc);
        }
    }

    Rect Path::localBounds() const {
        if (points_.empty()) {
            return Rect();
        }

        float minX = points_[0].x, maxX = points_[0].x;
        float minY = points_[0].y, maxY = points_[0].y;
        for (const Point& p : points_) {
            minX = p.x < minX ? p.x : minX;
            maxX = p.x > maxX ? p.x : maxX;
            minY = p.y < minY ? p.y : minY;
            maxY = p.y > maxY ? p.y : maxY;
        }

        return Rect(minX, minY, maxX - minX, maxY - minY);
    }

    void Path::buildPath(BLPath& path) const {
        if (points_.empty()) {
            return;
        }

        path.move_to(double(points_[0].x), double(points_[0].y));
        for (size_t i = 1; i < points_.size(); ++i) {
            path.line_to(double(points_[i].x), double(points_[i].y));
        }

        if (closed()) {
            path.close();
        }
    }

    void Curve::buildPath(BLPath& path) const {
        // Catmull-Rom -> cubic Bezier via the shared catmullRomSegments()
        // helper (see its own comment, this file's anonymous namespace) -
        // TextOnPath::buildPath() walks the exact same segments by arc
        // length instead of emitting them here, so the two can never
        // visually disagree about what curve points() traces.
        std::vector<CubicSegment> segments = catmullRomSegments(points_, closed());
        if (segments.empty()) {
            Path::buildPath(path);
            return;
        }

        path.move_to(double(segments.front().p0.x), double(segments.front().p0.y));
        for (const CubicSegment& seg : segments) {
            path.cubic_to(double(seg.c1.x), double(seg.c1.y), double(seg.c2.x), double(seg.c2.y),
                double(seg.p3.x), double(seg.p3.y));
        }

        if (closed()) {
            path.close();
        }
    }

    Rect Text::localBounds() const {
        if (text_.empty()) {
            return Rect(x_, y_, 0.0f, 0.0f);
        }

        TextMetrics tm = font_.measureText(text_);
        return Rect(x_, y_ - tm.ascent, tm.width, tm.ascent + tm.descent);
    }

    void Text::buildPath(BLPath& path) const {
        BLFont* blFont = font_.blFont();
        if (blFont == nullptr || text_.empty()) {
            return;
        }

        BLGlyphBuffer gb;
        gb.set_utf8_text(text_.c_str(), text_.size());
        blFont->shape(gb);

        BLMatrix2D m = BLMatrix2D::make_translation(double(x_), double(y_));
        blFont->get_glyph_run_outlines(gb.glyph_run(), m, path);
    }

    Rect TextOnPath::localBounds() const {
        if (points_.empty()) {
            return Rect();
        }

        float minX = points_[0].x, maxX = points_[0].x;
        float minY = points_[0].y, maxY = points_[0].y;
        for (const Point& p : points_) {
            minX = p.x < minX ? p.x : minX;
            maxX = p.x > maxX ? p.x : maxX;
            minY = p.y < minY ? p.y : minY;
            maxY = p.y > maxY ? p.y : maxY;
        }

        float pad = font_.size() * 1.2f;
        return Rect(minX - pad, minY - pad, (maxX - minX) + pad * 2.0f, (maxY - minY) + pad * 2.0f);
    }

    void TextOnPath::buildPath(BLPath& path) const {
        if (text_.empty() || points_.size() < 2) {
            return;
        }

        BLFont* blFont = font_.blFont();
        if (blFont == nullptr) {
            return;
        }

        std::vector<CurveSample> samples = sampleCurve(points_, closed());
        if (samples.size() < 2) {
            return;
        }

        float cursor = startOffset_;

        size_t byteOffset = 0;
        while (byteOffset < text_.size()) {
            size_t remaining = text_.size() - byteOffset;
            size_t charLength = size_t(utf8CharLength(static_cast<unsigned char>(text_[byteOffset])));
            charLength = charLength < remaining ? charLength : remaining;

            std::string ch = text_.substr(byteOffset, charLength);
            byteOffset += charLength;

            BLGlyphBuffer gb;
            gb.set_utf8_text(ch.c_str(), ch.size());
            blFont->shape(gb);
            if (gb.size() == 0) {
                continue;
            }

            BLTextMetrics tm;
            blFont->get_text_metrics(gb, tm);
            float advance = float(tm.advance.x);

            // Placed by this character's own center, not its leading
            // edge - centers a single-character glyph on the curve's
            // local tangent more evenly than a leading-edge placement
            // would (which visibly drifts to one side of the curve on a
            // sharp bend).
            Point centerPoint;
            float angleRadians = 0.0f;
            if (pointAndTangentAtDistance(samples, cursor + advance * 0.5f, centerPoint, angleRadians)) {
                // Glyph outlines come out relative to the glyph's own
                // baseline origin (0,0) - shift left by half this
                // glyph's own advance first (so the shift happens in the
                // glyph's own, unrotated space), then rotate to the
                // curve's tangent there, then translate onto
                // centerPoint. BLMatrix2D composes right-to-left against
                // a point (matching make_translation()/rotate()/
                // translate() below applying in that written order), so
                // the shift is the last one constructed here despite
                // being the first one conceptually applied.
                BLMatrix2D m = BLMatrix2D::make_translation(double(centerPoint.x), double(centerPoint.y));
                m.rotate(double(angleRadians));
                m.translate(double(-advance) * 0.5, 0.0);

                blFont->get_glyph_run_outlines(gb.glyph_run(), m, path);
            }

            cursor += advance;
        }
    }

    void ShapeLayer::render(BLContext& ctx, const Rect& visibleRect) const {
        ctx.save();
        ctx.set_comp_op(toBLCompOp(compositingOp_));
        ctx.set_global_alpha(double(opacity_));

        for (const Shape* shape : shapes_) {
            if (rectsIntersect(shape->worldBoundsWithEffects(), visibleRect)) {
                shape->render(ctx);
            }
        }

        ctx.restore();
    }

}

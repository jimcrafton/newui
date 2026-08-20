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
        int maskW = int(std::ceil(maskBoundsLocal.width()));
        int maskH = int(std::ceil(maskBoundsLocal.height()));
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
            maskCtx.translate(double(-maskBoundsLocal.left() + offset.x), double(-maskBoundsLocal.top() + offset.y));

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
        ctx.blit_image(BLPoint(double(maskBoundsLocal.left()), double(maskBoundsLocal.top())), mask);
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
        size_t n = points_.size();
        if (n < 2) {
            Path::buildPath(path);
            return;
        }

        // Catmull-Rom -> cubic Bezier: each segment p[i]->p[i+1] gets
        // control points derived from its neighbors p[i-1]/p[i+2] (the
        // standard uniform-Catmull-Rom tangent, scaled by 1/6) - clamped
        // to the path's own endpoints when there's no neighbor on one
        // side (open path) or wrapped around (closed path).
        bool isClosed = closed();
        auto at = [&](long i) -> const Point& {
            if (isClosed) {
                long m = long(n);
                i = ((i % m) + m) % m;
                return points_[size_t(i)];
            }
            i = i < 0 ? 0 : (i >= long(n) ? long(n) - 1 : i);
            return points_[size_t(i)];
        };

        path.move_to(double(points_[0].x), double(points_[0].y));

        size_t segments = isClosed ? n : (n - 1);
        for (size_t i = 0; i < segments; ++i) {
            const Point& p0 = at(long(i) - 1);
            const Point& p1 = at(long(i));
            const Point& p2 = at(long(i) + 1);
            const Point& p3 = at(long(i) + 2);

            Point c1 = p1 + (p2 - p0) / 6.0f;
            Point c2 = p2 - (p3 - p1) / 6.0f;

            path.cubic_to(double(c1.x), double(c1.y), double(c2.x), double(c2.y), double(p2.x), double(p2.y));
        }

        if (isClosed) {
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

#include "newui/underlay.h"

namespace newui {

    void Underlay::viewSized(const Size& newSize) {
        if (newSize.width <= 0.0f || newSize.height <= 0.0f) {
            return;
        }

        size_ = newSize;
        image_ = gfx::Image(static_cast<int>(newSize.width), static_cast<int>(newSize.height));
    }

    void Underlay::paint(BLContext& ctx, const Rect& rect) {
        ctx.save();
        ctx.set_comp_op(toBLCompOp(compositingOp_));
        ctx.set_fill_style(fillColor_.toBLRgba32());
        ctx.set_fill_alpha(opacity_);
        ctx.fill_rect(BLRect(rect));
        ctx.restore();

        if (sourceImage_.isValid()) {
            ctx.blit_image(BLPoint(0, 0), sourceImage_.blImage());
        }

        shapeLayer_.render(ctx, rect);
    }

    void Underlay::render() {
        if (!image_.isValid()) {
            return;
        }

        BLContext ctx(image_.blImage());
        ctx.clear_all();
        if (visible_) {
            paint(ctx, Rect(0.0f, 0.0f, size_.width, size_.height));
        }
        ctx.end();
    }

    bool Underlay::loadImageFile(const std::string& path) {
        if (size_.width <= 0.0f || size_.height <= 0.0f) {
            return false;
        }

        gfx::Image loaded(path, static_cast<int>(size_.width), static_cast<int>(size_.height));
        if (!loaded.isValid()) {
            return false;
        }

        sourceImage_ = std::move(loaded);
        return true;
    }

}

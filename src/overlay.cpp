#include "newui/overlay.h"

namespace newui {

    void Overlay::paint(BLContext& ctx, const Rect& rect) {
        ctx.save();
        ctx.set_comp_op(toBLCompOp(compositingOp_));
        ctx.set_fill_style(fillColor_.toBLRgba32());
        ctx.set_fill_alpha(opacity_);
        ctx.fill_rect(BLRect(rect));
        ctx.restore();

        shapeLayer_.render(ctx, rect);
    }

}

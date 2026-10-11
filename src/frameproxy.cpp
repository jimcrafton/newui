#include "newui/frameproxy.h"
#include "newui/shapes.h"
#include "newui/uicolormanager.h"

namespace newui {

FrameProxy::FrameProxy() {
    setVisible(true);
    titleFont_ = FontManager::getSystemFont(SystemUIFont::Caption);
    // Body/title-bar background are painted directly in paint() as a
    // rounded shape, not via style().setBackgroundColor() - View::
    // paintStyle() always runs before paint() (view.cpp's paintChildren()),
    // so a plain style background fill here would square off exactly the
    // corners this class exists to round, underneath whatever paint()
    // draws on top.
}

void FrameProxy::paint(BLContext& ctx) {
    Rect clientBounds = getClientBounds();
    if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
        return;
    }

    double x0 = clientBounds.left();
    double y0 = clientBounds.top();
    double w = clientBounds.size().width;
    double h = clientBounds.size().height;
    double r = kCornerRadius;

    // A real shapes::RoundRect (not a plain ctx.fill_round_rect()) for the
    // fill/stroke pipeline consistency, though not for its dropShadow -
    // View::paintChildren() (view.cpp) clips every child to exactly its
    // own bounding box before calling paint(), which cuts off a shadow's
    // blurred mask (it needs to bleed outside these bounds) entirely. A
    // real, caught bug: FrameProxy's own would-be shadow here silently
    // never showed up live. Whatever hosts a FrameProxy (e.g. cpp_codetools'
    // own CanvasWell) casts its shadow instead, in a paint() pass that
    // isn't clipped to FrameProxy's own tiny bounds - see CanvasWell.cpp's
    // own comment. Built fresh each paint() call - clientBounds can change
    // on resize, and this is cheap (no heap state to keep in sync).
    shapes::RoundRect bodyShape;
    bodyShape.setX(float(x0));
    bodyShape.setY(float(y0));
    bodyShape.setWidth(float(w));
    bodyShape.setHeight(float(h));
    bodyShape.setRadiusX(float(r));
    bodyShape.setRadiusY(float(r));
    bodyShape.style().fill().setColor(UIColorManager::colorFor(UIColorRole::ControlBackground));
    bodyShape.style().fill().setKind(gfx::PaintKind::Color);
    bodyShape.render(ctx);

    Rect barBounds(x0, y0, w, kTitleBarHeight);

    // The title bar follows the body's own top-left/top-right rounding but
    // stays square along its own bottom edge, where it meets the body.
    BLPath barPath;
    shapes::buildPartiallyRoundedRectPath(barPath, barBounds, r, /*roundTop=*/true, /*roundBottom=*/false);

    ctx.save();
    ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightBackground).toBLRgba32());
    ctx.fill_path(barPath);
    ctx.restore();

    if (title_.empty()) {
        return;
    }

    BLFont* blFont = titleFont_.blFont();
    if (blFont == nullptr || !blFont->is_valid()) {
        return;
    }

    const BLFontMetrics& fontMetrics = blFont->metrics();
    double tx = barBounds.left() + 8.0;
    double ty = barBounds.top() + (kTitleBarHeight - (fontMetrics.ascent + fontMetrics.descent)) * 0.5 + fontMetrics.ascent;

    ctx.save();
    ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::HighlightText).toBLRgba32());
    ctx.fill_utf8_text(BLPoint(tx, ty), *blFont, title_.c_str(), title_.size());
    ctx.restore();
}

}

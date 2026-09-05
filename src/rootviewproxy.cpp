#include "newui/rootviewproxy.h"
#include "newui/shapes.h"
#include "newui/uicolormanager.h"

namespace newui {

RootViewProxy::RootViewProxy() {
    setVisible(true);
    // Background is painted directly in paint(), not via
    // style().setBackgroundColor() - lets cornerRadius_ round just the
    // bottom two corners (matching a hosting FrameProxy's own body shape,
    // if any) the way FrameProxy's own title bar/body split does -
    // ViewStyle's rectRadius is one uniform value across all four corners,
    // which can't express that split.
}

void RootViewProxy::paint(BLContext& ctx) {
    Rect clientBounds = getClientBounds();
    if (clientBounds.size().width <= 0.0f || clientBounds.size().height <= 0.0f) {
        return;
    }

    BLPath path;
    shapes::buildPartiallyRoundedRectPath(path, clientBounds, cornerRadius_, /*roundTop=*/false, /*roundBottom=*/true);

    ctx.save();
    ctx.set_fill_style(UIColorManager::colorFor(UIColorRole::WindowBackground).toBLRgba32());
    ctx.fill_path(path);
    ctx.restore();
}

}

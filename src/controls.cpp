#include "newui/controls.h"

namespace newui {

    Control::Control() {
        onMouseDown.add(this, &Control::handleTrackingMouseDown);
        onMouseUp.add(this, &Control::handleTrackingMouseUp);
    }

    SyncReturn Control::handleTrackingMouseDown(View& /*sender*/, const Point& /*pt*/,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        if (!isEnabled()) {
            return SyncReturn::Ignored;
        }
        tracking_ = true;
        return SyncReturn::Ignored;
    }

    SyncReturn Control::handleTrackingMouseUp(View& /*sender*/, const Point& pt,
            std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/) {
        bool wasTracking = tracking_;
        tracking_ = false;
        if (!wasTracking || !isEnabled()) {
            return SyncReturn::Ignored;
        }

        // pt arrives in this Control's own local space (see
        // RootView::mouseUp()'s capturedSubView_ handling) - a
        // zero-origin rect matching this Control's own size is what
        // "still inside" means here, mirroring UIControl's isTouchInside
        // at release time.
        Rect localBounds(Point(0.0f, 0.0f), getBounds().size());
        if (localBounds.contains(pt)) {
            onClick(*this);
        }
        return SyncReturn::Ignored;
    }

}

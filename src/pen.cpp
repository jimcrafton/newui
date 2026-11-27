#include <newui/pen.h>

#if defined(_WIN32)
#include "newui/win32/winheaders.h"
#endif

namespace newui {

    namespace {
        // The Win32 pointer/pen bit values PenInfo carries (winuser.h), named here so this file needs no SDK.
        constexpr unsigned kPointerInRange = 0x2, kPointerInContact = 0x4;
        constexpr unsigned kPenBarrel = 0x1, kPenInverted = 0x2, kPenEraser = 0x4;
        constexpr unsigned kMaskPressure = 0x1, kMaskRotation = 0x2, kMaskTiltX = 0x4, kMaskTiltY = 0x8;

#if defined(_WIN32)
        static_assert(kPointerInRange == POINTER_FLAG_INRANGE && kPointerInContact == POINTER_FLAG_INCONTACT, "pointer flags");
        static_assert(kPenBarrel == PEN_FLAG_BARREL && kPenInverted == PEN_FLAG_INVERTED && kPenEraser == PEN_FLAG_ERASER, "pen flags");
        static_assert(kMaskPressure == PEN_MASK_PRESSURE && kMaskRotation == PEN_MASK_ROTATION &&
                      kMaskTiltX == PEN_MASK_TILT_X && kMaskTiltY == PEN_MASK_TILT_Y, "pen mask");
#endif
    }

    PenState penStateFrom(const PenInfo& info, const Point& clientPosition) {
        PenState state;
        state.inRange = (info.pointerFlags & kPointerInRange) != 0;
        state.inContact = (info.pointerFlags & kPointerInContact) != 0;
        state.barrel = (info.penFlags & kPenBarrel) != 0;
        state.eraser = (info.penFlags & (kPenEraser | kPenInverted)) != 0;
        state.hasPressure = (info.penMask & kMaskPressure) != 0;
        state.pressure = state.hasPressure ? float(info.pressure) / 1024.0f : (state.inContact ? 1.0f : 0.0f);
        state.hasTilt = (info.penMask & (kMaskTiltX | kMaskTiltY)) != 0;
        state.tiltX = (info.penMask & kMaskTiltX) != 0 ? info.tiltX : 0;
        state.tiltY = (info.penMask & kMaskTiltY) != 0 ? info.tiltY : 0;
        state.twist = (info.penMask & kMaskRotation) != 0 ? int(info.rotation) : 0;
        state.position = clientPosition;
        return state;
    }

}

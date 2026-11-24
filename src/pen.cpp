#include <newui/pen.h>

#include <windows.h>

namespace newui {

    double performanceSeconds(unsigned long long ticks) {
        static const double frequency = []() {
            LARGE_INTEGER f;
            ::QueryPerformanceFrequency(&f);
            return f.QuadPart > 0 ? double(f.QuadPart) : 1.0;
        }();
        return double(ticks) / frequency;
    }

    double performanceNowSeconds() {
        LARGE_INTEGER now;
        ::QueryPerformanceCounter(&now);
        return performanceSeconds(static_cast<unsigned long long>(now.QuadPart));
    }

    PenState penStateFrom(const PenInfo& info, const Point& clientPosition) {
        PenState state;
        state.inRange = (info.pointerFlags & POINTER_FLAG_INRANGE) != 0;
        state.inContact = (info.pointerFlags & POINTER_FLAG_INCONTACT) != 0;
        state.barrel = (info.penFlags & PEN_FLAG_BARREL) != 0;
        state.eraser = (info.penFlags & (PEN_FLAG_ERASER | PEN_FLAG_INVERTED)) != 0;
        state.hasPressure = (info.penMask & PEN_MASK_PRESSURE) != 0;
        state.pressure = state.hasPressure ? float(info.pressure) / 1024.0f : (state.inContact ? 1.0f : 0.0f);
        state.hasTilt = (info.penMask & (PEN_MASK_TILT_X | PEN_MASK_TILT_Y)) != 0;
        state.tiltX = (info.penMask & PEN_MASK_TILT_X) != 0 ? info.tiltX : 0;
        state.tiltY = (info.penMask & PEN_MASK_TILT_Y) != 0 ? info.tiltY : 0;
        state.twist = (info.penMask & PEN_MASK_ROTATION) != 0 ? int(info.rotation) : 0;
        state.position = clientPosition;
        state.timeSeconds = info.performanceCount != 0 ? performanceSeconds(info.performanceCount) : 0.0;
        return state;
    }

}

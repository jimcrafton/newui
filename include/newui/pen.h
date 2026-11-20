#pragma once

#include <newui/geometry.h>

namespace newui {

    // What the pen (or other stylus) is doing right now, from Windows pointer messages. A pen that reports no pressure
    // (some tablets, or a mouse) leaves hasPressure false and pressure at 1 while in contact.
    // @reflect ignore=true
    struct PenState {
        bool inRange = false;      // the tip is near the surface (hovering or touching)
        bool inContact = false;    // the tip is touching
        bool barrel = false;       // barrel button held (Windows also reports it as a right click)
        bool eraser = false;       // the eraser end is in use
        bool hasPressure = false;
        bool hasTilt = false;
        float pressure = 0.0f;     // 0..1
        int tiltX = 0;             // degrees, -90..90
        int tiltY = 0;
        int twist = 0;             // degrees, 0..359
        Point position;            // client pixels of the window that received it
    };

    // The raw pen fields Windows reports (a POINTER_PEN_INFO, without needing windows.h in this header).
    // @reflect ignore=true
    struct PenInfo {
        unsigned pointerFlags = 0;
        unsigned penFlags = 0;
        unsigned penMask = 0;
        unsigned pressure = 0;     // 0..1024
        unsigned rotation = 0;
        int tiltX = 0;
        int tiltY = 0;
    };

    PenState penStateFrom(const PenInfo& info, const Point& clientPosition);

}

// TODO(macport): screen conversions need the hosting NSView/NSWindow. Aborts rather than returning wrong points.
#include "newui/view.h"

#include <cstdio>
#include <cstdlib>

namespace newui {

    namespace {
        [[noreturn]] void notImplemented(const char* what) {
            std::fprintf(stderr, "newui: %s is not implemented on this platform yet\n", what);
            std::abort();
        }
    }

    Point View::localToScreen(const Point&) const { notImplemented("View::localToScreen"); }
    Point View::screenToLocal(const Point&) const { notImplemented("View::screenToLocal"); }

}

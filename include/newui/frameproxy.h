#pragma once

#include <newui/subview.h>
#include <newui/font.h>

namespace newui {

// Design-time stand-in for a real Frame, usable inside another RootView's
// own tree (a real Frame can't be nested there at all - it isn't even a
// View, see frame.h's own class comment - same reasoning RootViewProxy has
// one level down, see its own class comment). Duplicates the two Frame
// properties actually meaningful to show while editing - Frame::getName()/
// setName() (the bundle filename) is already inherited unchanged via
// Component (View's own name()/setName()), so only title() is genuinely
// new here.
//
// Paints a plain mock title bar - real system chrome font
// (SystemUIFont::Caption, the actual NONCLIENTMETRICS window-title font),
// no buttons or other chrome beyond a colored bar and the title text. Not
// a functional window - just enough for the design surface to visually
// read as "here's the window being edited", one level above whatever
// RootViewProxy content sits below it (kTitleBarHeight is how much top
// space a caller assembling that pair needs to reserve for it).
//
// @reflect proxyfor=Frame
class FrameProxy : public SubView {
public:
    static constexpr float kTitleBarHeight = 28.0f;

    FrameProxy();
    virtual ~FrameProxy() = default;

    void setTitle(std::string title) { title_ = std::move(title); redraw(); }
    const std::string& title() const { return title_; }

    void paint(BLContext& ctx) override;

private:
    std::string title_;
    Font titleFont_;
};

}

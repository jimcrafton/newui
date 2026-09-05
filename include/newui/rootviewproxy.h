#pragma once

#include <newui/subview.h>

namespace newui {

// Design-time stand-in for a real RootView, usable inside another
// RootView's own tree (a real RootView can never be nested there -
// View::addChild() only ever takes a SubView*, and RootView derives from
// View directly, not SubView - see view.h/rootview.h's own class
// comments). Structurally adds nothing beyond a plain background color:
// everything a saved rootView document actually needs (bounds/visible/
// name/style/layout/childViews) already lives on View/SubView, which this
// inherits unchanged. See Class::proxyFor()/ObjectWriter's design-mode
// substitution (reflection.h/reflectionio.h) for how a tree built under
// this still round-trips as an ordinary "type": "RootView" document.
//
// @reflect proxyfor=RootView
class RootViewProxy : public SubView {
public:
    RootViewProxy();
    virtual ~RootViewProxy() = default;

    void paint(BLContext& ctx) override;

    // Rounds this view's own bottom-left/bottom-right corners by this
    // radius when painting its background - its top edge is assumed to
    // always meet a square seam (a FrameProxy's own title bar sitting
    // directly above it), never independently rounded. 0 (the default)
    // paints a plain square background, correct for a standalone
    // RootViewProxy with no FrameProxy above it. A FrameProxy sets this to
    // match its own body's corner radius once it addChild()s a
    // RootViewProxy under itself - otherwise the RootViewProxy's own square
    // background fill (painted after the FrameProxy's own paint(), since
    // it's a child) would overwrite the FrameProxy body's already-rounded
    // bottom corners.
    float cornerRadius() const { return cornerRadius_; }
    void setCornerRadius(float radius) { cornerRadius_ = radius; redraw(); }

private:
    float cornerRadius_ = 0.0f;
};

}

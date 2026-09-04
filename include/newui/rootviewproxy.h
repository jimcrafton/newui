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
};

}

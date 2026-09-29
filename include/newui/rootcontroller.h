#pragma once

#include <newui/controllers.h>
#include <newui/rootview.h>

#include <string>

namespace newui {

// A screen/surface-level Controller wrapping exactly one already-loaded RootView it neither
// builds nor owns - e.g. one loaded via Bundle::loadFrame() from a .newui file, then handed off
// to a subclass that resolves named children and wires their delegates in internal_init()
// (Component::initialize()'s own protected hook).
//
// Deliberately NOT ViewController: that class builds+owns its own SubView via a required
// loadView() override and supports being presented/dismissed into a container with a
// childControllers() tree and transition animations - none of which applies here. Inherits
// Controller directly instead, the same exception ViewController's own class comment carves out
// for itself ("a screen-level controller genuinely IS one") - CodeToolsVsix's own
// ViewDesignerController already does exactly this, holding raw pointers into a View tree it
// doesn't own or build either.
class RootController : public Controller {
public:
    explicit RootController(RootView* view) : view_(view) {}

    RootView* view() const { return view_; }

protected:
    // Named-lookup + cast, in one call instead of every subclass repeating
    // static_cast<T*>(view_->findView(name)) once per resolved field. Returns nullptr if this
    // RootController has no view() (constructed with nullptr) or name isn't found.
    template<typename T>
    T* resolve(const std::string& name) {
        return view_ != nullptr ? static_cast<T*>(view_->findView(name)) : nullptr;
    }

private:
    RootView* view_ = nullptr;
};

}

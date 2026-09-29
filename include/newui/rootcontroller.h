#pragma once

#include <newui/controllers.h>
#include <newui/rootview.h>

#include <newui/reflection.h>

#include <string>
#include <typeindex>
#include <vector>

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

    // Before the first internal_init(), auto-binds every field of the most-derived class marked
    // `//@reflect connect=true` (e.g. `ProgressBar* progressBar_;`, private is fine) to the child
    // view named after it minus a trailing underscore (`"progressBar"`). Done here rather than in internal_init()
    // so it also happens when a subclass's override chains to Component::internal_init() directly,
    // and so the fields are already set when that override wires delegates.
    bool initialize() override {
        if (!isInitialized()) {
            bindViewFields();
        }
        return Controller::initialize();
    }

protected:
    // Named-lookup + cast, in one call instead of every subclass repeating
    // static_cast<T*>(view_->findView(name)) once per resolved field. Returns nullptr if this
    // RootController has no view() (constructed with nullptr) or name isn't found.
    template<typename T>
    T* resolve(const std::string& name) {
        return view_ != nullptr ? static_cast<T*>(view_->findView(name)) : nullptr;
    }

private:
    void bindViewFields() {
        if (view_ == nullptr) {
            return;
        }
        const reflection::Class* clazz = reflection::classinfo(std::type_index(typeid(*this)));
        const reflection::Class* viewClass = reflection::classinfo(std::type_index(typeid(View)));
        if (clazz == nullptr || viewClass == nullptr) {
            return;
        }
        std::vector<const reflection::Field*> fields;
        clazz->allFields(fields);
        for (const reflection::Field* field : fields) {
            if (!field->needsConnection()) {
                continue;
            }
            const reflection::Class* pointee = field->pointeeClass();
            if (pointee == nullptr || !pointee->isOrDerivesFrom(viewClass)) {
                continue;
            }
            std::string lookup = field->name();
            if (!lookup.empty() && lookup.back() == '_') {
                lookup.pop_back();
            }
            View* found = view_->findView(lookup);
            if (found == nullptr) {
                continue;
            }
            const reflection::Class* foundClass = reflection::classinfo(std::type_index(typeid(*found)));
            if (foundClass == nullptr || !foundClass->isOrDerivesFrom(pointee)) {
                continue;
            }
            // View has a single-inheritance chain, so the View* and the derived pointer share an
            // address; writing through the field's own storage avoids needing its static type.
            *static_cast<void**>(field->address(dynamic_cast<void*>(this))) = found;
        }
    }

    RootView* view_ = nullptr;
};

}

#pragma once

#include <string>
#include "newui/utils.h"


namespace json5 {
    class builder;
    class value;
}

namespace newui {

    

    // Shared serialization contract for every polymorphic type that can
    // appear as a JSON5 node: View (SubView/RootView), ViewStyle, Layout,
    // LayoutParams, Frame, Application. Pure interface, no data members -
    // each of those hierarchies inherits it individually (no diamond: none
    // of them share a common ancestor with each other), the same way each
    // already gets its own virtual paint()/arrange() contract.
    class UIComponent {
    public:
        virtual ~UIComponent() = default;

        // Registry-keyed name written as this node's "type" field on save,
        // and looked up in SerializationRegistry on load to find the right
        // default-constructing factory - e.g. "ButtonStyle", "StackLayout".
        // Not virtual and never overridden: typeid(*this) already reports
        // the most-derived dynamic type (UIComponent is polymorphic), so
        // one definition here covers every subclass automatically -
        // SerializationRegistry::registerType<T>() derives the same string
        // from typeid(T), so the write-time tag and the read-time registry
        // key can never drift apart from a typo'd literal.
        std::string typeName() const {
            return demangleTypeName(typeid(*this));
        }

        // Writes this object's own fields into w (the currently-open JSON5
        // object). Override, call the base class's writeFields() first
        // (same chaining convention as ViewStyle::paint()/
        // ButtonStyle::paint()), then add whatever this subclass adds.
        // Doesn't open/close the object itself, and doesn't handle nested
        // UIComponents (a View's style/layout/children) - the tree-walker
        // in serialization.cpp composes those, calling writeFields() only
        // for each node's own scalar fields.
        virtual void writeFields(json5::builder& w) const = 0;

        // Inverse of writeFields(): obj is this object's own JSON5 node.
        // Same chain-to-base-first convention.
        virtual void readFields(const json5::value& obj) = 0;
    };

}

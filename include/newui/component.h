#pragma once

#include <string>

namespace newui {

    // Common base for anything nameable/design-time-aware: View, Model,
    // Controller. Not a Delphi TComponent - just these two fields.
    // Non-virtual: no call site needs polymorphic dispatch here.
    class Component {
    public:
        virtual ~Component() = default;

        void setName(const std::string& name) { name_ = name; }
        std::string name() const { return name_; }

        // Transient runtime state, not persisted - see rootview.h's
        // hoveredSubView() etc. for the same ignore pattern.
        //@reflect ignore=true
        bool isDesignTime() const { return designTime_; }
        void setDesignTime(bool designTime) { designTime_ = designTime; }

    protected:
        std::string name_;
        bool designTime_ = false;
    };

}

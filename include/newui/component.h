#pragma once

#include <cstdint>
#include <string>

namespace newui {

    // Design-time state bits on a Component. Transient runtime state, never persisted.
    enum class DesignTimeFlags : std::uint32_t {
        None = 0,

        // Part of a document being edited in a designer: shown and edited rather than run
        // (no normal input, no focus).
        DesignTime = 1,

        // An implementation detail of the control that created it (e.g. a TabControl's tab strip
        // and buttons). A designer doesn't list it, drop onto it, or offer property or component
        // editors for it - the owning control manages it. Whether it can be selected is a separate
        // flag (NotSelectable).
        Internal = 2,

        // A real, user-visible part whose state the owning control manages: a designer still
        // shows and selects it, but offers no verbs and shows its properties read-only.
        ReadOnly = 4,

        // A designer never selects it: a click on it selects the nearest selectable ancestor
        // instead (a Slider's thumb, a TabControl's tab buttons). Independent of Internal - an
        // internal part may still be selectable, and vice versa.
        NotSelectable = 8,
    };

    constexpr DesignTimeFlags operator|(DesignTimeFlags a, DesignTimeFlags b) {
        return static_cast<DesignTimeFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
    }
    constexpr DesignTimeFlags operator&(DesignTimeFlags a, DesignTimeFlags b) {
        return static_cast<DesignTimeFlags>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
    }
    constexpr DesignTimeFlags operator~(DesignTimeFlags a) {
        return static_cast<DesignTimeFlags>(~static_cast<std::uint32_t>(a));
    }

    // Common base for anything nameable/design-time-aware: View, Model,
    // Controller. Not a Delphi TComponent - just a name and the design-time flags.
    // Non-virtual: no call site needs polymorphic dispatch here.
    class Component {
    public:
        virtual ~Component() = default;

        void setName(const std::string& name) { name_ = name; }
        std::string name() const { return name_; }

        // All of these are transient runtime state, not persisted - see rootview.h's
        // hoveredSubView() etc. for the same ignore pattern.
        //@reflect ignore=true
        DesignTimeFlags designTimeFlags() const { return designTimeFlags_; }
        //@reflect ignore=true
        void setDesignTimeFlags(DesignTimeFlags flags) { designTimeFlags_ = flags; }

        // True only if every bit of flag is set.
        //@reflect ignore=true
        bool hasDesignTimeFlag(DesignTimeFlags flag) const { return (designTimeFlags_ & flag) == flag; }
        //@reflect ignore=true
        void setDesignTimeFlag(DesignTimeFlags flag, bool on = true) {
            designTimeFlags_ = on ? (designTimeFlags_ | flag) : (designTimeFlags_ & ~flag);
        }

        // The DesignTime bit alone - the long-standing bool accessors (setDesignTime is also
        // invoked by name through reflection, see reflection.h).
        //@reflect ignore=true
        bool isDesignTime() const { return hasDesignTimeFlag(DesignTimeFlags::DesignTime); }
        void setDesignTime(bool designTime) { setDesignTimeFlag(DesignTimeFlags::DesignTime, designTime); }

        // One get/set pair per remaining flag - each touches only its own bit.
        //@reflect ignore=true
        bool isInternal() const { return hasDesignTimeFlag(DesignTimeFlags::Internal); }
        //@reflect ignore=true
        void setInternal(bool internal) { setDesignTimeFlag(DesignTimeFlags::Internal, internal); }

        //@reflect ignore=true
        bool isNotSelectable() const { return hasDesignTimeFlag(DesignTimeFlags::NotSelectable); }
        //@reflect ignore=true
        void setNotSelectable(bool notSelectable) { setDesignTimeFlag(DesignTimeFlags::NotSelectable, notSelectable); }

        //@reflect ignore=true
        bool isReadOnly() const { return hasDesignTimeFlag(DesignTimeFlags::ReadOnly); }
        //@reflect ignore=true
        void setReadOnly(bool readOnly) { setDesignTimeFlag(DesignTimeFlags::ReadOnly, readOnly); }

        // Reads better at a hit-test/selection call site than !isNotSelectable().
        //@reflect ignore=true
        bool isSelectableAtDesignTime() const { return !isNotSelectable(); }

    protected:
        std::string name_;
        DesignTimeFlags designTimeFlags_ = DesignTimeFlags::None;
    };

}

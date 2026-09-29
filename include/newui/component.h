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

    // Lifecycle state bits on a Component - transient, never persisted.
    enum class ComponentState : std::uint32_t {
        None = 0,
        // Being torn down (set on a whole subtree before any of it is detached): layout and
        // resize work is skipped, so listeners whose owners are already gone never run.
        Destroying = 1,
        // Set for the duration of internal_init() (see Component::initialize()) - lets
        // internal_init() itself (or anything it calls) tell "already fully initialized" apart
        // from "initialization is running right now" via componentState(), rather than only
        // ever seeing the pre- or post-state.
        Initializing = 2,
        // initialize() has already run internal_init() to completion once - isInitialized() below.
        Initialized = 3,
    };

    // Common base for anything nameable/design-time-aware: View, Model,
    // Controller. Not a Delphi TComponent - just a name and the design-time flags.
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

        //@reflect ignore=true
        ComponentState componentState() const { return componentState_; }
        //@reflect ignore=true
        bool isDestroying() const { return componentState_ == ComponentState::Destroying; }
        //@reflect ignore=true
        void setDestroying() { componentState_ = ComponentState::Destroying; }

        //@reflect ignore=true
        bool isInitialized() const { return componentState_ == ComponentState::Initialized; }

        // Idempotent - a no-op returning true once already initialized, so any caller (a
        // subtree cascade, a second explicit call, ...) can call this freely without tracking
        // who's already run it. internal_init() below is the real customization point - override
        // that, not this, so the guard here always applies regardless of what a subclass does.
        // RootView/SubView (view.h) still each declare their own initialize() too (matching how
        // Frame - an unrelated hierarchy - already spells this name), chaining to this one at
        // whatever point their own real setup succeeds.
        //@reflect ignore=true
        virtual bool initialize() {
            if (isInitialized()) {
                return true;
            }
            componentState_ = ComponentState::Initializing;
            bool result = internal_init();
            componentState_ = result ? ComponentState::Initialized : ComponentState::None;
            return result;
        }

    protected:
        // The standard place to wire this object's own delegates to handler methods (e.g.
        // saveButton_->onClick.add(this, &MyDialog::onSaveButtonClicked);) - guaranteed to run
        // exactly once, after construction, regardless of which concrete initialize() override
        // actually triggers it. Exists specifically so generated code (cpp_codetools's delegate-
        // wiring codegen, cpptools_codegen) has one predictable override to emit such a call
        // into, instead of guessing at a constructor or some other ad hoc spot. Always call
        // Component::internal_init() from an override, same as any other base-chaining override
        // in this codebase (see SubView::destroy()) - base is a no-op today but that's not
        // guaranteed to stay true.
        virtual bool internal_init() { return true; }

        std::string name_;
        DesignTimeFlags designTimeFlags_ = DesignTimeFlags::None;
        ComponentState componentState_ = ComponentState::None;
    };

}

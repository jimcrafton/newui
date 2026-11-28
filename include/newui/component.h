#pragma once

#include "newui/delegate.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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
    // Controller - and the unit of ownership, after Delphi's TComponent.
    //
    // A component may own others (addComponent / createOwned). free() on the
    // owner frees them, last added first. An owned component is a heap object
    // that is only ever freed through free() / release(): never `delete` it,
    // hold it in a unique_ptr, or put it on the stack. Ownership is opt-in: a
    // component nothing owns, and that owns nothing, behaves as any other C++
    // object. For a View, ownership is separate from the visual parent: a
    // parent being destroyed deletes the children nothing owns, as ever, and
    // only detaches the owned ones; destroying a view frees what it owns before
    // its children go. So the controllers a window's root view owns are gone
    // before any of its views (see FrameController in controllers.h).
    //
    // Ownership operations belong to one thread (the UI thread): a component
    // remembers the thread that first used them. lifetime() may be called
    // from any thread.
    class Component {
    public:
        // What notification() is being told about a component this one owns.
        enum class Operation {
            Inserted,
            Removing,
        };

        typedef Delegate<Component, Component&> OwnedComponentDelegate;
        typedef Delegate<Component> FreeingDelegate;

        Component() = default;
        // Copies the name and flags only: ownership, connections and lifetime are never copied.
        Component(const Component& other)
            : name_(other.name_), designTimeFlags_(other.designTimeFlags_), componentState_(other.componentState_) {}
        Component& operator=(const Component& other) {
            name_ = other.name_;
            designTimeFlags_ = other.designTimeFlags_;
            componentState_ = other.componentState_;
            return *this;
        }

        // Frees anything still owned, and leaves the owner, if the object is destroyed without free(). Both are mistakes (they
        // assert in debug builds): by then the derived class is already gone.
        virtual ~Component();

        // The component that frees this one, or null.
        //@reflect ignore=true
        Component* owner() const;
        // What this component owns, in the order it was added. Don't add or remove while iterating it; take a copy.
        //@reflect ignore=true
        const std::vector<Component*>& components() const;
        //@reflect ignore=true
        std::size_t componentCount() const { return components().size(); }
        // The first directly owned component with that name, or null.
        //@reflect ignore=true
        Component* findComponent(const std::string& name) const;

        // Takes ownership of child, moving it from any previous owner; nothing happens if this already owns it. Only ownership
        // changes: a view's visual parent is left alone. Refused (asserting in debug builds) for null, this, an owner of this
        // at any level, and when either is being destroyed.
        //@reflect ignore=true
        void addComponent(Component* child);
        // Gives child up without freeing it: the caller now owns it. Nothing happens if this doesn't own it.
        //@reflect ignore=true
        void removeComponent(Component* child);
        // newOwner->addComponent(this), or removal from the current owner when null.
        //@reflect ignore=true
        void setOwner(Component* newOwner);

        // new T(args...) owned by this: the normal way to make an owned component. Null if this is being destroyed.
        //@reflect ignore=true
        template<typename T, typename... A>
        T* createOwned(A&&... args) {
            T* child = new T(std::forward<A>(args)...);
            Component* base = child;
            addComponent(base);
            if (base->owner() != this) {
                delete child;
                return nullptr;
            }
            return child;
        }

        // Destroys this heap-allocated component now. In order: marks it and everything it owns Destroying; expires its
        // lifetime() and disconnects its connections(); calls beforeDestruction(); frees what it owns, last added first;
        // leaves its owner; deletes itself. A second call while that is under way does nothing. From inside the component's own
        // event handler use release().
        //@reflect ignore=true
        void free();
        // Marks this Destroying now and free()s it on the next turn of the calling thread's run loop. Calling it again does
        // nothing, as does the queued free if the component has been destroyed meanwhile.
        //@reflect ignore=true
        void release();

        // Expires when this component starts being freed (or is destroyed). Already expired if asked for after that.
        //@reflect ignore=true
        std::weak_ptr<const void> lifetime() const;

        // Asks for this component to be told when other goes: notification(other, Operation::Removing) is called as other
        // starts being freed or destroyed, while it is still whole. (If other is simply deleted, only its Component part
        // is left by then.) The registration ends when either of the two goes.
        //@reflect ignore=true
        void freeNotification(Component& other);
        // Fired as this component starts being freed or destroyed, after the components watching it have been told.
        // For listeners that are not components.
        //@reflect ignore=true
        FreeingDelegate& onFreeing();

        // Fired on the owner after a component is added to it, and before / as one leaves it.
        //@reflect ignore=true
        OwnedComponentDelegate& onComponentAdded();
        //@reflect ignore=true
        OwnedComponentDelegate& onComponentRemoved();

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

        // Called once, by free() or (for a View) destroy(), while the object is still whole and its owned components still
        // exist: stop timers, cancel work, unregister. connections() are already disconnected and lifetime() expired, so
        // finish the work here (cancel, then wait); no delegate, or posted completion that checks the lifetime, will reach
        // this object again.
        virtual void beforeDestruction() {}

        // Marks this and what goes with it Destroying. A View adds the child views that are not owned from outside.
        virtual void markDestroyingTree();
        // Called by release() before anything else, while the component still works normally.
        virtual void aboutToBeReleased() {}
        // Called by free() after the owned components are gone and before the object is deleted.
        virtual void tearDownForFree() {}
        // How each owned component is freed. An override may do something first (a Frame destroys its window before its root
        // view goes), but the component must have left this one's list by the time it returns.
        virtual void freeOwnedComponent(Component& component) { component.free(); }

        // Expires lifetime(), disconnects connections(), calls beforeDestruction() (all once only) and frees what this owns,
        // last added first. For a class that is torn down by something other than free().
        void destroyOwnedComponents();

        // True while free() is running on this component.
        bool isBeingFreed() const;

        // Makes the calling thread the one that ownership of this component, and of everything it owns, belongs to. For a
        // class whose thread is settled only after it owns something (a Frame: the thread that makes its window).
        void moveOwnershipToCurrentThread();

        // Told when a component is added to, or is leaving, the ones this owns, and (Removing) when one asked about with
        // freeNotification() goes; an owner that also asked is told once. Not called while this is itself being freed.
        virtual void notification(Component& component, Operation operation) {
            (void)component;
            (void)operation;
        }

        // Subscriptions made on this component's behalf, disconnected as soon as it starts being freed.
        Connections& connections();

        template<typename DelegateT>
        void connect(DelegateT& delegate, typename DelegateT::Callback fn) {
            connections().add(delegate, std::move(fn));
        }

        std::string name_;
        DesignTimeFlags designTimeFlags_ = DesignTimeFlags::None;
        ComponentState componentState_ = ComponentState::None;

    private:
        // Allocated only for a component that uses ownership, connections or its lifetime.
        struct Ownership;

        Ownership& ownership() const;
        Ownership* ownershipIfAny() const { return ownership_.load(std::memory_order_acquire); }
        void detachFromOwner();
        void announceFreeing();

        mutable std::atomic<Ownership*> ownership_{nullptr};
        // lifetime() has been expired and beforeDestruction() called.
        bool teardownStarted_ = false;
    };

}

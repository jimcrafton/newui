#include "newui/component.h"
#include "newui/runloop.h"

#include <algorithm>
#include <cassert>
#include <thread>

namespace newui {

    struct Component::Ownership {
        Component* owner = nullptr;
        std::vector<Component*> components;
        std::shared_ptr<const void> life = std::make_shared<char>();
        // Lasts exactly as long as the object, unlike life: what a queued release() checks.
        std::shared_ptr<const void> exists = std::make_shared<char>();
        Connections connections;
        std::unique_ptr<OwnedComponentDelegate> added;
        std::unique_ptr<OwnedComponentDelegate> removed;
        // Components to be told when this one goes, and those this one is to be told about.
        std::vector<Component*> watchers;
        std::vector<Component*> watching;
        std::unique_ptr<FreeingDelegate> freeingEvent;
        std::thread::id thread;
        bool freeing = false;
        bool releasePending = false;
        // The owner has already had its notification(Removing), as a watcher.
        bool ownerTold = false;

        // True on the thread that owns this component's ownership (the first one to ask).
        bool onOwningThread() {
            if (thread == std::thread::id()) {
                thread = std::this_thread::get_id();
            }
            return thread == std::this_thread::get_id();
        }
    };

    namespace {

        // A mistake by the caller: stops a debug build, ignored in a release build.
        void refuse(const char* why) {
            (void)why;
            assert(false && "newui::Component ownership misuse - see the 'why' argument");
        }

    }

    Component::Ownership& Component::ownership() const {
        Ownership* current = ownershipIfAny();
        if (current == nullptr) {
            auto made = std::make_unique<Ownership>();
            if (ownership_.compare_exchange_strong(current, made.get(), std::memory_order_acq_rel)) {
                current = made.release();
            }
        }
        return *current;
    }

    Component::~Component() {
        Ownership* data = ownershipIfAny();
        if (data == nullptr) {
            return;
        }
        if (!teardownStarted_) {
            teardownStarted_ = true;
            announceFreeing();
        }
        data->life.reset();
        data->connections.clear();
        if (data->owner != nullptr) {
            refuse("an owned component was deleted instead of freed");
            detachFromOwner();
        }
        if (!data->components.empty()) {
            refuse("a component that owns others was destroyed without free()");
            data->freeing = true;
            while (!data->components.empty()) {
                Component* child = data->components.back();
                if (child->isBeingFreed()) {
                    data->components.pop_back();
                    child->ownership().owner = nullptr;
                } else {
                    child->free();
                }
            }
        }
        delete data;
    }

    Component* Component::owner() const {
        const Ownership* data = ownershipIfAny();
        return data == nullptr ? nullptr : data->owner;
    }

    const std::vector<Component*>& Component::components() const {
        static const std::vector<Component*> none;
        const Ownership* data = ownershipIfAny();
        return data == nullptr ? none : data->components;
    }

    Component* Component::findComponent(const std::string& name) const {
        for (Component* child : components()) {
            if (child->name_ == name) {
                return child;
            }
        }
        return nullptr;
    }

    bool Component::isBeingFreed() const {
        const Ownership* data = ownershipIfAny();
        return data != nullptr && data->freeing;
    }

    void Component::addComponent(Component* child) {
        if (child == nullptr) {
            refuse("addComponent(nullptr)");
            return;
        }
        for (const Component* above = this; above != nullptr; above = above->owner()) {
            if (above == child) {
                refuse("a component cannot own itself or one of its owners");
                return;
            }
        }
        if (isDestroying() || child->isDestroying() || isBeingFreed() || child->isBeingFreed()) {
            refuse("addComponent on a component that is being destroyed");
            return;
        }
        if (!ownership().onOwningThread() || !child->ownership().onOwningThread()) {
            refuse("component ownership used from another thread");
            return;
        }
        if (child->owner() == this) {
            return;
        }
        if (child->owner() != nullptr) {
            child->owner()->removeComponent(child);
        }
        ownership().components.push_back(child);
        child->ownership().owner = this;
        notification(*child, Operation::Inserted);
        if (ownership().added) {
            ownership().added->syncCall(*this, *child);
        }
    }

    void Component::removeComponent(Component* child) {
        if (child == nullptr || child->owner() != this) {
            return;
        }
        if (isBeingFreed()) {
            refuse("removeComponent while the owner is freeing its components");
            return;
        }
        if (!ownership().onOwningThread()) {
            refuse("component ownership used from another thread");
            return;
        }
        child->detachFromOwner();
    }

    void Component::detachFromOwner() {
        Ownership& mine = ownership();
        Component* const previous = mine.owner;
        if (previous == nullptr) {
            return;
        }
        Ownership& theirs = previous->ownership();
        theirs.components.erase(std::remove(theirs.components.begin(), theirs.components.end(), this), theirs.components.end());
        mine.owner = nullptr;
        // An owner on its way out has already dropped its listeners, and part of it may be gone.
        if (!theirs.freeing) {
            if (!mine.ownerTold) {
                previous->notification(*this, Operation::Removing);
            }
            if (theirs.removed) {
                theirs.removed->syncCall(*previous, *this);
            }
        }
    }

    void Component::setOwner(Component* newOwner) {
        if (newOwner != nullptr) {
            newOwner->addComponent(this);
        } else if (owner() != nullptr) {
            owner()->removeComponent(this);
        }
    }

    void Component::markDestroyingTree() {
        setDestroying();
        for (Component* child : components()) {
            child->markDestroyingTree();
        }
    }

    void Component::destroyOwnedComponents() {
        if (!teardownStarted_) {
            teardownStarted_ = true;
            announceFreeing();
            if (Ownership* data = ownershipIfAny()) {
                data->life.reset();
                data->connections.clear();
            }
            beforeDestruction();
        }
        Ownership* data = ownershipIfAny();
        while (data != nullptr && !data->components.empty()) {
            Component* child = data->components.back();
            if (child->isBeingFreed()) {
                // It is already being freed further up the stack (this call came from inside that): let it finish by itself.
                data->components.pop_back();
                child->ownership().owner = nullptr;
            } else {
                child->free();
            }
        }
    }

    void Component::free() {
        Ownership& data = ownership();
        if (data.freeing) {
            return;
        }
        if (!data.onOwningThread()) {
            refuse("component freed from another thread");
            return;
        }
        data.freeing = true;
        markDestroyingTree();
        destroyOwnedComponents();
        tearDownForFree();
        detachFromOwner();
        delete this;
    }

    void Component::release() {
        Ownership& data = ownership();
        if (data.freeing || data.releasePending) {
            return;
        }
        if (!data.onOwningThread()) {
            refuse("component released from another thread");
            return;
        }
        data.releasePending = true;
        aboutToBeReleased();
        markDestroyingTree();
        std::weak_ptr<const void> alive = data.exists;
        RunLoop::current().post([this, alive]() {
            if (!alive.expired()) {
                free();
            }
        });
    }

    std::weak_ptr<const void> Component::lifetime() const {
        if (teardownStarted_) {
            return std::weak_ptr<const void>();
        }
        return ownership().life;
    }

    Connections& Component::connections() {
        return ownership().connections;
    }

    void Component::freeNotification(Component& other) {
        if (&other == this || teardownStarted_ || other.teardownStarted_) {
            return;
        }
        if (!ownership().onOwningThread() || !other.ownership().onOwningThread()) {
            refuse("freeNotification used from another thread");
            return;
        }
        std::vector<Component*>& watching = ownership().watching;
        if (std::find(watching.begin(), watching.end(), &other) != watching.end()) {
            return;
        }
        watching.push_back(&other);
        other.ownership().watchers.push_back(this);
    }

    void Component::announceFreeing() {
        Ownership* data = ownershipIfAny();
        if (data == nullptr) {
            return;
        }
        // One at a time from the live list: a handler may free another watcher, which then takes itself off it.
        while (!data->watchers.empty()) {
            Component* watcher = data->watchers.front();
            data->watchers.erase(data->watchers.begin());
            std::vector<Component*>& theirs = watcher->ownership().watching;
            theirs.erase(std::remove(theirs.begin(), theirs.end(), this), theirs.end());
            if (!watcher->teardownStarted_) {
                if (watcher == data->owner) {
                    data->ownerTold = true;
                }
                watcher->notification(*this, Operation::Removing);
            }
        }
        if (data->freeingEvent) {
            data->freeingEvent->syncCall(*this);
        }
        for (Component* watched : data->watching) {
            std::vector<Component*>& theirs = watched->ownership().watchers;
            theirs.erase(std::remove(theirs.begin(), theirs.end(), this), theirs.end());
        }
        data->watching.clear();
    }

    Component::FreeingDelegate& Component::onFreeing() {
        Ownership& data = ownership();
        if (!data.freeingEvent) {
            data.freeingEvent = std::make_unique<FreeingDelegate>();
        }
        return *data.freeingEvent;
    }

    Component::OwnedComponentDelegate& Component::onComponentAdded() {
        Ownership& data = ownership();
        if (!data.added) {
            data.added = std::make_unique<OwnedComponentDelegate>();
        }
        return *data.added;
    }

    Component::OwnedComponentDelegate& Component::onComponentRemoved() {
        Ownership& data = ownership();
        if (!data.removed) {
            data.removed = std::make_unique<OwnedComponentDelegate>();
        }
        return *data.removed;
    }

}

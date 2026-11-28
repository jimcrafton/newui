#pragma once

#include "newui/component.h"

#include <cassert>
#include <memory>
#include <type_traits>

namespace newui {

    // A pointer to a component that becomes null when the component is freed
    // or destroyed, instead of needing an "set it to null in onDestroyed"
    // handler. It does not own the component.
    template<typename T>
    class ComponentRef {
    public:
        ComponentRef() = default;
        ComponentRef(T* target) { reset(target); }

        ComponentRef& operator=(T* target) {
            reset(target);
            return *this;
        }

        void reset(T* target = nullptr) {
            static_assert(std::is_base_of<Component, T>::value, "ComponentRef is for classes derived from Component");
            target_ = target;
            // Through the base: a subclass may have a lifetime() of its own that means something else.
            life_ = target == nullptr ? std::weak_ptr<const void>() : static_cast<const Component*>(target)->lifetime();
        }

        T* get() const {
            return life_.expired() ? nullptr : target_;
        }

        T* operator->() const {
            T* target = get();
            assert(target != nullptr && "ComponentRef used after its component was freed");
            return target;
        }

        T& operator*() const {
            return *operator->();
        }

        explicit operator bool() const {
            return get() != nullptr;
        }

    private:
        T* target_ = nullptr;
        std::weak_ptr<const void> life_;
    };

}

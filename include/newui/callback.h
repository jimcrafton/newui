#pragma once

#include "newui/component.h"

#include <cassert>
#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace newui {

    template<typename Signature>
    class Callback;

    // One listener that gives a value back - what a Delegate cannot do. It is
    // installed on behalf of a component and counts as unset from the moment
    // that component starts being freed or destroyed, so nobody has to clear
    // it by hand. (What the function captured is released only when the
    // Callback is set again, cleared or destroyed.)
    //
    //   Callback<std::string(int)> describe;
    //   describe.set(*this, [this](int row) { return rowText(row); });
    //   std::string text = describe.call("-", 3);   // "-" when unset
    template<typename R, typename... Args>
    class Callback<R(Args...)> {
    public:
        // Installs fn, replacing any earlier one. An empty fn, or an owner that is already being destroyed, leaves it unset.
        void set(Component& owner, std::function<R(Args...)> fn) {
            clear();
            if (fn) {
                owner_ = static_cast<const Component&>(owner).lifetime();
                fn_ = std::make_shared<const std::function<R(Args...)>>(std::move(fn));
            }
        }

        void clear() {
            owner_.reset();
            fn_.reset();
        }

        // Set, and its owner is still there.
        explicit operator bool() const {
            return fn_ != nullptr && !owner_.expired();
        }

        // For a callback known to be set: calling an unset one is a mistake. It asserts in debug builds, then returns R()
        // or, for a result with no default, throws std::bad_function_call.
        R operator()(Args... args) const {
            if (*this) {
                // Held for the call: fn may set or clear this very callback.
                const auto held = fn_;
                return (*held)(std::forward<Args>(args)...);
            }
            assert(false && "calling an unset newui::Callback");
            if constexpr (std::is_void<R>::value) {
                return;
            } else if constexpr (std::is_default_constructible<R>::value) {
                return R();
            } else {
                throw std::bad_function_call();
            }
        }

        // The result, or fallback when unset or the owner has gone. (Not for a callback returning void.)
        template<typename Fallback>
        R call(Fallback&& fallback, Args... args) const {
            if (*this) {
                const auto held = fn_;
                return (*held)(std::forward<Args>(args)...);
            }
            return std::forward<Fallback>(fallback);
        }

    private:
        std::weak_ptr<const void> owner_;
        std::shared_ptr<const std::function<R(Args...)>> fn_;
    };

}

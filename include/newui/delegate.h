#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace newui {

    class SyncReturn {
    public:
		enum ReturnCode {
			Handled,
			Ignored,
            Error
		};

		SyncReturn(ReturnCode val) : val_(val) {}

		SyncReturn& operator=(ReturnCode val) {
			val_ = val;
			return *this;
		}


		ReturnCode get() const {
			return val_;
		}

		operator ReturnCode() const {
			return val_;
		}

		operator bool() const {
			return val_ == Handled;
		}

		bool handled() const {
			return val_ == Handled;
		}

		bool ignored() const {
			return val_ == Ignored;
		}

		bool error() const {
			return val_ == Error;
		}

	private:
        ReturnCode val_;
    };

class RunLoop; // Forward declaration; include newui/runloop.h to call postCall.

// Handle returned by Delegate::postCall. The caller can wait() (or
// waitFor()/isDone()) on it to find out when the posted callback batch has
// finished running on the RunLoop.
class AsyncReturn {
public:
    AsyncReturn() : state_(std::make_shared<State>()) {}

    void wait() const {
        std::unique_lock<std::mutex> lock(state_->mutex);
        state_->cv.wait(lock, [this] { return state_->done; });
    }

    template<typename Rep, typename Period>
    bool waitFor(const std::chrono::duration<Rep, Period>& timeout) const {
        std::unique_lock<std::mutex> lock(state_->mutex);
        return state_->cv.wait_for(lock, timeout, [this] { return state_->done; });
    }

    bool isDone() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->done;
    }

    SyncReturn result() const {
        std::lock_guard<std::mutex> lock(state_->mutex);
        return state_->result;
    }

    // Called by the task Delegate::postCall queues onto the RunLoop once the
    // callback batch finishes; not meant to be called by consumers of the
    // AsyncReturn they got back from postCall.
    void complete(SyncReturn result) const {
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            state_->result = result;
            state_->done = true;
        }
        state_->cv.notify_all();
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        SyncReturn result{SyncReturn::Ignored};
    };

    std::shared_ptr<State> state_;
};

// Opaque handle returned by Delegate::add, used to remove() that specific
// listener later. A default-constructed Connection is never connected
// (remove() on it is a no-op) - useful as a "not subscribed yet" sentinel.
class Connection {
public:
    Connection() = default;

    bool connected() const {
        return id_ != 0;
    }

    explicit operator bool() const {
        return connected();
    }

private:
    template<typename, typename...> friend class Delegate;

    explicit Connection(std::uint64_t id) : id_(id) {}

    std::uint64_t id_ = 0;
};

namespace detail {
// A single counter shared by every Delegate<...> specialization: it only
// needs to hand out ids that are unique within one Delegate's slot list,
// and a process-wide monotonic counter trivially satisfies that. Keeping
// it out of Delegate itself (rather than a std::atomic member) means
// Delegate stays copyable - it holds nothing but a shared_ptr, same as
// before - which several owning classes (e.g. MenuItem) rely on via their
// own implicitly-generated copy assignment.
inline std::uint64_t nextConnectionId() {
    static std::atomic<std::uint64_t> counter{0};
    return counter.fetch_add(1, std::memory_order_relaxed) + 1;
}
}  // namespace detail

// Lock-free multicast delegate. Readers (syncCall) take an atomic snapshot
// of an immutable listener list, so they never block and never see a list
// mutated mid-iteration. Writers (add/remove) build a new immutable list
// and publish it with a compare-exchange retry loop instead of a mutex.
//
// Sender is the type of the instance invoking syncCall (typically passed as
// `*this` by the owning class), and is always the first parameter every
// listener receives.
//
// add() accepts anything callable as SyncReturn(SenderRefT, Args...): a
// free function, a capturing or non-capturing lambda, or (instance, &T::
// Method) for a member function - and returns a Connection to remove() it
// later. Listeners are type-erased with std::function, so unlike a plain
// function pointer they can't be compared for equality; that's why removal
// goes through the Connection token instead of by value.
template<typename SenderT, typename... Args>
class Delegate {
    static_assert(!std::is_reference<SenderT>::value,
        "Delegate's Sender type must be the plain instance type, not a reference "
        "(use Delegate<View>, not Delegate<View&> - the delegate adds & or * itself).");

public:
	using SenderRefT = SenderT&;
    using SenderPtrT = SenderT*;

    using FunctionPtr = SyncReturn(*)(SenderRefT, Args...);
    using Callback = std::function<SyncReturn(SenderRefT, Args...)>;

    Delegate() : slots_(std::make_shared<const SlotList>()) {}

    // Accepts a free function, a lambda (capturing or not), or any other
    // SyncReturn(SenderRefT, Args...)-callable.
    Connection add(Callback fn) {
        if (!fn) {
            return Connection();
        }
        std::uint64_t id = detail::nextConnectionId();
        addSlot(Slot{id, std::move(fn)});
        return Connection(id);
    }

    // Overload kept distinct from add(Callback) so &SomeTemplateFn resolves
    // against a concrete function-pointer type first - taking the address
    // of an overloaded/template function only works when the target type is
    // a specific function pointer, not when it's a class type like
    // std::function that would have to deduce which overload to convert.
    Connection add(FunctionPtr fn) {
        if (fn == nullptr) {
            return Connection();
        }
        return add(Callback(fn));
    }

    // Binds a non-const member function on instance, e.g.
    //   delegate.add(&logger, &Logger::onChanged);
    template<typename T>
    Connection add(T* instance, SyncReturn (T::*method)(SenderRefT, Args...)) {
        if (instance == nullptr || method == nullptr) {
            return Connection();
        }
        return add([instance, method](SenderRefT sender, Args... args) {
            return (instance->*method)(sender, args...);
        });
    }

    // Same as above, for a const member function bound to a const instance.
    template<typename T>
    Connection add(const T* instance, SyncReturn (T::*method)(SenderRefT, Args...) const) {
        if (instance == nullptr || method == nullptr) {
            return Connection();
        }
        return add([instance, method](SenderRefT sender, Args... args) {
            return (instance->*method)(sender, args...);
        });
    }

	Delegate& operator+=(Callback fn) {
		add(std::move(fn));
		return *this;
	}

    void remove(Connection connection) {
        if (!connection) {
            return;
        }
        removeSlot(connection.id_);
    }

    void syncCall(SenderRefT sender, Args... args) const {
        std::shared_ptr<const SlotList> snapshot = std::atomic_load(&slots_);
        for (const Slot& slot : *snapshot) {
            auto result = slot.fn(sender, args...);
			if (result.error()) {
				return;
			}
        }
    }

	void operator()(SenderRefT sender, Args... args) const {
		syncCall(sender, args...);
	}

    SyncReturn syncCallFirst(SenderRefT sender, Args... args) const {
        std::shared_ptr<const SlotList> snapshot = std::atomic_load(&slots_);
        for (const Slot& slot : *snapshot) {
            auto result = slot.fn(sender, args...);
            if (result.handled()) {
                return result;
            }
        }
        return SyncReturn(SyncReturn::Ignored);
    }

    // Returns immediately. The current list of listeners is snapshotted now
    // and posted onto runLoop as a single deferred task, so the callbacks
    // run later, in order, on whatever thread is running runLoop.run() -
    // no thread is spawned. The returned AsyncReturn can be waited on for
    // that task to finish; its result() is the same as syncCall's early-out
    // semantics would produce (last SyncReturn, or the one that errored).
    AsyncReturn postCall(RunLoop& runLoop, SenderPtrT sender, Args... args) const {
        std::shared_ptr<const SlotList> snapshot = std::atomic_load(&slots_);
        AsyncReturn asyncReturn;
        runLoop.post([snapshot, asyncReturn, sender, args...]() {
            SyncReturn result(SyncReturn::Ignored);
            for (const Slot& slot : *snapshot) {
                result = slot.fn(*sender, args...);
                if (result.error()) {
                    break;
                }
            }
            asyncReturn.complete(result);
        });
        return asyncReturn;
    }

private:
    struct Slot {
        std::uint64_t id;
        Callback fn;
    };

    using SlotList = std::vector<Slot>;

    // slots_ is a plain (non-atomic) shared_ptr, so ordinary assignment
    // from multiple threads would be a data race - atomic_load/
    // atomic_compare_exchange_weak are what make access to that one
    // shared_ptr variable itself thread-safe. The retry loop is needed on
    // top of that: without it, two concurrent add()/remove() calls could
    // both read the same oldList, each build their own updated copy, and
    // whichever atomic_store'd second would silently discard the other's
    // change. The CAS only publishes newList if slots_ is still oldList;
    // if another thread published first, it fails, refreshes oldList to
    // the current value, and the loop rebuilds updated on top of that -
    // so no update is ever lost. In the uncontended case (the common one)
    // the CAS succeeds first try and the loop runs once.
    void addSlot(Slot slot) {
        std::shared_ptr<const SlotList> oldList = std::atomic_load(&slots_);
        std::shared_ptr<const SlotList> newList;
        do {
            auto updated = std::make_shared<SlotList>(*oldList);
            updated->push_back(slot);
            newList = std::move(updated);
        } while (!std::atomic_compare_exchange_weak(&slots_, &oldList, newList));
    }

    // Same retry-on-conflict scheme as addSlot - see the comment there.
    void removeSlot(std::uint64_t id) {
        std::shared_ptr<const SlotList> oldList = std::atomic_load(&slots_);
        std::shared_ptr<const SlotList> newList;
        do {
            auto updated = std::make_shared<SlotList>(*oldList);
            updated->erase(std::remove_if(updated->begin(), updated->end(),
                                           [id](const Slot& slot) { return slot.id == id; }),
                           updated->end());
            newList = std::move(updated);
        } while (!std::atomic_compare_exchange_weak(&slots_, &oldList, newList));
    }

    std::shared_ptr<const SlotList> slots_;
};



}

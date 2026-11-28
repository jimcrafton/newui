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
#include <utility>
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
class Component; // newui/component.h; postCall treats a sender derived from it specially.

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

namespace detail {
// What a Connection needs from the delegate it came from, without knowing the delegate's type.
struct DelegateStateBase {
    virtual void remove(std::uint64_t id) = 0;

protected:
    ~DelegateStateBase() = default;
};
}  // namespace detail

// Handle returned by Delegate::add for that specific listener: pass it to
// the delegate's remove(), or call disconnect() on it. It holds the
// delegate's state only weakly, so disconnect() is safe after the delegate
// has been destroyed (it then does nothing). A default-constructed
// Connection is never connected (remove() and disconnect() on it are
// no-ops) - useful as a "not subscribed yet" sentinel. Copies refer to the
// same listener. connected() says the handle names a listener, not that the
// listener is still there.
class Connection {
public:
    Connection() = default;

    bool connected() const {
        return id_ != 0;
    }

    explicit operator bool() const {
        return connected();
    }

    // Removes the listener from the delegate it was added to (or the one that delegate was moved into), and forgets it.
    void disconnect() {
        const std::uint64_t id = std::exchange(id_, 0);
        std::weak_ptr<detail::DelegateStateBase> weak;
        weak.swap(state_);
        if (id != 0) {
            if (std::shared_ptr<detail::DelegateStateBase> state = weak.lock()) {
                state->remove(id);
            }
        }
    }

private:
    template<typename, typename...> friend class Delegate;

    Connection(std::uint64_t id, std::weak_ptr<detail::DelegateStateBase> state) : id_(id), state_(std::move(state)) {}

    std::uint64_t id_ = 0;
    std::weak_ptr<detail::DelegateStateBase> state_;
};

// A Connection that disconnects itself when it is destroyed or assigned over.
class ScopedConnection {
public:
    ScopedConnection() = default;
    ScopedConnection(Connection connection) : connection_(std::move(connection)) {}
    ~ScopedConnection() { disconnect(); }

    ScopedConnection(const ScopedConnection&) = delete;
    ScopedConnection& operator=(const ScopedConnection&) = delete;

    ScopedConnection(ScopedConnection&& other) noexcept : connection_(std::exchange(other.connection_, Connection())) {}

    ScopedConnection& operator=(ScopedConnection&& other) noexcept {
        if (this != &other) {
            Connection incoming = std::exchange(other.connection_, Connection());
            disconnect();
            connection_ = std::move(incoming);
        }
        return *this;
    }

    bool connected() const {
        return connection_.connected();
    }

    explicit operator bool() const {
        return connected();
    }

    void disconnect() {
        connection_.disconnect();
    }

private:
    Connection connection_;
};

namespace detail {
// A single counter shared by every Delegate<...> specialization: it only
// needs to hand out ids that are unique within one Delegate's slot list,
// and a process-wide monotonic counter trivially satisfies that.
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
//
// A removed listener is not called again, even by a syncCall already under
// way or a postCall already queued; a queued postCall calls nothing if the
// delegate has been destroyed, or if its sender is a Component that has been
// freed or is Destroying. A copy gets its own listener list, starting
// with the same listeners: adding or removing on one leaves the other alone,
// and a Connection's disconnect() acts on the delegate it was added to. A
// move takes the listeners and their Connections with it and leaves the
// source empty.
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

    Delegate() : state_(std::make_shared<State>()) {}

    Delegate(const Delegate& other) : state_(std::make_shared<State>()) {
        state_->slots = copiedSlots(*other.state_);
    }

    Delegate& operator=(const Delegate& other) {
        if (this != &other) {
            state_->replace(copiedSlots(*other.state_));
        }
        return *this;
    }

    Delegate(Delegate&& other) noexcept : state_(std::exchange(other.state_, std::make_shared<State>())) {}

    Delegate& operator=(Delegate&& other) noexcept {
        if (this != &other) {
            state_ = std::exchange(other.state_, std::make_shared<State>());
        }
        return *this;
    }

    // Accepts a free function, a lambda (capturing or not), or any other
    // SyncReturn(SenderRefT, Args...)-callable. Not describable - see
    // add(descriptor, ...)'s own comment - so invisible to
    // reflection::Delegate::describedListeners() (newui/reflection.h),
    // and thus never written out by ObjectWriter's own "delegates" block
    // (reflectionio.h).
    Connection add(Callback fn) {
        return add(std::string(), std::move(fn));
    }

    // Same as add(Callback), but tags the connection with a `descriptor` -
    // an opaque string reflection::Delegate::describedListeners() (see
    // newui/reflection.h) hands back verbatim, and ObjectWriter's
    // "delegates" block (reflectionio.h) writes verbatim into the array
    // for this delegate's own key. A std::function type-erases its
    // target, so there's no way to recover a name from an arbitrary
    // connection after the fact - the descriptor has to be supplied here,
    // at connect time, by whoever already knows what this connection
    // *is* (typically ObjectReader reconnecting a "<object>@<Class>.
    // <method>" entry it just read, or application code that wants this
    // exact connection to round-trip through a future write()). An empty
    // descriptor (or the plain, undescribed add(Callback) above) means
    // "connect, but don't serialize this" - the common case for a
    // same-process-lifetime lambda/closure that isn't meant to survive a
    // save/load round-trip at all.
    Connection add(std::string descriptor, Callback fn) {
        if (!fn) {
            return Connection();
        }
        std::uint64_t id = detail::nextConnectionId();
        state_->append(Slot{id, std::make_shared<std::atomic<bool>>(true), std::move(fn), std::move(descriptor)});
        return Connection(id, state_);
    }

    // Overload kept distinct from add(Callback) so &SomeTemplateFn resolves
    // against a concrete function-pointer type first - taking the address
    // of an overloaded/template function only works when the target type is
    // a specific function pointer, not when it's a class type like
    // std::function that would have to deduce which overload to convert.
    Connection add(FunctionPtr fn) {
        return add(std::string(), fn);
    }

    // Same as add(FunctionPtr), but describable - see add(descriptor,
    // Callback)'s own comment. `descriptor` is typically just fn's own
    // name (e.g. "HappyChanged") - reconnecting a bare name like that
    // from a saved file isn't supported yet (ObjectReader::readObjects()'s
    // own comment, reflectionio.h), so this is write-only for now: the
    // name round-trips into the output, reading it back in a fresh
    // process doesn't yet reconnect it.
    Connection add(std::string descriptor, FunctionPtr fn) {
        if (fn == nullptr) {
            return Connection();
        }
        return add(std::move(descriptor), Callback(fn));
    }

    // Binds a non-const member function on instance, e.g.
    //   delegate.add(&logger, &Logger::onChanged);
    template<typename T>
    Connection add(T* instance, SyncReturn (T::*method)(SenderRefT, Args...)) {
        return add(std::string(), instance, method);
    }

    // Same as above, but describable - see add(descriptor, Callback)'s
    // own comment. This is the overload ObjectReader::readObjects()
    // (reflectionio.h) itself ends up calling indirectly, through
    // reflection::TypedDelegate<SourceT,Args...>::connectListener()
    // (newui/reflection.h), to reconnect a "<object>@<Class>.<method>"
    // entry read back from a file.
    template<typename T>
    Connection add(std::string descriptor, T* instance, SyncReturn (T::*method)(SenderRefT, Args...)) {
        if (instance == nullptr || method == nullptr) {
            return Connection();
        }
        return add(std::move(descriptor), [instance, method](SenderRefT sender, Args... args) {
            return (instance->*method)(sender, args...);
        });
    }

    // Same as add(T*, method), for a const member function bound to a
    // const instance.
    template<typename T>
    Connection add(const T* instance, SyncReturn (T::*method)(SenderRefT, Args...) const) {
        return add(std::string(), instance, method);
    }

    // Same as above, but describable - see add(descriptor, Callback)'s
    // own comment.
    template<typename T>
    Connection add(std::string descriptor, const T* instance, SyncReturn (T::*method)(SenderRefT, Args...) const) {
        if (instance == nullptr || method == nullptr) {
            return Connection();
        }
        return add(std::move(descriptor), [instance, method](SenderRefT sender, Args... args) {
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
        state_->remove(connection.id_);
    }

    // Disconnects every listener tagged with descriptor (see add(descriptor, Callback)). An empty
    // descriptor removes nothing - undescribed listeners can't be told apart.
    void removeDescribed(const std::string& descriptor) {
        if (!descriptor.empty()) {
            state_->removeIf([&descriptor](const Slot& slot) { return slot.descriptor == descriptor; });
        }
    }

    void syncCall(SenderRefT sender, Args... args) const {
        std::shared_ptr<const SlotList> snapshot = std::atomic_load(&state_->slots);
        for (const Slot& slot : *snapshot) {
            if (!slot.isActive()) {
                continue;
            }
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
        std::shared_ptr<const SlotList> snapshot = std::atomic_load(&state_->slots);
        for (const Slot& slot : *snapshot) {
            if (!slot.isActive()) {
                continue;
            }
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
        std::shared_ptr<const SlotList> snapshot = std::atomic_load(&state_->slots);
        std::weak_ptr<State> delegate = state_;
        std::weak_ptr<const void> senderLife;
        if constexpr (std::is_base_of<Component, SenderT>::value) {
            // Named through a dependent type, as Component is only declared here.
            const std::conditional_t<true, Component, SenderT>* component = sender;
            senderLife = component->lifetime();
        }
        AsyncReturn asyncReturn;
        runLoop.post([snapshot, delegate, senderLife, asyncReturn, sender, args...]() {
            SyncReturn result(SyncReturn::Ignored);
            for (const Slot& slot : *snapshot) {
                if (delegate.expired()) {
                    break;
                }
                if constexpr (std::is_base_of<Component, SenderT>::value) {
                    // The lifetime is checked first: the sender may no longer exist.
                    if (senderLife.expired()) {
                        break;
                    }
                    const std::conditional_t<true, Component, SenderT>* component = sender;
                    if (component->isDestroying()) {
                        break;
                    }
                }
                if (!slot.isActive()) {
                    continue;
                }
                result = slot.fn(*sender, args...);
                if (result.error()) {
                    break;
                }
            }
            asyncReturn.complete(result);
        });
        return asyncReturn;
    }

public:
    // True if nothing is currently connected - checkable without invoking
    // anything, e.g. so a caller can decide whether it's worth doing real
    // work to build the arguments a call would need (COMDropTarget's own
    // per-View drop negotiation, dragndrop.cpp, checks this against
    // View::dropTarget()'s delegates before bothering to extract a
    // payload that's about to go nowhere).
    bool empty() const {
        return std::atomic_load(&state_->slots)->empty();
    }

    // Every currently-connected slot's own descriptor, skipping any that
    // connected through an undescribed add() (empty descriptor - see
    // add(descriptor, Callback)'s own comment) - what
    // newui::reflection::TypedDelegate<SourceT,Args...>::describedListeners()
    // (newui/reflection.h) hands back verbatim for ObjectWriter's
    // "delegates" block (reflectionio.h) to write out. Snapshots slots_
    // the same lock-free way syncCall() does.
    std::vector<std::string> describedListeners() const {
        std::shared_ptr<const SlotList> snapshot = std::atomic_load(&state_->slots);
        std::vector<std::string> result;
        for (const Slot& slot : *snapshot) {
            if (!slot.descriptor.empty()) {
                result.push_back(slot.descriptor);
            }
        }
        return result;
    }

private:
    struct Slot {
        std::uint64_t id;
        // Shared by every snapshot holding this listener, so removing it reaches the calls already under way.
        std::shared_ptr<std::atomic<bool>> active;
        Callback fn;
        std::string descriptor;  // empty - not describable, see add(descriptor, Callback)

        bool isActive() const {
            return active->load(std::memory_order_acquire);
        }

        void deactivate() const {
            active->store(false, std::memory_order_release);
        }
    };

    using SlotList = std::vector<Slot>;

    // One empty list for every delegate nothing has been added to (lists are never changed in place).
    static const std::shared_ptr<const SlotList>& emptySlots() {
        static const std::shared_ptr<const SlotList> empty = std::make_shared<const SlotList>();
        return empty;
    }

    // The listener list lives apart from the Delegate so that a Connection
    // can refer to it weakly and outlive the delegate.
    //
    // slots is a plain (non-atomic) shared_ptr, so ordinary assignment
    // from multiple threads would be a data race - atomic_load/
    // atomic_compare_exchange_weak are what make access to that one
    // shared_ptr variable itself thread-safe. The retry loop is needed on
    // top of that: without it, two concurrent add()/remove() calls could
    // both read the same oldList, each build their own updated copy, and
    // whichever atomic_store'd second would silently discard the other's
    // change. The CAS only publishes newList if slots is still oldList;
    // if another thread published first, it fails, refreshes oldList to
    // the current value, and the loop rebuilds updated on top of that -
    // so no update is ever lost. In the uncontended case (the common one)
    // the CAS succeeds first try and the loop runs once.
    struct State final : detail::DelegateStateBase {
        std::shared_ptr<const SlotList> slots = emptySlots();

        void append(Slot slot) {
            std::shared_ptr<const SlotList> oldList = std::atomic_load(&slots);
            std::shared_ptr<const SlotList> newList;
            do {
                auto updated = std::make_shared<SlotList>(*oldList);
                updated->push_back(slot);
                newList = std::move(updated);
            } while (!std::atomic_compare_exchange_weak(&slots, &oldList, newList));
        }

        void remove(std::uint64_t id) override {
            removeIf([id](const Slot& slot) { return slot.id == id; });
        }

        // Same retry-on-conflict scheme as append. The removed listeners are deactivated once the new list is published.
        template<typename Predicate>
        void removeIf(Predicate matches) {
            std::shared_ptr<const SlotList> oldList = std::atomic_load(&slots);
            std::shared_ptr<const SlotList> newList;
            do {
                auto updated = std::make_shared<SlotList>(*oldList);
                updated->erase(std::remove_if(updated->begin(), updated->end(), matches), updated->end());
                if (updated->size() == oldList->size()) {
                    return;
                }
                newList = std::move(updated);
            } while (!std::atomic_compare_exchange_weak(&slots, &oldList, newList));
            for (const Slot& slot : *oldList) {
                if (matches(slot)) {
                    slot.deactivate();
                }
            }
        }

        void replace(std::shared_ptr<const SlotList> newList) {
            std::shared_ptr<const SlotList> oldList = std::atomic_exchange(&slots, std::move(newList));
            for (const Slot& slot : *oldList) {
                slot.deactivate();
            }
        }
    };

    // The other delegate's listeners with flags of their own, so that removing one from a copy leaves the original calling it.
    static std::shared_ptr<const SlotList> copiedSlots(const State& state) {
        auto copy = std::make_shared<SlotList>(*std::atomic_load(&state.slots));
        for (Slot& slot : *copy) {
            slot.active = std::make_shared<std::atomic<bool>>(true);
        }
        return copy;
    }

    std::shared_ptr<State> state_;
};

// Holds the connections an owner made to delegates it does not own, and removes them all when it goes away (or on clear()), newest first.
// The delegates may be destroyed before or after the Connections.
class Connections {
public:
    Connections() = default;
    ~Connections() { clear(); }

    Connections(const Connections&) = delete;
    Connections& operator=(const Connections&) = delete;

    Connections(Connections&& other) noexcept : connections_(std::move(other.connections_)) {
        other.connections_.clear();
    }

    Connections& operator=(Connections&& other) noexcept {
        if (this != &other) {
            clear();
            connections_ = std::move(other.connections_);
            other.connections_.clear();
        }
        return *this;
    }

    // Same as delegate.add(fn), remembering the connection.
    template<typename DelegateT>
    void add(DelegateT& delegate, typename DelegateT::Callback fn) {
        connections_.push_back(delegate.add(std::move(fn)));
    }

    // Disconnects everything added so far.
    void clear() {
        while (!connections_.empty()) {
            Connection connection = std::move(connections_.back());
            connections_.pop_back();
            connection.disconnect();
        }
    }

    std::size_t size() const {
        return connections_.size();
    }

private:
    std::vector<Connection> connections_;
};

}

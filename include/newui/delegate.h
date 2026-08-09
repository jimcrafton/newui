#pragma once

#include <algorithm>
#include <any>
#include <atomic>
#include <chrono>
#include <condition_variable>
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

// Lock-free multicast delegate. Readers (syncCall) take an atomic snapshot
// of an immutable function list, so they never block and never see a list
// mutated mid-iteration. Writers (add/remove) build a new immutable list
// and publish it with a compare-exchange retry loop instead of a mutex.
//
// Sender is the type of the instance invoking syncCall (typically passed as
// `*this` by the owning class), and is always the first parameter every
// FunctionPtr receives.
template<typename SenderT, typename... Args>
class Delegate {
    static_assert(!std::is_reference<SenderT>::value,
        "Delegate's Sender type must be the plain instance type, not a reference "
        "(use Delegate<View>, not Delegate<View&> - the delegate adds & or * itself).");

public:
	using SenderRefT = SenderT&;
    using SenderPtrT = SenderT*;

    using FunctionPtr = SyncReturn(*)(SenderRefT, Args...);


    Delegate() : functions_(std::make_shared<const FunctionList>()) {}

    void add(FunctionPtr fn) {
        if (fn == nullptr) {
            return;
        }

        std::shared_ptr<const FunctionList> oldList = std::atomic_load(&functions_);
        std::shared_ptr<const FunctionList> newList;
        do {
            auto updated = std::make_shared<FunctionList>(*oldList);
            updated->push_back(fn);
            newList = std::move(updated);
        } while (!std::atomic_compare_exchange_weak(&functions_, &oldList, newList));
    }

	Delegate& operator+=(FunctionPtr fn) {
		add(fn);
		return *this;
	}

    void remove(FunctionPtr fn) {
        std::shared_ptr<const FunctionList> oldList = std::atomic_load(&functions_);
        std::shared_ptr<const FunctionList> newList;
        do {
            auto updated = std::make_shared<FunctionList>(*oldList);
            updated->erase(std::remove(updated->begin(), updated->end(), fn), updated->end());
            newList = std::move(updated);
        } while (!std::atomic_compare_exchange_weak(&functions_, &oldList, newList));
    }

	Delegate& operator-=(FunctionPtr fn) {
		remove(fn);
		return *this;
	}

    void syncCall(SenderRefT sender, Args... args) const {
        std::shared_ptr<const FunctionList> snapshot = std::atomic_load(&functions_);
        for (FunctionPtr fn : *snapshot) {
            auto result = fn(sender, args...);
			if (result.error()) {
				return;
			}
        }
    }

	void operator()(SenderRefT sender, Args... args) const {
		syncCall(sender, args...);
	}

    SyncReturn syncCallFirst(SenderRefT sender, Args... args) const {
        std::shared_ptr<const FunctionList> snapshot = std::atomic_load(&functions_);
        for (FunctionPtr fn : *snapshot) {
            auto result = fn(sender, args...);
            if (result.handled()) {
                return result;
            }
        }
        return SyncReturn(SyncReturn::Ignored);
    }

    // Returns immediately. The current list of functions is snapshotted now
    // and posted onto runLoop as a single deferred task, so the callbacks
    // run later, in order, on whatever thread is running runLoop.run() -
    // no thread is spawned. The returned AsyncReturn can be waited on for
    // that task to finish; its result() is the same as syncCall's early-out
    // semantics would produce (last SyncReturn, or the one that errored).
    AsyncReturn postCall(RunLoop& runLoop, SenderPtrT sender, Args... args) const {
        std::shared_ptr<const FunctionList> snapshot = std::atomic_load(&functions_);
        AsyncReturn asyncReturn;
        runLoop.post([snapshot, asyncReturn, sender, args...]() {
            SyncReturn result(SyncReturn::Ignored);
            for (FunctionPtr fn : *snapshot) {
                result = fn(*sender, args...);
                if (result.error()) {
                    break;
                }
            }
            asyncReturn.complete(result);
        });
        return asyncReturn;
    }

private:
    using FunctionList = std::vector<FunctionPtr>;

    std::shared_ptr<const FunctionList> functions_;

    
};



}

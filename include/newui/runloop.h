#pragma once

#include "newui/newui.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>

namespace newui {

// A Win32 message-loop-backed run loop. run() pumps the calling thread's
// message queue; post() enqueues a task from any thread and wakes the loop
// so it drains the queue on the loop's own thread, in the order posted.
//
// post()/quit() require run() to already be executing (on whatever thread
// is going to own the loop) - a thread's message queue only exists once it
// has started pumping messages, so posting before then has nothing to wake.
// Call waitUntilStarted() from another thread to block until that's true.
class RunLoop {
public:
    void run();

    void waitUntilStarted() {
        std::unique_lock<std::mutex> lock(mutex_);
        startedCv_.wait(lock, [this] { return started_; });
    }

    // Enqueues task to run on the loop's thread and wakes the loop.
    void post(std::function<void()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push_back(std::move(task));
        }
        postTaskMsg();
    }

    // Asks the loop to exit; run() returns once it processes this.
    void quit() {
        postQuitMsg();
    }

private:
    static constexpr UINT kTaskMessage = WM_APP + 1;

    void drainTasks() {
        std::deque<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(tasks_);
        }
        for (auto& task : pending) {
            task();
        }
    }

    
    DWORD threadId_ = 0;

    std::mutex mutex_;
    std::condition_variable startedCv_;
    bool started_ = false;
    std::deque<std::function<void()>> tasks_;
    

    void postTaskMsg() const ;
    void postQuitMsg() const ;
};

}

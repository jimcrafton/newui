#pragma once

#include "newui/newui.h"
#include "newui/custom_message_constants.h"
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

    // Enqueues task to run on the loop's own thread every time the loop is
    // idle (no pending Windows messages) - see idleProcessing() - until it
    // reports it's done: task returns false to mean "not finished yet,
    // call me again next time you're idle" (it stays queued), or true to
    // mean "finished" (it's removed and won't run again). A task that
    // always returns true behaves like a one-shot idle callback. Wakes the
    // loop the same way post() does, so a task queued while the loop is
    // blocked waiting for the next message still gets picked up promptly
    // instead of waiting on unrelated activity to nudge the loop idle
    // again.
    void postIdle(std::function<bool()> task) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            idleTasks_.push_back(std::move(task));
        }
        postTaskMsg();
    }

    // Asks the loop to exit; run() returns once it processes this.
    void quit() {
        postQuitMsg();
    }

private:

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
    std::deque<std::function<bool()>> idleTasks_;



    void postTaskMsg() const ;
    void postQuitMsg() const ;

    // Runs a single queued idle task, if any - not the whole queue at
    // once. run()'s idle loop calls this repeatedly (see hasIdleTasks()),
    // checking for a pending Windows message between each call, so a long
    // run of idle work stays interruptible instead of hogging the loop
    // until it's all done. The task is popped from the front and, if it
    // reports it isn't finished (returns false), pushed back onto the
    // back of the queue rather than left in place - so with more than one
    // idle task queued, they interleave round-robin instead of one
    // starving the rest until it completes.
    void idleProcessing();

    // Whether any idle tasks are still queued; drives run()'s idle loop -
    // it keeps calling idleProcessing() while this is true.
    bool hasIdleTasks();
};

}

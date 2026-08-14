#pragma once

#include "newui/newui.h"
#include "newui/custom_message_constants.h"
#include "newui/delegate.h"

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

    typedef Delegate<RunLoop> RunLoopDelegate;
    typedef Delegate<RunLoop, bool&> RunLoopEndingDelegate;

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

    // Runs a nested modal message loop directly on the calling thread -
    // the same reentrant-pump technique any native Win32 modal dialog
    // uses internally - blocking until isDone() returns true. modalHandle
    // is the window actually being shown modally: WM_CLOSE targeting it
    // (whether from its own titlebar X, or Escape - see onModalEnding
    // below) is what onModalEnding fires for and what a subscriber can
    // veto; may be nullptr for a modal loop with no single window to
    // watch (isDone() is then the only way it ever ends, short of
    // WM_QUIT). ownerHandle (may independently be nullptr) is a
    // *different* window - typically modalHandle's conceptual owner -
    // disabled for the duration via ::EnableWindow() so it can't be
    // interacted with, then re-enabled and refocused before this
    // returns, regardless of how the loop ended.
    //
    // Meant for any modal-UI scenario, not just Dialog::showModal() (its
    // original motivating use) - e.g. a custom confirmation popup, a
    // blocking progress window, anything that needs to pump messages (so
    // the UI stays responsive/repaints) while still not returning control
    // to its caller until dismissed.
    //
    // Must be called from the same thread already running this RunLoop's
    // own run() - GetMessage()/DispatchMessage() below always operate on
    // "whichever thread calls them"'s message queue, so calling this
    // before run() has started pumping, or from a different thread than
    // the one running it, would silently drive an unrelated queue
    // instead of this loop's real one rather than the nested loop anyone
    // actually wants. Throws std::runtime_error in either case.
    //
    // Returns true if the loop ended because isDone() returned true, or
    // false if it ended early because WM_QUIT reached this thread (the
    // app itself is shutting down) - re-posted via ::PostQuitMessage() so
    // the outer run() still sees it once this call returns and unwinds
    // back to it. Callers that track their own "why did this end" result
    // (e.g. Dialog::showModal()'s DialogResult) should treat a false
    // return as an implicit cancel/abort, since isDone() never actually
    // agreed the loop was done.
    bool runModal(HWND modalHandle, HWND ownerHandle, std::function<bool()> isDone);

    // Fired right as run()'s own message pump starts/ends - see run().
    RunLoopDelegate onStart;

    // Fired exactly once, the one place run() ever sees WM_QUIT (see
    // run()'s comment on why every other "quit" path funnels through
    // there too). canQuit starts true; a subscriber that sets it false
    // vetoes ending run() - since WM_QUIT itself is already consumed by
    // the time this fires, "vetoing" just means run() keeps pumping as
    // if nothing happened, not that the quit request is somehow retried
    // later.
    RunLoopEndingDelegate onEnding;

    // Fired right before run() returns, once its pump has actually ended.
    RunLoopDelegate onEnd;

    // Fired right as runModal() starts/ends - see runModal().
    RunLoopDelegate onModalStart;

    // Fired when modalHandle is about to receive a WM_CLOSE runModal()
    // is watching for (see runModal()'s modalHandle parameter) - covers
    // both a real titlebar X click and Escape (translated into the same
    // WM_CLOSE). canClose starts true; a subscriber that sets it false
    // vetoes it: the WM_CLOSE is swallowed before ever reaching
    // modalHandle's own WndProc, so nothing closes and the modal loop
    // keeps running.
    RunLoopEndingDelegate onModalEnding;

    // Fired right before runModal() returns, once its loop has actually
    // ended and owner has been re-enabled.
    RunLoopDelegate onModalEnd;
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

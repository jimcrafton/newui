#pragma once

#include "newui/newui.h"
#include "newui/custom_message_constants.h"
#include "newui/delegate.h"

#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace newui {

class Action;

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

    // The RunLoop actively pumping messages on the calling thread right
    // now, or nullptr if none is - thread-local, set for the whole
    // duration of whichever RunLoop instance's run() is executing on this
    // thread (see run()'s own implementation). Works the same regardless
    // of who owns that instance - Application's own runLoop_ member (the
    // usual case), or a standalone RunLoop a caller constructs and runs
    // itself on its own dedicated thread with no Application/Frame
    // involved at all (e.g. a RootView hosted inside another process's
    // HWND). Lets code that needs "whatever loop is live right now"
    // (scheduleRepaint()'s idle task, Caret's blink timer, ScrollBar/
    // Stepper auto-repeat, hotkey Action registration, ...) work
    // correctly in either configuration without hardcoding
    // Application::instance().runLoop().
    static RunLoop* current() {
        return t_currentRunLoop;
    }

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

    // Opaque handle to a still-running postDelayed() timer - accepted by
    // cancelDelayed(). kInvalidTimerHandle is never a real handle
    // postDelayed() itself returns.
    using TimerHandle = UINT_PTR;
    static constexpr TimerHandle kInvalidTimerHandle = 0;

    // Like postIdle(), but task only actually runs once per interval has
    // elapsed, rather than on every idle tick - the formal version of the
    // repeat-timer pattern ScrollBar::startRepeat()/Stepper::startRepeat()
    // (controls.cpp) each currently hand-roll themselves (a postIdle task
    // that checks its own steady_clock::time_point). Implemented via
    // ::SetTimer(nullptr, 0, interval, TimerProc)/::KillTimer() rather
    // than a hand-rolled scheduler: with hwnd == nullptr, WM_TIMER posts
    // into the calling thread's own queue and DispatchMessage() invokes
    // the callback directly (no window procedure involved), so this slots
    // into run()'s existing GetMessage()/DispatchMessage() pump with no
    // change needed to that loop's own idle-wait logic (which has a
    // documented past CPU-usage bug in its own comments - better left
    // alone). Also thread-safe by construction: a given timer's callback
    // only ever fires on the thread that called ::SetTimer() for it -
    // task therefore only ever runs on this RunLoop's own thread, same as
    // every post()/postIdle() task, but with no aliveFlag_-style guard
    // needed the way the existing hand-rolled repeats require.
    //
    // task's return convention matches postIdle(): false means "not
    // finished, call again after another interval" (the underlying timer
    // keeps running); true means "finished" - the timer is killed and
    // task is dropped, it won't run again.
    //
    // Must be called from the same thread already running this RunLoop's
    // own run() - same restriction as runModal() (see its own doc
    // comment), for the same underlying reason: a Win32 timer is
    // inherently thread-affine, unlike a plain post()/postIdle() task,
    // so there's no cross-thread "wake the loop" path this could
    // reasonably support instead. Throws std::runtime_error if called
    // before run() has started, or from a different thread.
    //
    // Returns a handle cancelDelayed() accepts to stop this timer early -
    // e.g. Caret needs to stop blinking the instant focus is lost or it's
    // destroyed, not wait for its own next tick (whose captured task
    // would otherwise fire again against a view that's gone).
    TimerHandle postDelayed(std::chrono::milliseconds interval, std::function<bool()> task);

    // Cancels a timer started via postDelayed() before it finishes on its
    // own - a no-op if handle is kInvalidTimerHandle or already
    // finished/cancelled. Same thread restriction as postDelayed() itself.
    void cancelDelayed(TimerHandle handle);

    // Asks the loop to exit; run() returns once it processes this.
    void quit() {
        postQuitMsg();
    }

    // Registers action so run()'s own message pump matches its
    // hotkey()/hotkeyMask() (see Action::matchesHotkey()) against every
    // incoming WM_KEYDOWN/WM_SYSKEYDOWN and calls its perform() on a
    // match - see run()'s WM_KEYDOWN/WM_SYSKEYDOWN case. Not owned - a
    // caller whose Action is about to be destroyed must unregisterAction()
    // it first. Registering the same action twice is a no-op.
    void registerAction(Action* action);

    // Undoes registerAction() - a no-op if action isn't currently
    // registered (or is nullptr).
    void unregisterAction(Action* action);

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

    // postDelayed()'s live timers, keyed by the handle ::SetTimer()
    // returned. Only ever touched from this RunLoop's own thread (see
    // postDelayed()'s own doc comment on why it doesn't support being
    // called cross-thread the way tasks_/idleTasks_ do) - no mutex_
    // guarding this one.
    std::unordered_map<TimerHandle, std::function<bool()>> timerTasks_;

    // registerAction()'d Actions - checked against every WM_KEYDOWN/
    // WM_SYSKEYDOWN in run(). Not owned; see registerAction()'s own doc
    // comment. Only ever touched from this RunLoop's own thread, same
    // as timerTasks_ above.
    std::vector<Action*> actions_;

    // Set for the duration of run(), cleared on return - lets the static
    // TimerProc() below find its way back to whichever RunLoop instance
    // is actually running on the current thread, since a Win32 TIMERPROC
    // carries no user-data slot of its own. Safe as thread_local rather
    // than needing to be keyed by thread id itself: TimerProc() for a
    // given timer is only ever invoked on the thread that called
    // ::SetTimer() for it, which is always the same thread currently
    // running this RunLoop's own run().
    static thread_local RunLoop* t_currentRunLoop;

    static void CALLBACK TimerProc(HWND hwnd, UINT message, UINT_PTR idEvent, DWORD dwTime);

    void checkCalledFromLoopThread(const char* what);

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

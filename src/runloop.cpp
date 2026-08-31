#include "newui/runloop.h"
#include "newui/action.h"
#include "newui/keyboard_constants.h"
#include "newui/utils.h"
#include <algorithm>
#include <bitset>
#include <unordered_map>


namespace newui {
    std::unordered_map<size_t, RunLoop*> g_runLoopMap;
    

    RunLoop::RunLoop()
    {
        threadId_ = ::GetCurrentThreadId();
        g_runLoopMap.emplace(threadId_, this);
    }

    RunLoop::~RunLoop()
    {

    }

    void RunLoop::drainTasks() {
        std::deque<std::function<void()>> pending;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending.swap(tasks_);
        }
        for (auto& task : pending) {
            task();
        }
    }

    void RunLoop::waitForStart()
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!started_) {
            startedCv_.wait(lock, [this] { return started_; });
        }   
    }

    RunLoop& RunLoop::current()
    {
        thread_local RunLoop currentThreadRunLoop;        
        return currentThreadRunLoop;
    }

    RunLoop* RunLoop::runLoopFromThread(size_t threadId)
    {
        RunLoop* result = nullptr;

        auto found = g_runLoopMap.find(threadId);
        if (found != g_runLoopMap.end()) {
            result = found->second;
        }

        return result;
    }

    void RunLoop::run() {

        
        HRESULT hr = OleInitialize(NULL);
        if (!SUCCEEDED(hr)) {
            throw std::runtime_error("newui::RunLoop: OleInitialize failed");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = true;
        }
        startedCv_.notify_all();
		onStart(*this);

        MSG msg;
        BOOL getMessageResult;
		BYTE keyState[256];
		HACCEL hAccelTable = NULL;

        bool done = false;
		bool isIdle = true;

        while (!done ) {
			while (isIdle && (!::PeekMessage(&msg, NULL, NULL, NULL, PM_NOREMOVE))) {
				idleProcessing();
				if (!hasIdleTasks()) {
					isIdle = false;
				} else {
					// There's still idle work pending (e.g. AnimationManager,
					// or any other permanent postIdle() task - see
					// RunLoop::postIdle()'s doc comment on "never returns
					// true" tasks) but nothing new happened just now -
					// without this wait, this loop spins re-checking
					// PeekMessage as fast as the CPU allows, pegging a full
					// core the entire time any such task is registered,
					// even completely untouched (confirmed live: an idle
					// newui app showing ~5% CPU in Task Manager - see
					// HANDOFF.md). MsgWaitForMultipleObjects blocks for at
					// most 1ms (in practice however long the system's timer
					// resolution actually grants, typically ~15ms - still
					// far more than enough headroom for 30-60fps idle work)
					// but wakes immediately if a real message arrives, so
					// this doesn't add latency to normal message handling.
					::MsgWaitForMultipleObjects(0, nullptr, FALSE, 1, QS_ALLINPUT);
				}
			}

			do {
				bool doTranslateAndDispatch = true;

				if (GetMessage(&msg, NULL, 0, 0)) {
					switch (msg.message) {

						case kTaskMessage: {
							drainTasks();
							doTranslateAndDispatch = false;
						}
						break;

						case WM_KEYDOWN:  case WM_SYSKEYDOWN: {

							KeyboardEventInfo keyData = {};
							translateKeyEventInfo(msg.hwnd, msg.message, msg.wParam, msg.lParam, keyData);

							int  keyCharVal = 0;
							int eventType = keUndefined;

							// keyData.keyMask is already newui's own kmShift/
							// kmCtrl/kmAlt bits (translateKeyEventInfo()
							// builds it straight from GetAsyncKeyState(), not
							// a raw Win32 MK_* mask) - do NOT re-run it
							// through translateKeyMask(), which expects the
							// mouse-message MK_CONTROL/MK_SHIFT encoding
							// instead. Doing so corrupts it: kmCtrl (0x4)
							// collides with MK_SHIFT (0x4), so a real Ctrl
							// press gets reported as Shift while Ctrl itself
							// never registers - see the identical fix/comment
							// on RootView::handleMessage()'s own WM_KEYDOWN
							// case (rootview.cpp), which hit this same bug
							// first; this call site (registerAction()'s own
							// hotkey matching, just below) still had it.
							auto keyMask = static_cast<std::uint32_t>(keyData.keyMask);

							keyData.VKeyCode = translateVirtualKey(msg.wParam, 0);

							bool msgConsumed = false;

							for (Action* action : actions_) {
								if (action->matchesHotkey(static_cast<std::uint32_t>(keyData.VKeyCode), keyMask)) {
									action->perform();
									msgConsumed = true;
									break;
								}
							}

							if (msgConsumed) {
								doTranslateAndDispatch = false;
							}
						}
						break;

						case WM_SYSCOMMAND: {
							if (SC_CLOSE == msg.wParam) {
								PostQuitMessage(0);
							}
						}
						break;
					}
					if (doTranslateAndDispatch) {
						if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
							TranslateMessage(&msg);
							DispatchMessage(&msg);
						}
					}
				}
				else {
					// GetMessage() returns FALSE (0) only when it
					// retrieves WM_QUIT - this is the only place that
					// ever happens (msg.message can never actually equal
					// WM_QUIT past this point - see onEnding's own doc
					// comment), so it's where onEnding fires. canQuit
					// lets a subscriber veto ending the loop; the WM_QUIT
					// itself is already consumed - there's no way to put
					// it back on the queue - so "vetoing" just means
					// keep pumping as if nothing happened, not that the
					// quit request is retried later.
					bool canQuit = true;
					onEnding(*this, canQuit);
					if (!canQuit) {
						continue;
					}
					done = true;
					break;
				}

				bool isIdleMessage = ((msg.message != WM_PAINT) && (msg.message != 0x0118));
				if (isIdleMessage) {
					if ((msg.message == WM_MOUSEMOVE) || (msg.message == WM_NCMOUSEMOVE)) {
						//check the prev mouse pt;
					}
				}

				if (isIdleMessage) {
					isIdle = true;
				}

			} while (::PeekMessage(&msg, NULL, NULL, NULL, PM_NOREMOVE) );
        }


        OleUninitialize();

        // Any postDelayed() timers still outstanding at this point belong
        // to this thread's message queue, which is about to stop being
        // pumped - kill them explicitly rather than leaving them to fire
        // WM_TIMER messages nobody will ever process again.
        for (auto& entry : timerTasks_) {
            ::KillTimer(nullptr, entry.first);
        }
        timerTasks_.clear();
        

		onEnd(*this);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
        }
    }

    void RunLoop::checkCalledFromLoopThread(const char* what) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!started_) {
                throw std::runtime_error(std::string("newui::RunLoop::") + what + " called before run()");
            }
        }
        if (::GetCurrentThreadId() != threadId_) {
            throw std::runtime_error(std::string("newui::RunLoop::") + what + " called from a different thread than run()");
        }
    }

    RunLoop::TimerHandle RunLoop::postDelayed(std::chrono::milliseconds interval, std::function<bool()> task) {
        checkCalledFromLoopThread("postDelayed");

        UINT_PTR elapse = static_cast<UINT_PTR>(interval.count());
        UINT_PTR handle = ::SetTimer(nullptr, 0, elapse, &RunLoop::TimerProc);
        if (handle == 0) {
            throw std::runtime_error("newui::RunLoop::postDelayed: ::SetTimer failed");
        }

        timerTasks_[handle] = std::move(task);
        return handle;
    }

    void RunLoop::cancelDelayed(TimerHandle handle) {
        checkCalledFromLoopThread("cancelDelayed");

        if (handle == kInvalidTimerHandle) {
            return;
        }

        auto it = timerTasks_.find(handle);
        if (it == timerTasks_.end()) {
            return;
        }

        ::KillTimer(nullptr, handle);
        timerTasks_.erase(it);
    }

    void CALLBACK RunLoop::TimerProc(HWND /*hwnd*/, UINT /*message*/, UINT_PTR idEvent, DWORD /*dwTime*/) {
        
        if (RunLoop::current()) {
            return;
        }
        auto& loop = RunLoop::current();
        auto it = loop.timerTasks_.find(idEvent);
        if (it == loop.timerTasks_.end()) {
            return;
        }

        // Copied out rather than invoked in place - task itself may call
        // postDelayed()/cancelDelayed() (e.g. rescheduling itself, or
        // cancelling a *different* still-live timer), either of which
        // would otherwise mutate timerTasks_ out from under the iterator
        // this holds. The map's own entry for idEvent, if task() leaves it
        // alone, still holds the real std::function untouched throughout.
        std::function<bool()> task = it->second;
        bool finished = task();
        if (finished) {
            ::KillTimer(nullptr, idEvent);
            loop.timerTasks_.erase(idEvent);
        }
        // Not finished: ::SetTimer()'s own periodic re-arm already
        // schedules the next WM_TIMER at the same interval - nothing
        // further to do here.
    }

    void RunLoop::registerAction(Action* action) {
        if (action == nullptr) {
            return;
        }
        if (std::find(actions_.begin(), actions_.end(), action) != actions_.end()) {
            return;
        }
        actions_.push_back(action);
    }

    void RunLoop::unregisterAction(Action* action) {
        if (action == nullptr) {
            return;
        }
        actions_.erase(std::remove(actions_.begin(), actions_.end(), action), actions_.end());
    }

    void RunLoop::postTaskMsg() const
    {
        ::PostThreadMessage(threadId_, kTaskMessage, 0, 0);
    }

    void RunLoop::postQuitMsg() const
    {
        ::PostThreadMessage(threadId_, WM_QUIT, 0, 0);
    }

    bool RunLoop::runModal(HWND modalHandle, HWND ownerHandle, std::function<bool()> isDone)
    {
        checkCalledFromLoopThread("runModal");

        onModalStart(*this);

        if (ownerHandle != nullptr) {
            ::EnableWindow(ownerHandle, FALSE);
        }

        bool quitSeen = false;
        MSG msg;
        while (!isDone()) {
            BOOL got = ::GetMessage(&msg, nullptr, 0, 0);
            if (got == 0 || got == -1) {
                // WM_QUIT reached this thread (or GetMessage itself
                // failed) - stop waiting rather than swallow it; re-post
                // so the outer run() still sees WM_QUIT once this call
                // returns and unwinds back to it.
                if (got == 0) {
                    ::PostQuitMessage(static_cast<int>(msg.wParam));
                }
                quitSeen = true;
                break;
            }

            bool doTranslateAndDispatch = true;

            if (modalHandle != nullptr) {
                if (msg.hwnd == modalHandle && msg.message == WM_CLOSE) {
                    bool canClose = true;
                    onModalEnding(*this, canClose);
                    if (!canClose) {
                        // Swallow it - never reaches modalHandle's own
                        // WndProc, so it doesn't actually close.
                        doTranslateAndDispatch = false;
                    }
                }
                else if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE
                         && ::GetAncestor(msg.hwnd, GA_ROOT) == modalHandle) {
                    // Escape arrives targeting whichever control actually
                    // has keyboard focus, not necessarily modalHandle
                    // itself - e.g. a Dialog's RootView is a child window
                    // that grabs focus on its own (see
                    // RootView::initialize()'s SetFocus() call), so msg.hwnd
                    // here is that child, never the top-level frame. Walk
                    // up to the owning top-level window before comparing,
                    // so this still fires regardless of which of
                    // modalHandle's descendants currently has focus.
                    //
                    // Translate Escape into the same close request a
                    // native titlebar X click sends, so the WM_CLOSE
                    // handling above - veto included - is the one and
                    // only place that decides whether a close request
                    // actually goes through, for either trigger, instead
                    // of firing onModalEnding twice with two different
                    // ways to answer it.
                    ::PostMessage(modalHandle, WM_CLOSE, 0, 0);
                    doTranslateAndDispatch = false;
                }
            }

            if (doTranslateAndDispatch) {
                ::TranslateMessage(&msg);
                ::DispatchMessage(&msg);
            }
        }

        if (ownerHandle != nullptr) {
            ::EnableWindow(ownerHandle, TRUE);
            ::SetForegroundWindow(ownerHandle);
        }

        onModalEnd(*this);

        return !quitSeen;
    }

	void RunLoop::idleProcessing()
	{
		std::function<bool()> task;
		{
			std::lock_guard<std::mutex> lock(mutex_);
			if (idleTasks_.empty()) {
				return;
			}
			task = std::move(idleTasks_.front());
			idleTasks_.pop_front();
		}

		if (!task) {
			return;
		}

		bool complete = task();
		if (!complete) {
			std::lock_guard<std::mutex> lock(mutex_);
			idleTasks_.push_back(std::move(task));
		}
	}

	bool RunLoop::hasIdleTasks()
	{
		std::lock_guard<std::mutex> lock(mutex_);
		return !idleTasks_.empty();
	}

    void RunLoop::runTillNotified(std::condition_variable& cv, std::mutex& mtx)
    {
        for (;;)
        {
            std::unique_lock<std::mutex> lock(mtx);
            auto waitMs = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
            auto success = cv.wait_until(lock, waitMs);


            if (success == std::cv_status::no_timeout)
            {
                return;
            }

            MSG msg;
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
        }
    }
}
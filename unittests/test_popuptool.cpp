#include "newui/popuptool.h"
#include "newui/runloop.h"

#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>

// Real WM_KEYDOWN sent through the popup's own WndProc (not a direct onKeyDown() call). Runs on a
// real RunLoop thread: with no RunLoop current, dismiss() deletes the popup synchronously from
// inside its own event dispatch, which is only safe when deferred through the loop.

namespace {
int g_dismissedCount = 0;

newui::SyncReturn CountDismissed(newui::PopupTool&) {
    ++g_dismissedCount;
    return newui::SyncReturn::Handled;
}

// Runs task on a fresh RunLoop's own thread and waits for it.
template <typename Fn>
void runOnLoopThread(Fn task) {
    auto [runLoopPtr, loopThread] = newui::RunLoop::runThreaded();
    newui::RunLoop& runLoop = *runLoopPtr;
    runLoop.waitForStart();

    std::mutex m;
    std::condition_variable cv;
    bool done = false;
    runLoop.post([&]() {
        task();
        // Queued after anything task() itself posted, so those have already run by now.
        newui::RunLoop::current().post([&]() {
            std::lock_guard<std::mutex> lock(m);
            done = true;
            cv.notify_all();
        });
    });
    {
        std::unique_lock<std::mutex> lock(m);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; }));
    }
    runLoop.quit();
    loopThread.join();
}

HWND makeOwner() {
    return ::CreateWindowExA(0, "STATIC", "owner", WS_OVERLAPPEDWINDOW, 0, 0, 200, 200,
        nullptr, nullptr, ::GetModuleHandleA(nullptr), nullptr);
}
}

TEST(PopupTool, PopupHoldsKeyboardFocusRightAfterInitialize) {
    HWND focused = nullptr;
    HWND popupHwnd = nullptr;
    runOnLoopThread([&]() {
        HWND owner = makeOwner();
        auto* popup = new newui::CalloutTool(owner, ::GetModuleHandleA(nullptr), newui::Rect(10, 10, 120, 80), "p");
        if (popup->initialize()) {
            popupHwnd = popup->windowHandle();
            focused = ::GetFocus();
        }
        popup->dismiss();
        ::DestroyWindow(owner);
    });

    ASSERT_NE(popupHwnd, nullptr);
    EXPECT_EQ(focused, popupHwnd);
}

TEST(PopupTool, EscapeKeyDownDismisses) {
    g_dismissedCount = 0;
    int countRightAfterKey = -1;
    runOnLoopThread([&]() {
        HWND owner = makeOwner();
        auto* popup = new newui::CalloutTool(owner, ::GetModuleHandleA(nullptr), newui::Rect(10, 10, 120, 80), "p");
        if (popup->initialize()) {
            popup->onDismissed.add(&CountDismissed);
            ::SendMessage(popup->windowHandle(), WM_KEYDOWN, VK_ESCAPE, 0);
            countRightAfterKey = g_dismissedCount;  // teardown is posted, not synchronous
        }
        ::DestroyWindow(owner);
    });

    EXPECT_EQ(countRightAfterKey, 0);
    EXPECT_EQ(g_dismissedCount, 1);
}

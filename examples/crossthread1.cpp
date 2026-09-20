// The VSIX-host shape, with the real classes: a standalone newui::RootView (no Application, no Frame) living on
// its own worker thread's RunLoop, as a WS_CHILD of a plain Win32 window that belongs to the MAIN thread.
// Whichever PresentSurface the RootView uses (presentsurface.h) is created, sized, presented to and destroyed
// entirely on the worker thread.
//
//   crossthread1.exe [gdi|dxgi] [--stall]        (or NEWUI_PRESENT=dxgi)
//
// Threading rules this follows - the same ones swapchain2.cpp proved for a bare swap chain, and HANDOFF Part 92's
// EditThreadHost deadlock is what breaks them:
//  - The main (parent) thread only ever PostMessage()s / RunLoop::post()s to the worker. It never SendMessage()s
//    to the child window and never calls SetWindowPos/MoveWindow/ShowWindow on it (they send messages
//    internally); the worker does its own RootView::setBounds() when told the parent's size changed.
//  - The main thread never blocks waiting for the worker: CreateWindowEx()/DestroyWindow() of a cross-thread
//    child SendMessage() WM_PARENTNOTIFY back to the parent, so a parent blocked in join() would deadlock.
//    Shutdown is a posted handshake, and join() only happens once the worker is finished with its window.
//  - --stall makes the main thread Sleep(1500) inside every WM_SIZE: the child must keep responding (hover,
//    repaint, present) on the worker thread through it.
//  - Minimizing the parent tells the worker to size the child to 0x0 (what a collapsed host does), i.e. the
//    surface gets resize(0, 0) and then a real size again on restore.

#include "newui/newui.h"
#include "newui/controls.h"
#include "newui/layout.h"
#include "newui/presentsurface.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/uicolormanager.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace {

    constexpr UINT WM_APP_CHILD_READY = WM_APP + 1;  // worker -> parent
    constexpr UINT WM_APP_CHILD_GONE = WM_APP + 2;   // worker -> parent: the child window is destroyed
    constexpr int kHeaderHeight = 40;

    bool g_stall = false;

    HWND g_parent = nullptr;
    newui::RunLoop* g_loop = nullptr;  // the worker's loop - post() is thread-safe
    bool g_childReady = false;         // parent thread only
    bool g_closing = false;            // parent thread only
    newui::RootView* g_root = nullptr; // owned by, and only ever touched on, the worker thread

    // Runs on the worker: builds the hosted RootView as a child of `parent` (owned by the main thread).
    void createHostedRoot(HWND parent, int width, int height) {
        g_root = new newui::RootView(parent, ::GetModuleHandleW(nullptr),
            newui::Rect(0.0f, float(kHeaderHeight), float(width), float(height)), "hosted");
        newui::RootView& root = *g_root;
        root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

        auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        layout->setSpacing(10.0f);
        layout->setPadding(16.0f);
        root.setLayout(std::move(layout));

        auto* label = new newui::Label();
        label->setVisible(true);
        label->setText("Starting...");
        label->setDesiredSize(newui::Size(0.0f, 28.0f));
        root.addChild(label);
        for (const char* text : { "First", "Second", "Third" }) {
            auto* button = new newui::Button();
            button->setVisible(true);
            button->setText(text);
            button->setDesiredSize(newui::Size(0.0f, 28.0f));
            root.addChild(button);
        }

        // Report which backend the child really ended up with, on the first frame and after every resize (a
        // minimize/restore rebuilds the swap chain) - printing only on a change.
        auto lastReported = std::make_shared<std::optional<newui::PresentBackend>>();
        auto report = [label, lastReported]() {
            if (g_root == nullptr) {
                return;
            }
            const newui::PresentBackend now = g_root->presentBackend();
            if (*lastReported == now) {
                return;
            }
            *lastReported = now;
            const char* name = now == newui::PresentBackend::Dxgi ? "DXGI" : "GDI";
            std::printf("crossthread1: hosted RootView presenting via %s (worker thread %lu)\n", name, ::GetCurrentThreadId());
            std::fflush(stdout);
            label->setText(std::string("Worker thread, presenting via ") + name);
        };
        root.onSizeChanged.add(std::function<newui::SyncReturn(newui::View&, const newui::Size&)>(
            [report](newui::View&, const newui::Size&) {
                newui::RunLoop::current().post(report);
                return newui::SyncReturn::Handled;
            }));

        // Cross-thread parent: this sends WM_PARENTNOTIFY etc. to the parent's thread, which is pumping.
        if (!root.initialize()) {
            std::printf("crossthread1: RootView::initialize() failed\n");
            std::fflush(stdout);
            return;
        }
        newui::RunLoop::current().post(report);
        ::PostMessageW(parent, WM_APP_CHILD_READY, 0, 0);
    }

    // Runs on the worker: tears the hosted RootView down, tells the parent it's now safe to go away, and stops
    // the worker's loop.
    void destroyHostedRoot() {
        if (g_root != nullptr) {
            g_root->destroy();  // DestroyWindow() of the cross-thread child - the parent thread is pumping
            delete g_root;
            g_root = nullptr;
        }
        ::PostMessageW(g_parent, WM_APP_CHILD_GONE, 0, 0);
        newui::RunLoop::current().quit();
    }

    // Parent thread: tell the worker to re-size the child. Never touches the child window itself.
    void layoutChild(HWND parent, bool minimized) {
        if (!g_childReady || g_loop == nullptr) {
            return;
        }
        RECT rc = {};
        ::GetClientRect(parent, &rc);
        const int width = minimized ? 0 : rc.right - rc.left;
        const int height = minimized ? 0 : rc.bottom - rc.top - kHeaderHeight;
        if (!minimized && (width <= 0 || height <= 0)) {
            return;
        }
        g_loop->post([width, height]() {
            if (g_root != nullptr) {
                g_root->setBounds(newui::Rect(0.0f, float(kHeaderHeight), float(width), float(height)));
            }
        });
    }

    LRESULT CALLBACK parentProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
        case WM_APP_CHILD_READY:
            g_childReady = true;
            layoutChild(hwnd, false);  // pick up any resize that happened while the child was being created
            return 0;
        case WM_SIZE:
            layoutChild(hwnd, wp == SIZE_MINIMIZED);
            if (g_stall && wp != SIZE_MINIMIZED) {
                ::Sleep(1500);  // the parent thread is busy: the child must keep working regardless
            }
            return 0;
        case WM_CLOSE:
            if (g_loop != nullptr && !g_closing) {
                g_closing = true;
                g_loop->post(destroyHostedRoot);  // destroy the parent only once the worker says it's done
                return 0;
            }
            break;
        case WM_APP_CHILD_GONE:
            ::DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = ::BeginPaint(hwnd, &ps);
            RECT rc = {};
            ::GetClientRect(hwnd, &rc);
            RECT band = { 0, 0, rc.right, kHeaderHeight };
            HBRUSH brush = ::CreateSolidBrush(RGB(30, 30, 30));
            ::FillRect(dc, &band, brush);
            ::DeleteObject(brush);
            ::SetBkMode(dc, TRANSPARENT);
            ::SetTextColor(dc, RGB(230, 230, 230));
            const wchar_t* text = L"Parent window (main thread, GDI). Below: a standalone RootView on a worker thread.";
            ::TextOutW(dc, 10, 12, text, int(wcslen(text)));
            ::EndPaint(hwnd, &ps);
            return 0;
        }
        }
        return ::DefWindowProcW(hwnd, msg, wp, lp);
    }

}

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    for (int i = 1; i < argc; ++i) {
        newui::PresentBackend requested;
        if (std::strcmp(argv[i], "--stall") == 0) {
            g_stall = true;
        }
        else if (newui::parsePresentBackend(argv[i], requested)) {
            newui::setDefaultPresentBackend(requested);
        }
        else {
            std::printf("usage: crossthread1 [gdi|dxgi] [--stall]   (unrecognized: '%s')\n", argv[i]);
        }
    }

    HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = parentProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"crossthread1_parent";
    wc.hCursor = ::LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    ::RegisterClassExW(&wc);

    g_parent = ::CreateWindowExW(0, wc.lpszClassName, L"Cross-thread RootView", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        10, 10, 460, 320, nullptr, nullptr, instance, nullptr);
    ::ShowWindow(g_parent, SW_SHOW);

    newui::RunLoop::RunLoopThread worker = newui::RunLoop::runThreaded();
    g_loop = worker.loop;
    g_loop->waitForStart();  // the worker just has to start pumping; it needs nothing from this thread yet

    RECT rc = {};
    ::GetClientRect(g_parent, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top - kHeaderHeight;
    g_loop->post([width, height]() { createHostedRoot(g_parent, width, height); });

    // Pump right away - the worker's CreateWindowEx() of the child needs this thread servicing messages.
    MSG msg;
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }

    // Only reached after the parent was destroyed, which only happens after the worker posted CHILD_GONE - i.e.
    // it's finished with its window and is just returning from run() - so this join can't block on this thread.
    worker.thread.join();
    return 0;
}

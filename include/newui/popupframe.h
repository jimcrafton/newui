#pragma once

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/frame.h>

namespace newui {

    // A borderless, always-on-top, non-activating popup window - the same
    // "owns a Frame HWND, hosts a RootView" idea Frame itself is, just
    // with WS_POPUP/WS_EX_NOACTIVATE styling instead of a titled
    // WS_OVERLAPPEDWINDOW. Built for DropDownList (controls.h) to host a
    // ListView in - RootView::initialize() (rootview.cpp) hard-codes its
    // WS_CHILD parent to parentFrame_->frameHandle(), so anything hosting
    // a RootView has to genuinely be a Frame, not just look like one; this
    // is why PopupFrame derives from Frame rather than being a from-scratch
    // class (see this feature's own plan/HANDOFF.md entry for the fuller
    // reasoning).
    //
    // Frame::WndProc/handleMessage()/frameCreated()/destroy() are all
    // reused unchanged (see frame.h) - only the window class/style/owner
    // in initialize() and the WM_CREATE/WM_DESTROY/dismiss handling in
    // handleMessage() are actually different from Frame's own.
    //
    // Persistent, not recreated per show - show()/hide() just toggle
    // window visibility (plus the outside-click hook below), so a
    // DropDownList's hosted ListView keeps its scroll position/pooled
    // Items across repeated opens instead of rebuilding from scratch every
    // click.
    class PopupFrame : public Frame {
    public:
        PopupFrame();
        ~PopupFrame() override;

        // owner is required, unlike Frame::initialize() (which defaults to
        // Application::instance().dummyWindowHandle()) - a WS_POPUP's
        // owner relationship is what keeps this correctly stacked above/
        // tied to the real window that spawned it (e.g. so it doesn't
        // outlive or float independently of that window).
        bool initialize(HWND owner);

        // Frame::setBounds() (inherited) only ever updates this object's
        // own bounds_/rootView_ sizing model - it never actually moves the
        // real HWND (fine for Frame's own top-level windows, which this
        // codebase never repositions after creation). A popup needs the
        // opposite: it's the same persistent HWND repositioned to a new
        // screen location on every single open, so moveTo() does both -
        // setBounds() for the model, then ::SetWindowPos() for the real
        // window - screenBounds is in screen coordinates (e.g. from
        // RootView::localToScreen()).
        void moveTo(const Rect& screenBounds);

        void show();
        void hide();
        bool isVisible() const;

        // Fired when this popup dismisses itself because the user clicked
        // outside it (see the WH_MOUSE hook below) - not fired from an
        // explicit hide() call. DropDownList listens to reset its own
        // "open" visual state without touching selection.
        typedef Delegate<PopupFrame> DismissedDelegate;
        DismissedDelegate onDismissed;

    protected:
        bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT) override;

    private:
        HWND owner_ = nullptr;

        // A WH_MOUSE_LL (low-level, system-wide) hook, installed only
        // while shown, is used to detect an outside click - not
        // SetCapture(), and not a thread-scoped WH_MOUSE hook. RootView
        // (rootview.h) already does its own SetCapture()/ReleaseCapture()
        // for drag tracking on *its* child HWND; if this popup's own
        // top-level HWND also held capture, that would steal mouse input
        // away from RootView's child HWND for clicks *inside* the popup
        // (only one HWND process-wide can hold capture at a time),
        // breaking clicking a row in the hosted ListView. A plain WH_MOUSE
        // hook was tried first, but WH_MOUSE only ever sees messages
        // destined for windows on the *same thread* that installed it -
        // confirmed live: clicking elsewhere in this app's own window
        // dismissed the popup correctly, but clicking a completely
        // different application's window (a different process/thread)
        // never reached the hook at all, leaving the popup stuck open.
        // WH_MOUSE_LL sees raw physical mouse input system-wide,
        // regardless of which process/thread the click is destined for,
        // which is what "click anywhere else dismisses this" actually
        // needs. The hook only inspects each click's screen point against
        // GetWindowRect() and never calls SetCapture/eats the message, so
        // normal child-HWND click routing inside the popup - and every
        // other application's own input - is unaffected.
        HHOOK mouseHook_ = nullptr;
        void installOutsideClickHook();
        void removeOutsideClickHook();
        static LRESULT CALLBACK MouseHookProc(int code, WPARAM wParam, LPARAM lParam);

        // dismiss() posts this to itself from MouseHookProc rather than
        // acting directly inside the hook - a hook procedure should stay
        // minimal and not reenter window/message state from within
        // another thread's (or even this same thread's, mid-dispatch)
        // hook chain.
        static constexpr UINT kDismissMessage = WM_APP + 0x317;
    };

}

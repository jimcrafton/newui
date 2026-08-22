#include "newui/popupframe.h"

#include <dwmapi.h>

#include "newui/application.h"

namespace newui {

    namespace {
        // Only one PopupFrame's outside-click hook is ever expected to be
        // active on this thread at a time in practice - a DropDownList
        // closes its own popup before another one would plausibly open in
        // the same window. thread_local (not a plain global) so this still
        // does the right thing if a future caller runs more than one UI
        // thread. See PopupFrame::mouseHook_'s own doc comment (popupframe.h)
        // for why a hook is used here instead of SetCapture().
        thread_local PopupFrame* t_hookOwner = nullptr;
    }

    PopupFrame::PopupFrame() {
    }

    PopupFrame::~PopupFrame() {
        removeOutsideClickHook();
    }

    bool PopupFrame::initialize(HWND owner) {
        owner_ = owner;

        WNDCLASSEXA wcex = {};
        std::string className = "PopupFrame";
        wcex.cbSize = sizeof(wcex);
        // CS_DROPSHADOW - a real dropdown/flyout's own subtle shadow,
        // matching the user's own spec for this class. CS_HREDRAW/
        // CS_VREDRAW/CS_DBLCLKS carried over from Frame::initialize() -
        // same reasoning, unrelated to the popup-specific styling below.
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS | CS_DROPSHADOW;
        wcex.lpfnWndProc = &Frame::WndProc;
        wcex.hInstance = Application::instance().instanceHandle();
        wcex.lpszClassName = className.c_str();
        ::RegisterClassExA(&wcex);

        const Rect& b = getBounds();
        HWND hwnd = ::CreateWindowExA(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            className.c_str(), "", WS_POPUP | WS_BORDER,
            b.left(), b.top(), b.size().width, b.size().height,
            owner_, NULL,
            Application::instance().instanceHandle(),
            this
        );

        if (!hwnd) {
            return false;
        }

        // frameHandle() was set in Frame::WndProc during WM_NCCREATE.
        if (hwnd != frameHandle()) {
            return false;
        }

        // Best-effort - DWMWA_WINDOW_CORNER_PREFERENCE only exists on
        // Windows 11+; DwmSetWindowAttribute() simply fails harmlessly on
        // older builds, same "square corners, nothing else different"
        // fallback every other app not opting in already gets there.
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        ::DwmSetWindowAttribute(frameHandle(), DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        return true;
    }

    void PopupFrame::moveTo(const Rect& screenBounds) {
        setBounds(screenBounds);
        if (frameHandle() != nullptr) {
            ::SetWindowPos(frameHandle(), NULL,
                int(screenBounds.left()), int(screenBounds.top()),
                int(screenBounds.size().width), int(screenBounds.size().height),
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void PopupFrame::show() {
        if (frameHandle() == nullptr) {
            return;
        }
        // SW_SHOWNOACTIVATE, never SW_SHOW - this window must not steal
        // activation from owner_, consistent with WS_EX_NOACTIVATE.
        ::ShowWindow(frameHandle(), SW_SHOWNOACTIVATE);
        ::RedrawWindow(frameHandle(), NULL, NULL, RDW_INVALIDATE | RDW_ERASENOW | RDW_ALLCHILDREN);
        installOutsideClickHook();
    }

    void PopupFrame::hide() {
        if (frameHandle() == nullptr) {
            return;
        }
        removeOutsideClickHook();
        ::ShowWindow(frameHandle(), SW_HIDE);
    }

    bool PopupFrame::isVisible() const {
        return frameHandle() != nullptr && ::IsWindowVisible(frameHandle()) != FALSE;
    }

    void PopupFrame::installOutsideClickHook() {
        if (mouseHook_ != nullptr) {
            return;
        }
        t_hookOwner = this;
        // WH_MOUSE_LL - system-wide, not thread-scoped - dwThreadId must
        // be 0 and hMod may be NULL since the hook procedure lives in this
        // same process (not a separate DLL injected into others). See
        // mouseHook_'s own doc comment (popupframe.h) for why a plain
        // (thread-scoped) WH_MOUSE hook isn't enough here.
        mouseHook_ = ::SetWindowsHookExA(WH_MOUSE_LL, &PopupFrame::MouseHookProc, NULL, 0);
    }

    void PopupFrame::removeOutsideClickHook() {
        if (mouseHook_ == nullptr) {
            return;
        }
        ::UnhookWindowsHookEx(mouseHook_);
        mouseHook_ = nullptr;
        if (t_hookOwner == this) {
            t_hookOwner = nullptr;
        }
    }

    LRESULT CALLBACK PopupFrame::MouseHookProc(int code, WPARAM wParam, LPARAM lParam) {
        // HC_ACTION is the only code carrying real message data - other
        // (rare, undocumented-use) codes must be passed straight through
        // without inspecting lParam. WH_MOUSE_LL only ever delivers the
        // raw client-area message codes (WM_LBUTTONDOWN/WM_RBUTTONDOWN/
        // WM_MBUTTONDOWN) - non-client (WM_NC*) variants don't exist at
        // this level, since this runs before any window's own hit-testing.
        if (code == HC_ACTION && t_hookOwner != nullptr) {
            switch (wParam) {
                case WM_LBUTTONDOWN:
                case WM_RBUTTONDOWN:
                case WM_MBUTTONDOWN: {
                    auto* hookStruct = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
                    RECT windowRect = {};
                    HWND popupHwnd = t_hookOwner->frameHandle();
                    if (popupHwnd != nullptr && ::GetWindowRect(popupHwnd, &windowRect)) {
                        if (!::PtInRect(&windowRect, hookStruct->pt)) {
                            // Don't act on window/hook state directly from
                            // inside the hook chain - post back to the
                            // popup's own WndProc instead, same "keep the
                            // hook itself minimal" reasoning as this file's
                            // own class comment.
                            ::PostMessage(popupHwnd, PopupFrame::kDismissMessage, 0, 0);

                            // Swallow this one click - return non-zero
                            // instead of falling through to
                            // CallNextHookEx() below, which blocks it from
                            // ever reaching whatever window is actually
                            // under the cursor (same real WH_MOUSE_LL/
                            // WH_KEYBOARD_LL technique global input-
                            // blocking utilities use). Without this, the
                            // same physical click both dismissed the
                            // popup *and* landed on whatever was
                            // underneath it as a real, separate click of
                            // its own - confirmed live - whereas a real
                            // combo box's first outside click only ever
                            // closes the dropdown.
                            return 1;
                        }
                    }
                }
                break;
                default:
                    break;
            }
        }
        return ::CallNextHookEx(NULL, code, wParam, lParam);
    }

    bool PopupFrame::handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT) {
        switch (message) {
            case WM_CREATE: {
                // Unlike Frame::handleMessage()'s own WM_CREATE case, a
                // failure here does not PostMessage(WM_QUIT) - this is a
                // transient popup, not the application shutting down; on
                // failure frameCreated() has already torn rootView_ back
                // down itself (see Frame::frameCreated()), so there's
                // nothing further to do beyond letting window creation
                // continue with a popup that simply has no content.
                frameCreated();
                return true;
            }

            case WM_DESTROY: {
                // Deliberately no PostQuitMessage(0) - the one behavior
                // that would be actively wrong to inherit unchanged from
                // Frame::handleMessage()'s own WM_DESTROY case, since
                // destroying a dropdown popup must never quit the
                // application's own message loop.
                destroy();
                return true;
            }

            default: {
                if (message == kDismissMessage) {
                    hide();
                    onDismissed(*this);
                    return true;
                }
                return Frame::handleMessage(message, wParam, lParam, outLRESULT);
            }
        }
    }

}

#include "newui/toolframe.h"
#include "newui/win32/nativehandles.h"

#include "newui/application.h"

namespace newui {

    ToolFrame::ToolFrame() {
    }

    // As Dialog::~Dialog and DropDownList do: Frame's destructor throws when its window is still alive, so take it down here, while
    // handleMessage() still resolves to this class (its WM_DESTROY does not quit the application).
    ToolFrame::~ToolFrame() {
        if (newui::win32::toHWND(frameHandle()) != nullptr) {
            ::DestroyWindow(newui::win32::toHWND(frameHandle()));
        }
    }

    bool ToolFrame::initialize(NativeWindowHandle owner) {
        if (owner == nullptr) {
            return false;
        }
        owner_ = owner;

        WNDCLASSEXA wcex = {};
        std::string className = "ToolFrame";
        wcex.cbSize = sizeof(wcex);
        // No CS_DROPSHADOW (PopupFrame's): a captioned, sized window already gets the system's own shadow, and the class shadow doubles it.
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wcex.lpfnWndProc = reinterpret_cast<WNDPROC>(&Frame::WndProc);
        wcex.hInstance = newui::win32::toHINSTANCE(Application::instance().instanceHandle());
        wcex.lpszClassName = className.c_str();
        ::RegisterClassExA(&wcex);

        const Rect& b = getBounds();
        HWND hwnd = ::CreateWindowExA(
            activatesOnClick_ ? WS_EX_TOOLWINDOW : (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE),
            className.c_str(), title_.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
            b.left(), b.top(), b.size().width, b.size().height,
            newui::win32::toHWND(owner_), NULL,
            newui::win32::toHINSTANCE(Application::instance().instanceHandle()),
            this
        );
        if (!hwnd || hwnd != newui::win32::toHWND(frameHandle())) {
            return false;
        }
        return true;
    }

    void ToolFrame::setActivatesOnClick(bool activates) {
        activatesOnClick_ = activates;
        if (newui::win32::toHWND(frameHandle()) == nullptr) {
            return;
        }
        LONG_PTR style = ::GetWindowLongPtr(newui::win32::toHWND(frameHandle()), GWL_EXSTYLE);
        style = activates ? (style & ~LONG_PTR(WS_EX_NOACTIVATE)) : (style | WS_EX_NOACTIVATE);
        ::SetWindowLongPtr(newui::win32::toHWND(frameHandle()), GWL_EXSTYLE, style);
    }

    void ToolFrame::moveTo(const Rect& screenBounds) {
        setBounds(screenBounds);
        if (newui::win32::toHWND(frameHandle()) != nullptr) {
            ::SetWindowPos(newui::win32::toHWND(frameHandle()), NULL,
                int(screenBounds.left()), int(screenBounds.top()),
                int(screenBounds.size().width), int(screenBounds.size().height),
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void ToolFrame::show() {
        if (newui::win32::toHWND(frameHandle()) == nullptr) {
            return;
        }
        ::ShowWindow(newui::win32::toHWND(frameHandle()), SW_SHOWNOACTIVATE);
        ::RedrawWindow(newui::win32::toHWND(frameHandle()), NULL, NULL, RDW_INVALIDATE | RDW_ERASENOW | RDW_ALLCHILDREN);
    }

    void ToolFrame::hide() {
        if (newui::win32::toHWND(frameHandle()) != nullptr) {
            ::ShowWindow(newui::win32::toHWND(frameHandle()), SW_HIDE);
        }
    }

    bool ToolFrame::isVisible() const {
        return newui::win32::toHWND(frameHandle()) != nullptr && ::IsWindowVisible(newui::win32::toHWND(frameHandle())) != FALSE;
    }

    bool ToolFrame::handleMessage(NativeMessage message, NativeWParam wParam, NativeLParam lParam, NativeLResult& outLRESULT) {
        switch (message) {
            case WM_CREATE:
                frameCreated();
                return true;

            // Closing the application's main window destroys the owned tool window; it must not quit the message loop itself.
            case WM_DESTROY:
                destroy();
                return true;

            case WM_CLOSE:
                hide();
                onClosed(*this);
                outLRESULT = 0;
                return true;

            case WM_GETMINMAXINFO: {
                auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
                info->ptMinTrackSize.x = LONG(minimumSize_.width);
                info->ptMinTrackSize.y = LONG(minimumSize_.height);
                outLRESULT = 0;
                return true;
            }

            // Unless asked to, never activate on a click: the owner keeps the keyboard.
            case WM_MOUSEACTIVATE:
                if (activatesOnClick_) {
                    return Frame::handleMessage(message, wParam, lParam, outLRESULT);
                }
                outLRESULT = MA_NOACTIVATE;
                return true;

            default:
                return Frame::handleMessage(message, wParam, lParam, outLRESULT);
        }
    }

}

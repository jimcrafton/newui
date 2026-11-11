#include "newui/toolframe.h"

#include "newui/application.h"

namespace newui {

    ToolFrame::ToolFrame() {
    }

    // As Dialog::~Dialog and DropDownList do: Frame's destructor throws when its window is still alive, so take it down here, while
    // handleMessage() still resolves to this class (its WM_DESTROY does not quit the application).
    ToolFrame::~ToolFrame() {
        if (frameHandle() != nullptr) {
            ::DestroyWindow(frameHandle());
        }
    }

    bool ToolFrame::initialize(HWND owner) {
        if (owner == nullptr) {
            return false;
        }
        owner_ = owner;

        WNDCLASSEXA wcex = {};
        std::string className = "ToolFrame";
        wcex.cbSize = sizeof(wcex);
        // No CS_DROPSHADOW (PopupFrame's): a captioned, sized window already gets the system's own shadow, and the class shadow doubles it.
        wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
        wcex.lpfnWndProc = &Frame::WndProc;
        wcex.hInstance = Application::instance().instanceHandle();
        wcex.lpszClassName = className.c_str();
        ::RegisterClassExA(&wcex);

        const Rect& b = getBounds();
        HWND hwnd = ::CreateWindowExA(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            className.c_str(), title_.c_str(), WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_CLIPCHILDREN,
            b.left(), b.top(), b.size().width, b.size().height,
            owner_, NULL,
            Application::instance().instanceHandle(),
            this
        );
        if (!hwnd || hwnd != frameHandle()) {
            return false;
        }
        return true;
    }

    void ToolFrame::moveTo(const Rect& screenBounds) {
        setBounds(screenBounds);
        if (frameHandle() != nullptr) {
            ::SetWindowPos(frameHandle(), NULL,
                int(screenBounds.left()), int(screenBounds.top()),
                int(screenBounds.size().width), int(screenBounds.size().height),
                SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }

    void ToolFrame::show() {
        if (frameHandle() == nullptr) {
            return;
        }
        ::ShowWindow(frameHandle(), SW_SHOWNOACTIVATE);
        ::RedrawWindow(frameHandle(), NULL, NULL, RDW_INVALIDATE | RDW_ERASENOW | RDW_ALLCHILDREN);
    }

    void ToolFrame::hide() {
        if (frameHandle() != nullptr) {
            ::ShowWindow(frameHandle(), SW_HIDE);
        }
    }

    bool ToolFrame::isVisible() const {
        return frameHandle() != nullptr && ::IsWindowVisible(frameHandle()) != FALSE;
    }

    bool ToolFrame::handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT) {
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

            // Never activate on a click: the owner keeps the keyboard.
            case WM_MOUSEACTIVATE:
                outLRESULT = MA_NOACTIVATE;
                return true;

            default:
                return Frame::handleMessage(message, wParam, lParam, outLRESULT);
        }
    }

}

#include "newui/runloop.h"
#include "newui/keyboard_constants.h"
#include "newui/utils.h"
#include "newui/Application.h"
#include "newui/Frame.h"
#include <bitset>


namespace newui {

    void RunLoop::run() {

        threadId_ = ::GetCurrentThreadId();

        HRESULT hr = OleInitialize(NULL);
        if (!SUCCEEDED(hr)) {
            throw std::runtime_error("newui::RunLoop: OleInitialize failed");
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = true;
        }
        startedCv_.notify_all();

        MSG msg;
        BOOL getMessageResult;
		BYTE keyState[256];
		HACCEL hAccelTable = NULL;

        bool done = false;
		bool isIdle = true;

        while (!done ) {
			while (isIdle && (!::PeekMessage(&msg, NULL, NULL, NULL, PM_NOREMOVE))) {
				//internal_idleTime();
				isIdle = false;
			}

			do {
				bool doTranslateAndDispatch = true;

				bool ESCkeyPressed = false;
				if (GetMessage(&msg, NULL, 0, 0)) {
					switch (msg.message) {

						case kTaskMessage: {
							drainTasks();
							doTranslateAndDispatch = false;
						}
						break;

						case WM_KEYDOWN:  case WM_SYSKEYDOWN: {
							int scanCode = ((BYTE*)&msg.lParam)[2]; //gets bits 16-23
							int VKeyCode = 0;
							bool altKeyDown = false;
							int isExtendedKey = 0;
							int keyMask = 0;
							WORD character = 0;
							memset(keyState, 0, sizeof(keyState));

							if (GetKeyboardState(&keyState[0])) {
								altKeyDown = (msg.lParam & KB_CONTEXT_CODE) != 0;
								isExtendedKey = (msg.lParam & KB_IS_EXTENDED_KEY) != 0;
								character = 0;

								VKeyCode = MapVirtualKey(scanCode, 1);

							}

							HKL keyboardLayout = GetKeyboardLayout(GetWindowThreadProcessId(msg.hwnd, NULL));

							ToAsciiEx(VKeyCode, scanCode, &keyState[0], &character, 1, keyboardLayout);
							std::bitset<16> keyBits;
							keyBits = GetAsyncKeyState(VK_SHIFT);
							if (keyBits[15] == 1) {
								keyMask |= kmShift;
							}

							keyBits = GetAsyncKeyState(VK_CONTROL);
							if (keyBits[15] == 1) {
								keyMask |= kmCtrl;
							}

							keyBits = GetAsyncKeyState(VK_MENU);
							if (keyBits[15] == 1) {
								altKeyDown = true;
								keyMask |= kmAlt;
							}


							VirtualKeyCode vkCode = (VirtualKeyCode)translateVirtualKey(msg.wParam, character);

							ESCkeyPressed = (vkCode == vkEscape);
							
							auto [frameTarget,viewTarget] = Application::instance().getFrame()->getTarget(msg.hwnd);

							bool msgConsumed = false;

							if (frameTarget != nullptr) {
								//do nothing ....
							}
							else if (viewTarget != nullptr) {
								//figure out if the view or sub view handles things
								//if they process the msg then 
								//msgConsumed = true
							}


							if (msgConsumed) {
								doTranslateAndDispatch = false;
							}
						}
					}
					if (doTranslateAndDispatch) {
						if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg)) {
							TranslateMessage(&msg);
							DispatchMessage(&msg);
						}
					}
				}
				else {
					done = true;
					break;
				}

				switch (msg.message) {
					case WM_KEYDOWN: {
						if (ESCkeyPressed) {
							
						}
					}
					break;

					case WM_SYSCOMMAND: {
						if (SC_CLOSE == msg.wParam) {							
							PostQuitMessage(0);
						}
					}
					break;

					case WM_QUIT: {
						done = true;
					}
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

        
    }

    void RunLoop::postTaskMsg() const
    {
        ::PostThreadMessage(threadId_, kTaskMessage, 0, 0);
    }

    void RunLoop::postQuitMsg() const
    {
        ::PostThreadMessage(threadId_, WM_QUIT, 0, 0);
    }
}
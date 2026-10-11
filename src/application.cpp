#include "newui/application.h"

#include "newui/frame.h"
#include "newui/newui.h"
#include "newui/runloop.h"
#include "newui/themedata.h"
#include "newui/uicolormanager.h"

#include <uxtheme.h>

#include <stdexcept>

// Forces the linker to merge a comctl32 v6 ("visual styles") dependency
// into whatever final .exe links this static library - without it,
// OpenThemeData() (see ThemedViewStyle, viewstyle.h/.cpp) silently returns
// null even with InitCommonControlsEx() called below, since theming
// requires the process to declare this dependency in its manifest. A
// manifest *resource* embedded in a static library's own .res doesn't
// propagate into the final .exe's linked-in manifest the way object-file
// symbols do, but this linker-pragma directive is honored from any .obj
// pulled into the final link - so it reaches every consumer (examples,
// unittests, newui_app) automatically, with nothing to add on their side.
#pragma comment(linker, \
    "\"/manifestdependency:type='Win32' name='Microsoft.Windows.Common-Controls' " \
    "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace newui {

std::atomic<bool> Application::instantiated_{false};

Application& Application::instance() {
    static Application instance;
    return instance;
}

Application::Application() {
    bool expected = false;
    if (!instantiated_.compare_exchange_strong(expected, true)) {
        throw std::logic_error("newui::Application: only one instance may be created");
    }
}

Application::~Application() = default;

void Application::setName(const std::string& name) {
    
    name_ = name;
}


void Application::setFrame(Frame* frame) {
    
    frame_ = frame;
}

LRESULT CALLBACK Application::DummyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;	

	switch (message) {

		case WM_COPYDATA: {
			COPYDATASTRUCT* cpd = (COPYDATASTRUCT*)lParam;
			if (NULL != cpd) {
				
			}
		}
		break;

		case WM_INPUTLANGCHANGE: {
		
		}
		break;

		case WM_SETTINGCHANGE: {
			//settings change!						
		}
		break;

		case WM_ACTIVATEAPP: {

			BOOL fActive = (BOOL)wParam;

			/**
			this is a total hack to get around the fact that if I create the dummyParentWnd_
			with a WS_CHILD style it causes a WM_NCACTIVATE with a active=0 to teh new popup
			window, which de-activates the caption bar and focus. However if I change the
			creation style to WS_POPUP it avoids the above, but then any time a popup window
			(like that of the ComboBox popup) is created it causes the main parent window to
			lose focus. So...create the dummyParentWnd_ with a WS_POPUP style to avoid the first
			goof, and then switch it back to a WS_CHILD style here to avoid the other goof.
			Of course, ideally it would be nice to never get either goof, but...
			*/
			static bool changeStyle = true;			

			if (changeStyle) {

				LONG_PTR style = ::GetWindowLongPtr(Application::instance().dummyWindowHandle(), GWL_STYLE);
				if (style & WS_POPUP) {
					style &= ~WS_POPUP;
					style |= WS_CHILD;
					::SetWindowLongPtr(Application::instance().dummyWindowHandle(), GWL_STYLE, style);
					::SetWindowPos(Application::instance().dummyWindowHandle(), NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
				}
			}
			changeStyle = false;

			if (fActive) {
				//activate main frmae
			}
			else {
				//kill tool tips
			}
		}
		break;

		case WM_CREATE: {

		}
		break;

		case WM_TIMER: {
			UINT wTimerID = wParam;
			//tooltip stuff if we want it
		}
		break;

		case WM_DESTROY: {
			result = DefWindowProcA(hWnd, message, wParam, lParam);			
		}
		break;

		case WM_NOTIFY: {
			NMHDR* notificationHdr = (LPNMHDR)lParam;
			//use notificationHdr->hwndFrom to get the HWND of the control that sent the notification
			
			/*
			else {
				//handle some special case messages here that wouldn't ordinarily get caught
				//because they are from child windows of a common control (like the header control in a listview)
				switch (notificationHdr->code) {
					case HDN_GETDISPINFOW: case HDN_ITEMCHANGING: case HDN_TRACK: case HDN_ENDTRACK: case HDN_BEGINTRACK: {
						HWND parent = ::GetParent(notificationHdr->hwndFrom);

					}
					break;
				}
			}
			*/
		}
		break;

		case WM_COMMAND: {
			HWND hwndCtl = (HWND)lParam;		
			
			result = DefWindowProcA(hWnd, message, wParam, lParam);
		}
		break;

		case WM_DRAWITEM: {
			DRAWITEMSTRUCT* drawItem = (DRAWITEMSTRUCT*)lParam;
			if (ODT_BUTTON == drawItem->CtlType ||
				ODT_COMBOBOX == drawItem->CtlType) {
				HWND hwndCtl = drawItem->hwndItem;
				
				
			}
			else {				
				
			}
			result = DefWindowProcA(hWnd, message, wParam, lParam);
		}
		break;

		case WM_MEASUREITEM: {
			MEASUREITEMSTRUCT* measureItem = (MEASUREITEMSTRUCT*)lParam;
			if (ODT_COMBOBOX == measureItem->CtlType) {
				HWND hwndCtl = GetDlgItem(hWnd, measureItem->CtlID);

				result = DefWindowProcA(hWnd, message, wParam, lParam);
			}
			else {
				result = DefWindowProcA(hWnd, message, wParam, lParam);
			}
		}
		break;
		default: {
			//post event?

			result = DefWindowProcA(hWnd, message, wParam, lParam);
		}
		break;
	}

    return result;
}

SyncReturn FrameDestroyed(Frame& frame) {
	
    DestroyWindow(Application::instance().dummyWindowHandle());

	return SyncReturn::Handled;
}

void Application::run() {
    if (nullptr == frame_) {
        return;
    }
    
	try {
		instanceHandle_ = ::GetModuleHandle(NULL);

		// Declares this process per-monitor-v2 DPI aware, so Windows stops
		// bitmap-stretching our windows on a non-96-DPI display (a
		// DPI-unaware process renders blurry and wrong-sized there
		// instead). Has to run before any window class is registered or
		// any window created - RegisterClassExA/CreateWindowA below read
		// the calling thread's current DPI awareness context. Not fatal
		// if it fails (e.g. pre-Windows 10 1703) - same "degrade, don't
		// throw" convention as BufferedPaintInit() just below. This only
		// stops the blur, though: View/Layout/RootView's own pixel-based
		// coordinates aren't scaled for DPI yet, so content can still
		// render at physical-pixel (i.e. too-small-looking) size on a
		// scaled display - real per-DPI scaling is a separate, larger
		// follow-up task, not attempted here.
		if (!::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
			printf("newui::Application: SetProcessDpiAwarenessContext failed - window may render blurry on a scaled display\n");
		}

		// Opts this process into following the system Light/Dark mode
		// setting on native surfaces this toolkit doesn't draw itself -
		// chiefly a ContextMenu's real TrackPopupMenuEx() popup
		// (uicolormanager.h) - undocumented, so silently a no-op on a
		// Windows build old enough not to export it, same "degrade,
		// don't throw" convention as the DPI awareness call just above.
		enableProcessDarkModeSupport();

		// Loads Resources/Themes/light.theme or dark.theme (themedata.h),
		// if either exists next to the .exe - a no-op (isLoaded() stays
		// false) for an app that never ran tools/themesgen/themesgen.py,
		// so this doesn't change behavior for anything that doesn't have
		// one. Frame's own WM_SETTINGCHANGE("ImmersiveColorSet") handler
		// re-runs this on every live Light/Dark toggle - this call is
		// just what gets the very first paint already using it too,
		// rather than only after the first live toggle.
		ThemeData::instance().reloadForCurrentMode();

		INITCOMMONCONTROLSEX icc{};
		icc.dwSize = sizeof(icc);
		icc.dwICC = ICC_WIN95_CLASSES;
		if (!::InitCommonControlsEx(&icc)) {
			throw std::runtime_error("newui::Application: InitCommonControlsEx failed");
		}

		// BufferedPaintInit()/UnInit() bracket any use of the buffered-paint
		// APIs (BeginBufferedPaint/GetBufferedPaintBits/EndBufferedPaint) on
		// this thread - required before ThemedViewStyle::paint() (viewstyle.cpp)
		// can use them. Not treated as fatal if it fails - a themed style's
		// paint() already no-ops gracefully when it can't render (see its
		// comment), so a plain View just misses its native chrome rather than
		// the whole application failing to start.
		bool bufferedPaintInitialized = true;
		if (FAILED(::BufferedPaintInit())) {
			printf("newui::Application: BufferedPaintInit failed - themed styles won't render\n");
			bufferedPaintInitialized = false;
		}

		HWND parent = ::GetDesktopWindow();

		//RegisterWin32ToolKitClass(::GetModuleHandleW(NULL));
		WNDCLASSEXA wcex;
		std::string className = "Application-dummy-" + name_;

		wcex.cbSize = sizeof(wcex);

		wcex.style = 0;
		wcex.lpfnWndProc = (WNDPROC)Application::DummyWndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = instanceHandle_;
		wcex.hIcon = NULL;
		wcex.hCursor = NULL;
		wcex.hbrBackground = 0;
		wcex.lpszMenuName = NULL;
		wcex.lpszClassName = className.c_str();
		wcex.hIconSm = NULL;

		::RegisterClassExA(&wcex);


		dummyWindowHandle_ = ::CreateWindowA(className.c_str(), NULL, WS_POPUP, 0, 0, 0, 0, parent, NULL, instanceHandle_, NULL);


		if (!frame_->initialize()) {
			throw std::runtime_error("newui::Application: Failed to initialize frame");
		}

		frame_->onDestroyed += FrameDestroyed;


		onStartup(*this);

		//runLoop_.onEnding += Application::runLoopEnding;
		//runLoop_.onEnd += Application::runLoopDone;

		RunLoop::current().run();

		if (bufferedPaintInitialized) {
			::BufferedPaintUnInit();
		}

		onShutdown(*this);
	}
	catch (const std::exception& e) {
		printf("exception trapped: %s", e.what());
		throw e;
	}
}

SyncReturn Application::runLoopEnding(RunLoop&)
{
	return newui::SyncReturn::Handled;
}

SyncReturn Application::runLoopDone(RunLoop&)
{
	return newui::SyncReturn::Handled;
}

}
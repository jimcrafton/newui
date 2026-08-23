#include "newui/frame.h"
#include "newui/application.h"
#include "newui/menus.h"
#include "newui/themedata.h"
#include "newui/uicolormanager.h"

namespace newui {

	Frame::Frame()
	{
		rootView_ = new RootView(this, bounds_, "rootview");
	}

	bool Frame::renderAllViewsToFile(const std::string& path) {
		rootView_->markDirty();

		BLImage& buffer = rootView_->getImageBuffer();
		if (buffer.is_empty()) {
			return false;
		}

		return buffer.write_to_file(path.c_str()) == BL_SUCCESS;
	}

	Frame::~Frame()
	{
		if (nullptr != rootView_) {
			if (nullptr != frameHandle_) {
				// A live native window still exists and was never torn
				// down through the normal WM_CLOSE/WM_DESTROY path (see
				// destroy()) - that's a real leak (both the HWND and
				// rootView_ still alive), so keep failing loudly instead
				// of silently leaking it.
				throw std::runtime_error("root view not destroyed!!!");
			}

			// initialize() was never called (or never got as far as
			// creating frameHandle_) - there's no live window and never
			// will be, so there's nothing for the normal WM_DESTROY-driven
			// teardown (destroy()) to wait for. Free rootView_ directly
			// instead - safe even if the caller already built content on
			// it via rootView() (View::destroy()/SubView::destroy() need
			// no live HWND). Skips destroy()'s onDestroyed(*this) - this
			// Frame was never actually shown, so nothing was "destroyed"
			// in the sense any onDestroyed listener would expect.
			rootView_->destroy();
			delete rootView_;
			rootView_ = nullptr;
		}
	}


void Frame::setTitle(const std::string& title)
{
	if (title_ == title) {
		return;
	}
	std::string oldTitle = title_;
	title_ = title;
	onTitleChanged.syncCall(*this, oldTitle, title);
}

void Frame::setBounds(const Rect& bounds)
{

	if (bounds == bounds_) {
		return;
	}

	bool notifySizeChange = (bounds.size() != bounds_.size());
	bool notifyPosChange = (bounds.pos() != bounds_.pos());

	bounds_ = bounds;

	if (notifySizeChange) {
		onSizeChanged(*this, bounds.size());
	}
	if (notifyPosChange) {
		onPosChanged(*this, bounds.pos());
	}

	updateViewBounds();
}

#define FRAME_WINDOW			WS_OVERLAPPEDWINDOW


LRESULT CALLBACK Frame::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	Frame* thisPtr = nullptr;
	if (message == WM_NCCREATE) {
		// Extract the 'this' pointer from CREATESTRUCT passed via CreateWindowEx
		auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
		thisPtr = reinterpret_cast<Frame*>(cs->lpCreateParams);
		// Associate the pointer with the HWND for future messages
		::SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(thisPtr));

		// Save the handle inside the object
		thisPtr->frameHandle_ = hWnd;
	}
	else {
		thisPtr = reinterpret_cast<Frame*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (thisPtr) {
		LRESULT lres = 0;
		if (!thisPtr->handleMessage(message, wParam, lParam, lres)) {
			return DefWindowProcA(hWnd, message, wParam, lParam);
		}
		else {
			return lres;
		}
	}


	return 0;
}

void Frame::updateViewBounds()
{
	if (nullptr != rootView_) {
		RECT clientRect = {};
		if (GetClientRect(frameHandle_, &clientRect)) {
			Size clientSize(clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
			rootView_->setBounds(Rect(Point(0,0), clientSize));
		}
	}
}

void Frame::sizeChange(const Size& newSize)
{
	if (bounds_.size() == newSize) {
		return;
	}

	auto tmp = bounds_;
	tmp.setSize(newSize);
	setBounds(tmp);
}

bool Frame::handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT)
{
	bool result = false;
	outLRESULT = 0;

	switch (message) {
		case WM_CREATE: {
			if (!frameCreated()) {
				::PostMessage(frameHandle_, WM_QUIT, 0, 0);
			}
			result = true;
		}
		break;

		case WM_SIZE: {
			Size sz(LOWORD(lParam), HIWORD(lParam));			
			sizeChange(sz);
			result = true;
		}
		break;

		case WM_MOVE: {
			Point pt(LOWORD(lParam),HIWORD(lParam));
			onPosChanged(*this, pt);
			result = true;
		}
		break;

		case WM_CLOSE: {
			// Handle window close event
			// You can add custom logic here if needed
			onClosed(*this);

			DestroyWindow(frameHandle_);

			result = true;
		}
		break;

		case WM_DESTROY: {
			destroy();

			::PostQuitMessage(0);
			result = true;
		}
		break;

		case WM_QUERYENDSESSION: {
			// wParam is reserved/unused; lParam carries ENDSESSION_*
			// reason flags (Vista+). We answer this ourselves (outLRESULT),
			// so no fall-through to DefWindowProcA needed.
			bool canEnd = true;
			Application::instance().onQueryEndSession(Application::instance(), canEnd,
				static_cast<std::uint32_t>(lParam));
			outLRESULT = canEnd ? TRUE : FALSE;
			result = true;
		}
		break;

		case WM_ENDSESSION: {
			Application::instance().onEndSession(Application::instance(), wParam != 0,
				static_cast<std::uint32_t>(lParam));
			result = true;
		}
		break;

		case WM_THEMECHANGED: {
			// Drop every ThemedViewStyle's cached HTHEME across this
			// Frame's whole view tree and redraw before anyone gets
			// notified, so onThemeChanged subscribers already see the new
			// look reflected if they query anything view-related. Still
			// falls through to DefWindowProcA afterward (result stays
			// false) - it does its own default WM_THEMECHANGED handling
			// for the window's native non-client chrome, which this
			// doesn't touch.
			if (nullptr != rootView_) {
				rootView_->refreshThemes();
			}
			Application::instance().onThemeChanged(Application::instance());
		}
		break;

		case WM_DWMCOLORIZATIONCOLORCHANGED: {
			// Same "refresh then notify" ordering as WM_THEMECHANGED above -
			// an accent-color change affects themed chrome just as much as
			// an actual theme switch does.
			if (nullptr != rootView_) {
				rootView_->refreshThemes();
			}
			// wParam is already exactly the 0xAARRGGBB layout Color's
			// (uint32_t, hasAlpha) constructor expects - see
			// ColorizationColorChangedDelegate's doc comment in
			// application.h for why this isn't a Win32 COLORREF.
			Application::instance().onColorizationColorChanged(Application::instance(),
				Color(static_cast<std::uint32_t>(wParam), true), lParam != 0);
		}
		break;

		case WM_SETTINGCHANGE: {
			// lParam is an ANSI string pointer here (not wide) - Frame's
			// window class was registered via RegisterClassExA, so Windows
			// has already thunked this message to the ANSI form by the
			// time it reaches this WndProc, same as every other string-ish
			// Win32 API this codebase calls with an explicit "A" suffix.
			std::string settingName;
			if (lParam != 0) {
				settingName = reinterpret_cast<const char*>(lParam);
			}

			// "ImmersiveColorSet" is what Windows actually sends for a
			// light/dark mode toggle (Settings > Personalization > Colors),
			// with wParam always 0 - it's a string-only, undocumented
			// notification, not one of the SPI_*-coded settings changes
			// that use wParam for an action code. WM_THEMECHANGED is a
			// different, less common setting (switching between whole
			// .theme files) and isn't guaranteed to fire from a light/
			// dark flip, which is why this needs its own check rather
			// than relying on that case alone. See
			// RootView::refreshThemes()'s doc comment for what this
			// does and does not actually change visually.
			if (settingName == "ImmersiveColorSet") {
				// Un-stick native ContextMenu popups from whichever mode
				// was cached at first use - see
				// refreshNativeMenuDarkModePolicy()'s own doc comment
				// (uicolormanager.h) for why refreshThemes() alone
				// (app-drawn chrome only) doesn't reach them.
				refreshNativeMenuDarkModePolicy();
				// Picks up the matching Themes/light.theme or dark.theme
				// (themedata.h) live - before refreshThemes() below
				// triggers the actual repaint, so that repaint already
				// sees the newly-loaded data instead of needing a second
				// toggle to catch up. A no-op (isLoaded() stays whatever
				// it already was) if neither file exists.
				ThemeData::instance().reloadForCurrentMode();
				if (nullptr != rootView_) {
					rootView_->refreshThemes();
				}
			}

			Application::instance().onSettingChange(Application::instance(),
				static_cast<std::uint32_t>(wParam), settingName);
		}
		break;

		case WM_MEASUREITEM: {
			// Owner-drawn items inside a ContextMenu popup shown with
			// this Frame's window as TrackPopupMenu's owner arrive here -
			// stateless (identifies the MenuItem via itemData directly),
			// so no MenuBar/ContextMenu instance is needed - see menus.h.
			result = DispatchMenuMeasureItem(*reinterpret_cast<MEASUREITEMSTRUCT*>(lParam));
		}
		break;

		case WM_DRAWITEM: {
			result = DispatchMenuDrawItem(*reinterpret_cast<DRAWITEMSTRUCT*>(lParam));
		}
		break;

		default: {
			result = false; // Message not handled
		}			
		break;
	}

	return result;
}

bool Frame::frameCreated()
{
	//cretate main view

	if (!rootView_->initialize()) {
		delete rootView_;
		rootView_ = nullptr;

		return false;
	}

	onCreated(*this);
	return true;
}

void Frame::destroy()
{
	onDestroyed(*this);

	if (nullptr != rootView_) {
		rootView_->destroy();
		delete rootView_;
	}
	rootView_ = nullptr;	
}

bool Frame::initialize()
{
	bool result = true;
	WNDCLASSEXA wcex;
	std::string className = "Frame";
	wcex.cbSize = sizeof(wcex);

	wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
	wcex.lpfnWndProc = (WNDPROC)Frame::WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = Application::instance().instanceHandle();
	wcex.hIcon = NULL;
	wcex.hCursor = NULL;//LoadCursor(NULL, IDC_ARROW);
	wcex.hbrBackground = NULL;//(HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName = NULL;
	wcex.lpszClassName = className.c_str();
	wcex.hIconSm = NULL;

	RegisterClassExA(&wcex);


	auto hwnd = ::CreateWindowExA(
		0, className.c_str(), title_.c_str(), FRAME_WINDOW,
		bounds_.left(), bounds_.top(), bounds_.size().width, bounds_.size().height,
		Application::instance().dummyWindowHandle(), 
		NULL,
		Application::instance().instanceHandle(), 
		this
	);


	if (!hwnd) {
		result = false;
		return result;
	}

	//frameHandle_ was set in WndProc during WM_NCCREATE, so we can check it here
	//should be the same as hwnd returned from CreateWindowExA
	if (hwnd != this->frameHandle_) {
		result = false;
		return result;
	}

	::ShowWindow(frameHandle_, SW_SHOW);
	::RedrawWindow(frameHandle_, NULL, NULL, RDW_INVALIDATE | RDW_ERASENOW | RDW_ALLCHILDREN);

	return result;
}

std::tuple<Frame*, RootView*> Frame::getTarget(HWND hwnd) 
{
	Frame* targetFrame = nullptr;
	RootView* targetView = nullptr;

	if (hwnd == frameHandle_) {
		targetFrame = this;
	}
	else {
		if (nullptr != rootView_) {
			SubView* subViewTarget = nullptr;
			std::tie(targetView, subViewTarget) = rootView_->getTarget(hwnd);
		}
	}

	return std::make_tuple(targetFrame, targetView);
}

}

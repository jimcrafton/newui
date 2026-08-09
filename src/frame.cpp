#include "newui/frame.h"
#include "newui/application.h"

namespace newui {

	Frame::Frame()
	{
		rootView_ = new RootView(this, bounds_, "rootview");
	}

Frame::~Frame() = default;


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

			::PostMessage(frameHandle_, WM_QUIT, 0, 0);
			result = true;
		}
		break;

		case WM_DESTROY: {
			// Handle window destroy event
			// You can add custom logic here if needed
			result = true;
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

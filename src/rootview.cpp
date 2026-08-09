#include "newui/rootview.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/subview.h"
#include "newui/utils.h"
#include "newui/keyboard_constants.h"

namespace newui {

	RootView::RootView(Frame* frame, const newui::Rect& bounds, const std::string& name) : parentFrame_(frame) {
		bounds_ = bounds;
		name_ = name;
	}

	RootView::~RootView() {

	}

	void RootView::setBounds(const Rect& bounds) {
		if (bounds == bounds_) {
			return;
		}

		bounds_ = bounds;
		resizeImageBuffer((int)bounds_.size().width, (int)bounds_.size().height);
		onSizeChanged(*this, bounds_.size());
		::SetWindowPos(viewHwnd_, NULL,
			(int)bounds_.left(),
			(int)bounds_.top(),
			(int)bounds_.size().width,
			(int)bounds_.size().height,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	}

	void RootView::resizeImageBuffer(int width, int height) {
		// Release the old wrapper before imagePixels_ reallocates, since
		// imageBuffer_ points directly into it.
		imageBuffer_.reset();

		if (width <= 0 || height <= 0) {
			imagePixels_.clear();
			return;
		}

		const size_t stride = size_t(width) * 4;
		imagePixels_.assign(stride * size_t(height), 0);

		imageBuffer_.create_from_data(width, height, BL_FORMAT_XRGB32, imagePixels_.data(), intptr_t(stride));

		notifyRedrawNeeded();
	}

	void RootView::markDirty() {
		notifyRedrawNeeded();
	}

	void RootView::notifyRedrawNeeded() {
		onRedrawNeeded(*this);
		repaint();
		invalidate();
	}

	void RootView::repaint() {
		if (imagePixels_.empty()) {
			return;
		}

		BLContext ctx(imageBuffer_);
		paintStyle(ctx);
		paint(ctx);
		paintChildren(ctx);
		ctx.end();
	}

	void RootView::paintImageBufferToWindow(HDC hdc) {
		if (imagePixels_.empty()) {
			return;
		}

		BLImageData data;
		imageBuffer_.get_data(&data);

		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = data.size.w;
		bmi.bmiHeader.biHeight = -data.size.h; // negative = top-down, matching our stride's row order
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		::StretchDIBits(hdc, 0, 0, data.size.w, data.size.h,
			0, 0, data.size.w, data.size.h,
			data.pixel_data, &bmi, DIB_RGB_COLORS, SRCCOPY);
	}

	void RootView::invalidate() {
		if (nullptr != viewHwnd_) {
			::InvalidateRect(viewHwnd_, nullptr, FALSE);
		}
	}

	void RootView::setVisible(bool visible) 
	{
		if (visible == visible_) {
			return;
		}

		visible_ = visible;
		onVisibilityChanged(*this);
	}

	void RootView::addChild(SubView* child)
	{
		child->setParentView(this);
		View::addChild(child);		
	}

	void RootView::removeChild(SubView* child) {
		View::removeChild(child);
		child->setParentView(nullptr);
	}

	std::tuple<RootView*, SubView*> RootView::getTarget(HWND hwnd)
	{
		RootView* targetView = nullptr;
		SubView* targetSubView = nullptr;

		if (hwnd == viewHwnd_) {
			targetView = this;
		}
		else {
			for (SubView* child : childViews_) {
				// Assuming SubView has a method to get its HWND, which is not defined in the provided code.
				// You may need to implement this method in SubView class.
				// For example: HWND childHwnd = child->getHwnd();
				// if (childHwnd == hwnd) {
				//     targetSubView = child;
				//     break;
				// }
			}
		}

		return std::make_tuple(targetView, targetSubView);
	}

	void RootView::mouseEntered(const Point& pt)
	{
		onMouseEntered(*this, pt, 0, 0);
	}

	void RootView::mouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseDown(*this, pt, btnMask, keyMask);
	}

	void RootView::mouseMove(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseMove(*this, pt, btnMask, keyMask);
	}

	void RootView::mouseWheel(const Point& pt, float mouseDelta, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseWheel(*this, pt, mouseDelta);
	}
	
	void RootView::mouseLeft(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseLeft(*this, pt, btnMask, keyMask);
	}

	void RootView::mouseUp(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseUp(*this, pt, btnMask, keyMask);
	}

	void RootView::mouseDblClick(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseDblClick(*this, pt, btnMask, keyMask);
	}


	void RootView::gotFocus()
	{
		onGotFocus(*this);
	}

	void RootView::lostFocus()
	{
		onLostFocus(*this);
	}

	void RootView::keyEvent(int eventType, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode)
	{
		switch (eventType) {
			case keKeyPress: {
				onKeyPress(*this, keyMask, keyCharVal, repeatCount, VKeyCode);
			}
			break;
			
			case keKeyDown: {
				onKeyDown(*this, keyMask, keyCharVal, repeatCount, VKeyCode);
			}
			break;

			case keKeyUp: {
				onKeyUp(*this, keyMask, keyCharVal, repeatCount, VKeyCode);
			}
			break;
		}
	}

	bool RootView::handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT)
	{
		bool result = false;
		outLRESULT = 0;
		switch (message) {
			case WM_CREATE: {
				viewCreated();
				result = true;
			}
			break;

			case WM_DESTROY: {

				result = false;
			}
			break;

			case WM_PAINT: {
				PAINTSTRUCT ps;
				HDC hdc = ::BeginPaint(viewHwnd_, &ps);
				paintImageBufferToWindow(hdc);
				::EndPaint(viewHwnd_, &ps);
				result = true;
			}
			break;

			case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN: {
				Point pt(LOWORD(lParam), HIWORD(lParam));
				auto btnMask = translateButtonMask(wParam);
				auto keyMask = translateKeyMask(wParam);
				

				/*
				Scrollable* scrollable = msg->control_->getScrollable();
				if (NULL != scrollable) {
					pt.x_ += scrollable->getHorizontalPosition();
					pt.y_ += scrollable->getVerticalPosition();
				}
				*/

				mouseDown(pt, btnMask, keyMask);

				::SetFocus(viewHwnd_);
				result = true;
			}
			break;

			case WM_LBUTTONUP: case WM_MBUTTONUP: case WM_RBUTTONUP: {
				Point pt(LOWORD(lParam),HIWORD(lParam));

				

				/*
				Scrollable* scrollable = msg->control_->getScrollable();
				if (NULL != scrollable) {
					pt.x_ += scrollable->getHorizontalPosition();
					pt.y_ += scrollable->getVerticalPosition();
				}
				*/

				WPARAM tmpWParam = wParam;
				switch (message) {
					case WM_LBUTTONUP: {
						tmpWParam |= MK_LBUTTON;
					}
					break;
					case WM_MBUTTONUP: {
						tmpWParam |= MK_MBUTTON;
					}
					break;

					case WM_RBUTTONUP: {
						tmpWParam |= MK_RBUTTON;
					}
					break;
				}

				auto btnMask = translateButtonMask(tmpWParam);
				auto keyMask = translateKeyMask(tmpWParam);

				mouseUp(pt, btnMask, keyMask);
				result = true;
			}
			break;

			case WM_MOUSEMOVE: {
				Point pt(LOWORD(lParam), HIWORD(lParam));

				auto btnMask = translateButtonMask(wParam);
				auto keyMask = translateKeyMask(wParam);

				if (false == mouseEnteredControl_) {

					TRACKMOUSEEVENT trackmouseEvent = { 0,0,0,0 };
					trackmouseEvent.cbSize = sizeof(trackmouseEvent);
					trackmouseEvent.dwFlags = TME_LEAVE;
					trackmouseEvent.hwndTrack = viewHwnd_;
					trackmouseEvent.dwHoverTime = HOVER_DEFAULT;

					if (_TrackMouseEvent(&trackmouseEvent)) {
						//event->setType(Control::MOUSE_ENTERED);
						//peerControl_->handleEvent(event);

						//event->setType(Control::MOUSE_MOVE);
						mouseEntered(pt);
					}
				}

				mouseEnteredControl_ = true;

				mouseMove(pt, btnMask, keyMask);
				result = true;
			}
			break;

			case WM_MOUSEWHEEL:
			{
				Point pt(LOWORD(lParam), HIWORD(lParam));
				auto btnMask = translateButtonMask(wParam);
				auto keyMask = translateKeyMask(wParam);
				short mouseDelta = (short)HIWORD(wParam);   // wheel rotation
				mouseWheel(pt, mouseDelta, btnMask, keyMask);
				result = true;
			}
			break;

			case WM_MOUSELEAVE: {
				POINT pt = { 0,0 };
				::GetCursorPos(&pt);
				ScreenToClient(viewHwnd_, &pt);

				Point pt2(pt.x, pt.y);
				
				/*
				* Scrollable* scrollable = msg->control_->getScrollable();
				if (NULL != scrollable) {
					pt2.x_ += scrollable->getHorizontalPosition();
					pt2.y_ += scrollable->getVerticalPosition();
				}
				*/

				auto btnMask = translateButtonMask(0);
				auto keyMask = translateKeyMask(0);
				
				mouseLeft(pt2, btnMask, keyMask);
				result = true;
			}
			break;

			case WM_LBUTTONDBLCLK: case WM_MBUTTONDBLCLK: case WM_RBUTTONDBLCLK: {

				Point pt(LOWORD(lParam), HIWORD(lParam));
				/*
				Scrollable* scrollable = msg->control_->getScrollable();
				if (NULL != scrollable) {
					pt.x_ += scrollable->getHorizontalPosition();
					pt.y_ += scrollable->getVerticalPosition();
				}
				*/
				auto btnMask = translateButtonMask(wParam);
				auto keyMask = translateKeyMask(wParam);
				
				mouseDblClick(pt, btnMask, keyMask);
				result = true;
			}
			break;

			case WM_CHAR: case WM_KEYDOWN: case WM_KEYUP: {

				KeyboardEventInfo keyData = {};
				translateKeyEventInfo(viewHwnd_, message, wParam, lParam, keyData);

				int  keyCharVal = 0;
				int eventType = keUndefined;
				auto keyMask = translateKeyMask(keyData.keyMask);

				switch (message) {
					case WM_CHAR: {
						//eventType = Control::KEYBOARD_PRESSED;
						eventType = keKeyPress;
						keyCharVal = (int)wParam;
						if (isgraph(keyCharVal)) {
							keyData.VKeyCode = translateCharToVKCode(keyCharVal);
						}
					}
					break;

					case WM_KEYDOWN: {
						keyCharVal = keyData.character;
						eventType = keKeyDown;
						
						keyData.VKeyCode = translateVirtualKey(wParam,0);
					}
					break;

					case WM_KEYUP: {
						eventType = keKeyUp;
						
						keyCharVal = keyData.character;
						keyData.VKeyCode = translateVirtualKey(wParam, 0);
					}
					break;
				}


				keyEvent(eventType, keyMask, keyCharVal, keyData.repeatCount, keyData.VKeyCode);
				result = true;
			}
			break;


			case WM_SETFOCUS: {
				gotFocus();
				result = true;
			}
			break;

			case WM_KILLFOCUS: {
				lostFocus();
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

	LRESULT CALLBACK RootView::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		RootView* thisPtr = nullptr;
		if (message == WM_NCCREATE) {
			// Extract the 'this' pointer from CREATESTRUCT passed via CreateWindowEx
			auto* cs = reinterpret_cast<CREATESTRUCT*>(lParam);
			thisPtr = reinterpret_cast<RootView*>(cs->lpCreateParams);
			// Associate the pointer with the HWND for future messages
			::SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(thisPtr));

			// Save the handle inside the object
			thisPtr->viewHwnd_ = hWnd;
		}
		else {
			thisPtr = reinterpret_cast<RootView*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
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

#define SIMPLE_VIEW	 WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_OVERLAPPED

	bool RootView::initialize()
	{
		bool result = true;

		if (nullptr == parentFrame_) {
			return false;
		}

		if (name_.empty()) {
			return false;
		}

		WNDCLASSEXA wcex;
		std::string className = "View" + name_;
		wcex.cbSize = sizeof(wcex);

		wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
		wcex.lpfnWndProc = (WNDPROC)RootView::WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = Application::instance().instanceHandle();
		wcex.hIcon = NULL;
		wcex.hCursor = NULL;//LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_HIGHLIGHT+1);
		wcex.lpszMenuName = NULL;
		wcex.lpszClassName = className.c_str();
		wcex.hIconSm = NULL;

		RegisterClassExA(&wcex);

		auto hwnd = ::CreateWindowExA( 0, className.c_str(), "", 
						SIMPLE_VIEW,
						bounds_.left(), 
						bounds_.top(), 
						bounds_.size().width, 
						bounds_.size().height,
						parentFrame_->frameHandle(),
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
		if (hwnd != this->viewHwnd_) {
			result = false;
			return result;
		}

		resizeImageBuffer((int)bounds_.size().width, (int)bounds_.size().height);

		::ShowWindow(viewHwnd_, SW_SHOW);
		::SetFocus(viewHwnd_);

		return true;
	}



	void RootView::viewCreated()
	{
		onCreated(*this);
	}

	void RootView::destroy() {
		View::destroy();

		if (nullptr != viewHwnd_) {
			DestroyWindow(viewHwnd_);
			viewHwnd_ = nullptr;
		}
	}

}

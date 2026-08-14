#include "newui/rootview.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/subview.h"
#include "newui/utils.h"
#include "newui/keyboard_constants.h"
#include "newui/viewstyle.h"

namespace {

	// Recurses view and every descendant SubView, dropping the cached
	// HTHEME on whichever ones are actually ThemedViewStyle - a plain
	// ViewStyle has nothing theme-related to drop, so dynamic_cast simply
	// skips it. Used by RootView::refreshThemes() below.
	void closeThemedStyles(newui::View& view) {
		if (auto* themed = dynamic_cast<newui::ThemedViewStyle*>(&view.style())) {
			themed->closeTheme();
		}
		for (newui::SubView* child : view.childViews()) {
			closeThemedStyles(*child);
		}
	}

	// True if candidate is subtreeRoot itself, or a descendant of it -
	// walks candidate's own parent() chain upward (each SubView's parent_
	// is set correctly at every depth by SubView::addChild()/
	// RootView::addChild(), regardless of nesting - see subview.h) until
	// it either reaches subtreeRoot (true) or the chain runs out at
	// something that isn't a SubView, i.e. the RootView itself (false).
	// Used by RootView::notifySubViewRemoved() to decide whether a
	// removed subtree carries away this RootView's hovered/captured/
	// focused pointer with it.
	bool isWithinSubtree(const newui::SubView* candidate, const newui::SubView* subtreeRoot) {
		for (const newui::View* cur = candidate; cur != nullptr; ) {
			if (cur == subtreeRoot) {
				return true;
			}
			const newui::SubView* sv = dynamic_cast<const newui::SubView*>(cur);
			if (sv == nullptr) {
				return false;
			}
			cur = sv->parent();
		}
		return false;
	}

}

namespace newui {

	RootView::RootView(Frame* frame, const newui::Rect& bounds, const std::string& name) : parentFrame_(frame) {
		bounds_ = bounds;
		name_ = name;

		// A RootView is its own root - rootView() (and therefore
		// ViewStyle::markDirty()'s view_->rootView() chain) needs this set
		// on itself, not just propagated down to children (see
		// addChild()).
		setRootView(this);
	}

	RootView::~RootView() {

	}

	void RootView::setBounds(const Rect& bounds) {
		if (bounds == bounds_) {
			return;
		}

		bounds_ = bounds;
		// updateLayout() before resizeImageBuffer(): the latter is what
		// triggers the actual repaint (via notifyRedrawNeeded()), so
		// children need their new bounds in place first - otherwise
		// that repaint would still walk childViews_ at their pre-resize
		// positions/sizes.
		updateLayout();
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
		imagePixels_.shrink_to_fit();

		imageBuffer_.create_from_data(width, height, BL_FORMAT_XRGB32, imagePixels_.data(), intptr_t(stride));

		notifyRedrawNeeded();
	}

	void RootView::markDirty() {
		notifyRedrawNeeded();
	}

	void RootView::refreshThemes() {
		closeThemedStyles(*this);
		markDirty();
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
		// propagateRootView(), not setRootView(): child may already have
		// its own subtree (built before being attached here), and every
		// descendant in it needs to pick up this RootView too, not just
		// child itself.
		child->propagateRootView(rootView());
		child->setParent(this);
		View::addChild(child);
		
	}

	void RootView::removeChild(SubView* child) {
		notifySubViewRemoved(child);
		View::removeChild(child);
		child->setParentView(nullptr);
		child->setParent(nullptr);
		child->propagateRootView(nullptr);
	}

	Point RootView::accumulatedOffset(const SubView* view) const {
		Point offset(0.0f, 0.0f);
		for (const View* cur = view; cur != nullptr; ) {
			const SubView* sv = dynamic_cast<const SubView*>(cur);
			if (sv == nullptr) {
				break;
			}
			offset = offset + sv->getBounds().pos();
			cur = sv->parent();
		}
		return offset;
	}

	Point RootView::localToScreen(const Point& rootLocalPt) const {
		if (viewHwnd_ == nullptr) {
			return rootLocalPt;
		}
		POINT pt = rootLocalPt;
		::ClientToScreen(viewHwnd_, &pt);
		return pt;
	}

	void RootView::updateHoveredSubView(SubView* target, const Point& rootPt) {
		if (target == hoveredSubView_) {
			return;
		}

		if (hoveredSubView_ != nullptr) {
			SubView* left = hoveredSubView_;
			left->onMouseLeft(*left, rootPt - accumulatedOffset(left), 0, 0);
			left->setHighlighted(false);
			left->style().markDirty();
		}

		hoveredSubView_ = target;

		if (hoveredSubView_ != nullptr) {
			hoveredSubView_->onMouseEntered(*hoveredSubView_, rootPt - accumulatedOffset(hoveredSubView_), 0, 0);
			hoveredSubView_->setHighlighted(true);
			hoveredSubView_->style().markDirty();
		}
	}

	void RootView::setFocusedSubView(SubView* target) {
		if (target == focusedSubView_) {
			return;
		}

		if (focusedSubView_ != nullptr) {
			focusedSubView_->onLostFocus(*focusedSubView_);
		}

		focusedSubView_ = target;

		if (focusedSubView_ != nullptr) {
			focusedSubView_->onGotFocus(*focusedSubView_);
		}
	}

	void RootView::notifySubViewRemoved(SubView* removedSubtreeRoot) {
		if (removedSubtreeRoot == nullptr) {
			return;
		}

		if (hoveredSubView_ != nullptr && isWithinSubtree(hoveredSubView_, removedSubtreeRoot)) {
			hoveredSubView_ = nullptr;
		}
		if (capturedSubView_ != nullptr && isWithinSubtree(capturedSubView_, removedSubtreeRoot)) {
			capturedSubView_ = nullptr;
		}
		if (focusedSubView_ != nullptr && isWithinSubtree(focusedSubView_, removedSubtreeRoot)) {
			focusedSubView_ = nullptr;
		}
	}

	View* RootView::cursorTargetAt(const Point& pt) {
		if (capturedSubView_ != nullptr) {
			return capturedSubView_;
		}

		Point localPt;
		SubView* hit = hitTestChildren(pt, localPt);
		return hit != nullptr ? static_cast<View*>(hit) : static_cast<View*>(this);
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

	// Every mouseXxx() below fires this RootView's own delegate first
	// (pt in RootView-local/window-client coordinates, unchanged
	// pre-existing behavior) and then, where applicable, routes a second,
	// translated copy of the event to whichever SubView is the right
	// target - hit-tested under the cursor, or capturedSubView_/
	// focusedSubView_ where capture/focus semantics apply (see
	// hoveredSubView()/capturedSubView()/focusedSubView() in rootview.h).
	void RootView::mouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseDown(*this, pt, btnMask, keyMask);

		Point localPt;
		SubView* target = hitTestChildren(pt, localPt);
		capturedSubView_ = target;
		setFocusedSubView(target);

		if (target != nullptr) {
			target->onMouseDown(*target, localPt, btnMask, keyMask);
		}
	}

	void RootView::mouseMove(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseMove(*this, pt, btnMask, keyMask);

		Point hoverLocalPt;
		SubView* hoverTarget = hitTestChildren(pt, hoverLocalPt);
		updateHoveredSubView(hoverTarget, pt);

		SubView* dispatchTarget = capturedSubView_ != nullptr ? capturedSubView_ : hoverTarget;
		if (dispatchTarget != nullptr) {
			Point localPt = (dispatchTarget == hoverTarget) ? hoverLocalPt : (pt - accumulatedOffset(dispatchTarget));
			dispatchTarget->onMouseMove(*dispatchTarget, localPt, btnMask, keyMask);
		}
	}

	void RootView::mouseWheel(const Point& pt, float mouseDelta, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseWheel(*this, pt, mouseDelta);

		Point localPt;
		SubView* target = hitTestChildren(pt, localPt);
		if (target != nullptr) {
			target->onMouseWheel(*target, localPt, mouseDelta);
		}
	}

	void RootView::mouseLeft(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseLeft(*this, pt, btnMask, keyMask);

		// The cursor left the whole window, not just whatever SubView it
		// was last over - nothing is hovered now, regardless of
		// capturedSubView_ (capture is unaffected: a drag that started on
		// a SubView keeps routing mouseMove()/mouseUp() to it even while
		// the cursor is outside the window entirely - see handleMessage()'s
		// SetCapture()).
		updateHoveredSubView(nullptr, pt);
	}

	void RootView::mouseUp(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseUp(*this, pt, btnMask, keyMask);

		Point localPt;
		SubView* target = capturedSubView_;
		if (target != nullptr) {
			localPt = pt - accumulatedOffset(target);
		} else {
			target = hitTestChildren(pt, localPt);
		}

		capturedSubView_ = nullptr;

		if (target != nullptr) {
			target->onMouseUp(*target, localPt, btnMask, keyMask);
		}
	}

	void RootView::mouseDblClick(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseDblClick(*this, pt, btnMask, keyMask);

		// Windows doesn't send a fresh WM_LBUTTONDOWN for the second
		// click of a double-click (WM_LBUTTONDBLCLK stands in for it), so
		// this re-establishes capture/focus exactly like mouseDown() does -
		// otherwise a click-drag starting on a double-click would have no
		// capturedSubView_ to route through.
		Point localPt;
		SubView* target = hitTestChildren(pt, localPt);
		capturedSubView_ = target;
		setFocusedSubView(target);

		if (target != nullptr) {
			target->onMouseDblClick(*target, localPt, btnMask, keyMask);
		}
	}


	void RootView::gotFocus()
	{
		onGotFocus(*this);

		if (focusedSubView_ != nullptr) {
			focusedSubView_->onGotFocus(*focusedSubView_);
		}
	}

	void RootView::lostFocus()
	{
		onLostFocus(*this);

		if (focusedSubView_ != nullptr) {
			focusedSubView_->onLostFocus(*focusedSubView_);
		}
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

		if (focusedSubView_ == nullptr) {
			return;
		}

		switch (eventType) {
			case keKeyPress: {
				focusedSubView_->onKeyPress(*focusedSubView_, keyMask, keyCharVal, repeatCount, VKeyCode);
			}
			break;

			case keKeyDown: {
				focusedSubView_->onKeyDown(*focusedSubView_, keyMask, keyCharVal, repeatCount, VKeyCode);
			}
			break;

			case keKeyUp: {
				focusedSubView_->onKeyUp(*focusedSubView_, keyMask, keyCharVal, repeatCount, VKeyCode);
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
				// Keeps delivering WM_MOUSEMOVE/WM_*BUTTONUP to this window
				// even once the cursor leaves it - needed so
				// capturedSubView_ (set by mouseDown() above) keeps
				// receiving mouseMove()/mouseUp() for the rest of a drag
				// that goes outside the window's bounds. Released on the
				// matching button-up below (or via WM_CAPTURECHANGED if
				// something else steals it first).
				::SetCapture(viewHwnd_);
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
				// Matches the SetCapture() in WM_LBUTTONDOWN/WM_MBUTTONDOWN/
				// WM_RBUTTONDOWN - releases capture once mouseUp() above
				// has already cleared capturedSubView_. Safe to call even
				// if this window doesn't currently hold capture (e.g. a
				// button-up with no matching prior button-down).
				::ReleaseCapture();
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

			case WM_SETCURSOR: {
				// LOWORD(lParam) is the hit-test code from the preceding
				// WM_NCHITTEST - only override the cursor for the client
				// area (HTCLIENT); anything else (resize borders, etc.)
				// should keep getting Windows' own default handling.
				if (LOWORD(lParam) != HTCLIENT) {
					result = false;
					break;
				}

				POINT pt;
				::GetCursorPos(&pt);
				::ScreenToClient(viewHwnd_, &pt);

				View* target = cursorTargetAt(Point(static_cast<float>(pt.x), static_cast<float>(pt.y)));
				::SetCursor(target->resolvedCursor());
				outLRESULT = TRUE;
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
				// See the WM_LBUTTONDOWN/etc. comment - mouseDblClick()
				// re-establishes capturedSubView_ the same way mouseDown()
				// does, since Windows sends WM_LBUTTONDBLCLK instead of a
				// second WM_LBUTTONDOWN, so the Win32-level capture needs
				// re-establishing here too.
				::SetCapture(viewHwnd_);
				result = true;
			}
			break;

			case WM_CAPTURECHANGED: {
				// Something else (a system drag operation, another
				// window, ...) took over mouse capture out from under us -
				// capturedSubView_ would no longer receive real
				// WM_MOUSEMOVE/WM_*BUTTONUP messages to route, so drop it
				// rather than have it linger stale until some unrelated
				// future click happens to overwrite it.
				capturedSubView_ = nullptr;
				result = true;
			}
			break;

			case WM_CHAR: case WM_KEYDOWN: case WM_KEYUP: {

				KeyboardEventInfo keyData = {};
				translateKeyEventInfo(viewHwnd_, message, wParam, lParam, keyData);

				int  keyCharVal = 0;
				int eventType = keUndefined;

				// keyData.keyMask is already newui's own kmShift/kmCtrl/
				// kmAlt bits (translateKeyEventInfo() builds it straight
				// from GetAsyncKeyState(), not a raw Win32 MK_* mask) - do
				// NOT re-run it through translateKeyMask(), which expects
				// the mouse-message MK_CONTROL/MK_SHIFT encoding instead.
				// Doing so used to corrupt it: kmCtrl (0x4) collides with
				// MK_SHIFT (0x4), so a real Ctrl press got reported as
				// Shift while Ctrl itself never registered (only Alt
				// happened to still work, since translateKeyMask()
				// re-queries VK_MENU directly rather than trusting its own
				// argument for that bit).
				auto keyMask = static_cast<std::uint32_t>(keyData.keyMask);

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

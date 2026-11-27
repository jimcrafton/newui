// View's screen <-> local point conversions, which need the hosting window.
#include "newui/view.h"
#include "newui/rootview.h"
#include "newui/win32/nativehandles.h"
#include "newui/win32/geometryconv.h"

namespace newui {

	Point View::localToScreen(const Point& localPt) const
	{
		Point rootPt = localToRoot(localPt);
		const RootView* root = rootView();
		HWND hwnd = (root != nullptr) ? newui::win32::toHWND(root->windowHandle()) : nullptr;
		if (hwnd == nullptr) {
			return rootPt;
		}
		POINT pt = win32::toPOINT(rootPt);
		::ClientToScreen(hwnd, &pt);
		return win32::fromPOINT(pt);
	}

	Point View::screenToLocal(const Point& screenPt) const
	{
		const RootView* root = rootView();
		HWND hwnd = (root != nullptr) ? newui::win32::toHWND(root->windowHandle()) : nullptr;
		Point rootPt = screenPt;
		if (hwnd != nullptr) {
			POINT pt = win32::toPOINT(screenPt);
			::ScreenToClient(hwnd, &pt);
			rootPt = win32::fromPOINT(pt);
		}
		return rootToLocal(rootPt);
	}

}

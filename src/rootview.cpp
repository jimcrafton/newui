#include "newui/rootview.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/subview.h"
#include "newui/utils.h"
#include "newui/keyboard_constants.h"
#include "newui/viewstyle.h"

#include <cmath>

namespace {

	// Snaps a rect outward to whole pixel boundaries (floor the leading
	// edges, ceil the trailing ones, so it only ever grows, never shrinks
	// and clips off a partial pixel). dirtyRect_ accumulates through
	// float position math (accumulatedOffset()'s repeated +=, min()/max()
	// unions of several views' bounds, ...) and generally ends up with
	// fractional coordinates - fine for InvalidateRect/StretchDIBits
	// (Win32 truncates to int anyway) but not for
	// ctx.clip_to_rect(dirtyRect_): a fractional-edged clip stops
	// Blend2D's JIT from taking its fast axis-aligned "box fill" pipeline
	// (FillType::kBoxA - see pipedefs_p.h), and something about a themed
	// control's pattern/image fill running under the resulting non-box
	// path trips 'is_rect_fill()' assertions in fetchpatternpart.cpp
	// (reproduced live via mouse hover before this fix).
	newui::Rect snappedToPixels(const newui::Rect& r) {
		float left = std::floor(r.left());
		float top = std::floor(r.top());
		float right = std::ceil(r.right());
		float bottom = std::ceil(r.bottom());
		return newui::Rect(left, top, right - left, bottom - top);
	}

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
		// See aliveFlag_'s own doc comment (rootview.h) - a repaint task
		// posted via scheduleRepaint() can still be sitting in RunLoop's
		// idle queue after this point; this is what tells it to no-op
		// instead of touching a destroyed RootView.
		*aliveFlag_ = false;
		releaseImageBuffer();
	}

	void RootView::setBounds(const Rect& bounds) {
		if (bounds == bounds_) {
			return;
		}

		bounds_ = bounds;

		// Before onSizeChanged()/updateLayout()/resizeImageBuffer() below -
		// none of those trigger the actual repaint synchronously except
		// resizeImageBuffer() (via notifyRedrawNeeded()), but viewSized()
		// is for updating overlay_'s own extra state, not for painting, so
		// it just needs to run before that eventual repaint, same as
		// updateLayout() needing to run before it for childViews_.
		if (overlay_) {
			overlay_->viewSized(bounds_);
		}

		// updateLayout() before resizeImageBuffer(): the latter is what
		// triggers the actual repaint (via notifyRedrawNeeded()), so
		// children need their new bounds in place first - otherwise
		// that repaint would still walk childViews_ at their pre-resize
		// positions/sizes.
		onSizeChanged(*this, bounds_.size());

		updateLayout();
		resizeImageBuffer((int)bounds_.size().width, (int)bounds_.size().height);
		
		::SetWindowPos(viewHwnd_, NULL,
			(int)bounds_.left(),
			(int)bounds_.top(),
			(int)bounds_.size().width,
			(int)bounds_.size().height,
			SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
	}

	void RootView::releaseImageBuffer() {
		// Release the Blend2D wrapper before the DIB section it points
		// into goes away.
		imageBuffer_.reset();

		if (dibSection_ != nullptr) {
			// Re-select whatever the memory DC originally had (a 1x1 mono
			// stock bitmap) before deleting our own bitmap - deleting a
			// bitmap while it's still selected into a DC is undefined
			// behavior (GDI leaves the DC referencing a half-destroyed
			// object), same reasoning as any other GDI select/delete pair.
			::SelectObject(memDC_, dibSectionOldBitmap_);
			::DeleteObject(dibSection_);
			dibSection_ = nullptr;
			dibSectionOldBitmap_ = nullptr;
		}
		if (memDC_ != nullptr) {
			::DeleteDC(memDC_);
			memDC_ = nullptr;
		}
	}

	void RootView::resizeImageBuffer(int width, int height) {
		releaseImageBuffer();

		if (width <= 0 || height <= 0) {
			return;
		}

		// CreateDIBSection(), not a plain heap buffer wrapped by
		// BLImage::create_from_data() (the original approach) - gives
		// back memory GDI itself already recognizes as a real bitmap
		// object, so paintImageBufferToWindow() can BitBlt() from it
		// directly instead of re-describing a raw pointer via
		// StretchDIBits() on every single WM_PAINT. BitBlt() between two
		// already-realized GDI objects is the faster, more idiomatic
		// Win32 path for a CPU-rendered-then-blitted buffer like this one
		// (StretchDIBits() re-validates the BITMAPINFO header and
		// negotiates pixel format on every call, even for a 1:1 unscaled
		// blit). Blend2D still writes into this memory exactly as before
		// - CreateDIBSection()'s ppvBits is plain, directly-writable
		// pixel memory, just GDI-backed instead of a std::vector.
		BITMAPINFO bmi = {};
		bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
		bmi.bmiHeader.biWidth = width;
		bmi.bmiHeader.biHeight = -height; // negative = top-down, matching Blend2D's row order (see paintImageBufferToWindow())
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		void* bits = nullptr;
		memDC_ = ::CreateCompatibleDC(nullptr);
		if (memDC_ == nullptr) {
			return;
		}

		dibSection_ = ::CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
		if (dibSection_ == nullptr || bits == nullptr) {
			::DeleteDC(memDC_);
			memDC_ = nullptr;
			dibSection_ = nullptr;
			return;
		}
		dibSectionOldBitmap_ = static_cast<HBITMAP>(::SelectObject(memDC_, dibSection_));

		// 32bpp is always DWORD-aligned regardless of width, so the DIB
		// section's own row stride is exactly width * 4 - no padding to
		// account for, same guarantee the original std::vector-backed
		// buffer's comment already relied on.
		const size_t stride = size_t(width) * 4;
		imageBuffer_.create_from_data(width, height, BL_FORMAT_XRGB32, bits, intptr_t(stride));
		dirtyRect_ = newui::Rect( 0,0, width, height);
		notifyRedrawNeeded();
	}

	void RootView::markDirty() {
		dirtyRect_ = snappedToPixels(this->getClientBounds());
		scheduleRepaint();
	}


	void RootView::markDirty(const View* fromView, const newui::Rect& rect)
	{

		auto adjustedR = snappedToPixels(fromViewToLocal(fromView, rect));

		if (dirtyRect_.empty()) {
			dirtyRect_ = adjustedR;
		}
		else {
			// Union of the two rects' bounding box - min of the near
			// corners, max of the *far* corners (not max of the sizes
			// independently, which only happens to give the right answer
			// when both rects already share a top-left corner; two rects
			// that don't - e.g. two different controls going dirty before
			// the next flush - previously produced a box too small to
			// actually cover both). Both inputs are already pixel-snapped
			// (adjustedR just above, dirtyRect_ by this same function or
			// by markDirty()/resizeImageBuffer()), so the result is too -
			// min/max of already-integer values stays integer. This union
			// is also the whole reason scheduleRepaint() below is useful,
			// not just a performance nicety: without it, this branch was
			// dead code (dirtyRect_ always got cleared by invalidate()
			// before the next markDirty() call could ever see it non-
			// empty - see HANDOFF.md) - now that repaint is genuinely
			// deferred, several markDirty() calls really do accumulate
			// here before the next actual repaint() consumes them.
			float left = std::min(adjustedR.left(), dirtyRect_.left());
			float top = std::min(adjustedR.top(), dirtyRect_.top());
			float right = std::max(adjustedR.right(), dirtyRect_.right());
			float bottom = std::max(adjustedR.bottom(), dirtyRect_.bottom());

			dirtyRect_.setPos(Point(left, top));
			dirtyRect_.setSize(Size(right - left, bottom - top));
		}
		scheduleRepaint();
	}

	void RootView::refreshThemes() {
		closeThemedStyles(*this);
		markDirty();
	}

	void RootView::scheduleRepaint() {
		if (repaintScheduled_) {
			return;
		}
		repaintScheduled_ = true;

		// Captured by value - keeps the flag (and therefore the safety
		// check below) alive independently of *this*, which this queued
		// task may outlive - see aliveFlag_'s own doc comment (rootview.h).
		std::shared_ptr<bool> alive = aliveFlag_;
		Application::instance().runLoop().postIdle([this, alive]() {
			if (*alive) {
				repaintScheduled_ = false;
				notifyRedrawNeeded();
			}
			return true; // one-shot - done after running once
		});
	}

	void RootView::notifyRedrawNeeded() {
		onRedrawNeeded(*this);
		repaint();

		invalidate(&dirtyRect_);
	}

	void RootView::repaint() {
		if (memDC_ == nullptr) {
			return;
		}

		BLContext ctx(imageBuffer_);

		// This RootView's own paintStyle()/paint() are clipped to
		// dirtyRect_ in a narrow save()/restore() - safe because a
		// RootView's own background fill is always a plain solid-color
		// ctx.fill_rect() (ViewStyle::paint()'s base implementation, see
		// viewstyle.h) - solid fills don't hit the Blend2D JIT bug
		// pattern/image fills do. This scoping is what keeps a small
		// hover-driven repaint from wiping (and needing to redraw) the
		// *entire* window's background on every call.
		if (!dirtyRect_.empty()) {
			ctx.save();
			ctx.clip_to_rect(dirtyRect_);
			paintStyle(ctx);
			paint(ctx);
			ctx.restore();
		} else {
			paintStyle(ctx);
			paint(ctx);
		}

		// paintChildren() walks every child unconditionally (no dirty-rect
		// pruning) - see its own comment (view.h) for why: pruning was
		// tried and produced real visual corruption, confirmed via a
		// controlled test to be caused by the pruning itself rather than
		// this level's own clip above (removing pruning while keeping
		// this clip fixed it immediately). Each child still only ever
		// clips to its own full bounds via the unchanged
		// ctx.clip_to_rect() inside paintChildren() - never intersected
		// with dirtyRect_ - which is also what keeps themed/pattern-filled
		// children (uxtheme's DrawThemeBackground - ThemedViewStyle,
		// viewstyle.h) away from a real Blend2D JIT bug
		// ('is_rect_fill()' assertion) that a *combined* outer+child clip
		// hits.
		//
		// Consequence worth knowing about: every child gets redrawn on
		// every repaint anywhere in the tree, whether or not its own area
		// was part of what actually changed - harmless (self-correcting)
		// for a child with its own opaque backgroundFill, but NOT
		// idempotent for one that paints translucent content (anti-
		// aliased text, a partially-transparent themed part) with nothing
		// opaque under it - repeated re-blending of the same edge pixels
		// onto themselves subtly darkens/thickens them further each time
		// instead of reproducing the same result. See LabelStyle's own
		// doc comment (viewstyle.h) for the concrete fix (give it an
		// opaque backgroundFill).
		paintChildren(ctx);

		// Last, on top of every child - see Overlay's own class comment
		// (overlay.h). Unclipped, like paintChildren() above (not confined
		// to dirtyRect_) for the same reason: this whole function only
		// narrows to dirtyRect_ for this RootView's own paintStyle()/
		// paint(), never for anything drawn afterward.
		if (overlay_ && overlay_->visible()) {
			overlay_->paint(ctx, Rect(0.0f, 0.0f, bounds_.size().width, bounds_.size().height));
		}

		ctx.end();
	}

	void RootView::setOverlay(std::unique_ptr<Overlay> overlay) {
		overlay_ = std::move(overlay);
		if (overlay_) {
			overlay_->viewSized(bounds_);
		}
	}

	void RootView::paintImageBufferToWindow(HDC hdc, const newui::Rect& paintRect) {
		if (memDC_ == nullptr) {
			return;
		}

		BLImageData data;
		imageBuffer_.get_data(&data);

		auto pt = paintRect.pos();
		auto sz = paintRect.size();

		if ((pt.x >= data.size.w) || (pt.y >= data.size.h)) {
			printf("rect pos outside of bounds, %d, %d\n", (int)pt.x, (int)pt.y);
			return;
		}

		if (((pt.x + sz.width) > data.size.w) || ((pt.y + sz.height) > data.size.h)) {
			printf("rect outside of bounds, %d, %d\n", (int)pt.x, (int)pt.y);
			return;
		}

		// BitBlt from memDC_ (the DIB section Blend2D renders directly
		// into - see resizeImageBuffer()), not StretchDIBits from a raw
		// pointer - dest == src (both paintRect) is still an unscaled 1:1
		// blit of just that sub-region, same as before, just via the
		// faster GDI-to-GDI path (StretchDIBits re-validates a fresh
		// BITMAPINFO header and negotiates pixel format on every call,
		// even for a 1:1 blit between two already-realized bitmap
		// objects BitBlt doesn't need to).
		::BitBlt(hdc, (int)pt.x, (int)pt.y, (int)sz.width, (int)sz.height,
			memDC_, (int)pt.x, (int)pt.y, SRCCOPY);
	}

	newui::Rect RootView::fromViewToLocal(const View* fromView, const newui::Rect& rect)
	{
		// accumulatedOffset() already walks exactly this chain correctly -
		// it stops at the RootView itself (dynamic_cast<SubView*> fails on
		// it, since RootView isn't a SubView) instead of also folding in
		// the RootView's own bounds().pos() - its position within *its*
		// parent Frame/screen, which has nothing to do with this
		// RootView's own local/window-client space (root-local
		// coordinates treat the RootView's own top-left as the origin).
		// This used to reimplement the same walk by hand via a raw
		// parent() loop with no such stop condition - one level too far,
		// shifting every scoped dirty rect by the window's own on-screen
		// position whenever that wasn't exactly (0,0). dynamic_cast
		// returning nullptr when fromView is itself the RootView is also
		// the right answer there (nothing to add - see
		// accumulatedOffset()'s own null-safe loop).
		newui::Rect result = rect;
		result.setPos(result.pos() + accumulatedOffset(dynamic_cast<const SubView*>(fromView)));
		return result;
	}

	void RootView::invalidate(View* fromView, const newui::Rect* invalidArea)
	{
		// Was missing this return - falling through to dereference
		// invalidArea right below even when it's null (exactly what
		// Visual Studio's static analyzer was flagging: guaranteed
		// null-pointer dereference on this path).
		if (nullptr == invalidArea) {
			invalidate(nullptr);
			return;
		}

		// Was a second hand-rolled copy of fromViewToLocal()'s old
		// one-level-too-far walk (see its own comment) - delegate to the
		// fixed version instead of duplicating the same bug twice.
		newui::Rect localR = fromViewToLocal(fromView, *invalidArea);
		invalidate(&localR);
	}

	void RootView::invalidate(const newui::Rect* invalidArea)
	{
		if (nullptr != viewHwnd_) {
			RECT* paintRect = nullptr;
			RECT r = {};
			if (nullptr != invalidArea) {
				r = *invalidArea;
				paintRect = &r;
			}

			::InvalidateRect(viewHwnd_, paintRect, FALSE);
		}

		dirtyRect_.clear();
	}

	void RootView::invalidate() {
		invalidate(nullptr);		
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
			offset += sv->bounds().pos();
			// sv's immediate parent may itself have scrolled its children
			// (View::origin() - a ScrollView's viewport, say) - undo that
			// same shift here so this stays the exact inverse of
			// paintChildren()'s -origin() translate at every level
			// crossed, not just sv's own bounds().pos(). See origin()'s
			// own doc comment (view.h).
			View* parent = sv->parent();
			if (parent != nullptr) {
				offset -= parent->origin();
			}
			cur = parent;
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

	void RootView::mouseWheel(const Point& pt, float mouseDelta, std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
	{
		onMouseWheel(*this, pt, mouseDelta);

		Point localPt;
		SubView* target = hitTestChildren(pt, localPt);

		// Bubbles from the deepest hit-tested view up through its
		// ancestors (mirroring accumulatedOffset()'s own per-level walk -
		// see its own comment on why parent->origin() has to come out at
		// each step) until one actually handles it (syncCallFirst -
		// delegate.h - stops at the first Handled result) or there are no
		// more SubView ancestors. Unlike onMouseDown/onMouseMove/onMouseUp
		// (which only ever fire once, on whatever's directly under the
		// cursor), wheel is the one event every real GUI routes to "the
		// nearest ancestor that wants it" - this is what lets a
		// ScrollView (controls.h) catch a wheel event over any of its
		// nested content without that content needing to know scrolling
		// exists above it.
		for (View* cur = target; cur != nullptr; ) {
			SubView* sv = dynamic_cast<SubView*>(cur);
			if (sv == nullptr) {
				break;
			}
			if (sv->onMouseWheel.syncCallFirst(*sv, localPt, mouseDelta).handled()) {
				return;
			}
			View* parent = sv->parent();
			if (parent != nullptr) {
				localPt = localPt + sv->bounds().pos() - parent->origin();
			}
			cur = parent;
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
				
				//this checks if we truly have work to do,
				//if non zero return then returns with no painting
				if (!GetUpdateRect(viewHwnd_, NULL, FALSE)) {
					outLRESULT = 0;
					result = true;
					printf("GetUpdateRect failed\n");
					return result;
				}

				PAINTSTRUCT ps;
				HDC hdc = ::BeginPaint(viewHwnd_, &ps);
				
				newui::Rect paintRect = ps.rcPaint;

				paintImageBufferToWindow(hdc, paintRect);
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

				// mouseDown() above may have synchronously torn this
				// window down (a target's own onMouseDown handler closing
				// a popup RootView it was hosted in, e.g. DropDownList
				// dismissing its PopupFrame's popup on a row click,
				// controls.cpp) - if so, this window is already hidden by
				// the time control returns here, and stealing OS focus/
				// capture onto it now would silently pull real keyboard
				// input into a window nothing can see anymore, undoing
				// whatever the nested handler had already refocused
				// instead (confirmed live: exactly this, reported as "the
				// dropdown control loses focus" after selecting a popup
				// row - DropDownList::closePopup()'s own refocus onto the
				// main window ran *before* this tail code, which then
				// unconditionally overwrote it right back onto the
				// popup's own now-hidden HWND).
				if (::IsWindowVisible(viewHwnd_)) {
					::SetFocus(viewHwnd_);
					// Keeps delivering WM_MOUSEMOVE/WM_*BUTTONUP to this
					// window even once the cursor leaves it - needed so
					// capturedSubView_ (set by mouseDown() above) keeps
					// receiving mouseMove()/mouseUp() for the rest of a
					// drag that goes outside the window's bounds. Released
					// on the matching button-up below (or via
					// WM_CAPTURECHANGED if something else steals it first).
					::SetCapture(viewHwnd_);
				}
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

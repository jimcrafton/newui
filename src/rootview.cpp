#include "newui/rootview.h"
#include "newui/application.h"
#include "newui/clipboardmgr.h"
#include "newui/dragndrop.h"
#include "newui/frame.h"
#include "newui/mouse_constants.h"
#include "newui/subview.h"
#include "newui/uiinputmanager.h"
#include "newui/utils.h"
#include "newui/keyboard_constants.h"
#include "newui/uicolormanager.h"
#include "newui/viewstyle.h"
#include "newui/reflection.h"

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cmath>
#include <cstring>

namespace {

	// dirtyRect_ accumulates through float position math (accumulatedOffset()'s
	// repeated +=, min()/max() unions of several views' bounds, ...) and
	// generally ends up with fractional coordinates - fine for
	// InvalidateRect/StretchDIBits (Win32 truncates to int anyway) but not
	// for ctx.clip_to_rect(dirtyRect_), for the same reason
	// Rect::snappedOutwardToPixels()'s own comment (geometry.h) describes
	// in full - this was that fix's own original, narrower use site,
	// before View::paintChildren() needed the identical logic too
	// (view.cpp) and it got promoted to a shared Rect method.
	newui::Rect snappedToPixels(const newui::Rect& r) {
		return r.snappedOutwardToPixels();
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

namespace {

	// -1 = never set; otherwise a RepaintMode value.
	std::atomic<int> g_repaintModeOverride{ -1 };

	newui::RepaintMode readEnvironmentRepaintMode() {
		char buffer[32] = {};
		const DWORD length = ::GetEnvironmentVariableA("NEWUI_REPAINT", buffer, DWORD(sizeof(buffer)));
		newui::RepaintMode mode = newui::RepaintMode::Dirty;
		if (length > 0 && length < sizeof(buffer)) {
			newui::parseRepaintMode(buffer, mode);
		}
		return mode;
	}

	bool readEnvironmentVerifyRepaint() {
		char buffer[8] = {};
		const DWORD length = ::GetEnvironmentVariableA("NEWUI_VERIFY_REPAINT", buffer, DWORD(sizeof(buffer)));
		return length > 0 && !(length == 1 && buffer[0] == '0');
	}

}

namespace newui {

	bool parseRepaintMode(const std::string& text, RepaintMode& out) {
		std::string lower;
		for (char c : text) {
			lower += char(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower == "full") {
			out = RepaintMode::Full;
			return true;
		}
		if (lower == "dirty") {
			out = RepaintMode::Dirty;
			return true;
		}
		return false;
	}

	RepaintMode defaultRepaintMode() {
		const int override = g_repaintModeOverride.load();
		if (override >= 0) {
			return static_cast<RepaintMode>(override);
		}
		static const RepaintMode fromEnvironment = readEnvironmentRepaintMode();
		return fromEnvironment;
	}

	void setDefaultRepaintMode(RepaintMode mode) {
		g_repaintModeOverride.store(static_cast<int>(mode));
	}

	bool defaultVerifyRepaint() {
		static const bool fromEnvironment = readEnvironmentVerifyRepaint();
		return fromEnvironment;
	}

	void RootView::setRepaintMode(RepaintMode mode) {
		repaintMode_ = mode;
	}

	void RootView::setVerifyRepaint(bool verify) {
		verifyRepaint_ = verify;
	}

	RootView::RootView(Frame* frame, const newui::Rect& bounds, const std::string& name) : parentFrame_(frame) {
		surface_ = createPresentSurface(defaultPresentBackend());
		bounds_ = bounds;
		name_ = name;

		// A RootView is its own root - rootView() (and therefore
		// ViewStyle::markDirty()'s view_->rootView() chain) needs this set
		// on itself, not just propagated down to children (see
		// addChild()).
		setRootView(this);
		initDefaultBackground();
	}

	RootView::RootView(HWND externalParentHwnd, HINSTANCE instanceHandle, const newui::Rect& bounds, const std::string& name)
		: externalParentHwnd_(externalParentHwnd), externalInstanceHandle_(instanceHandle) {
		surface_ = createPresentSurface(defaultPresentBackend());
		bounds_ = bounds;
		name_ = name;

		// Same reasoning as the Frame-owned constructor above.
		setRootView(this);
		initDefaultBackground();
	}

	// Unlike a mid-tree SubView (which should stay transparent by default
	// and let its parent's own painting show through - ViewStyle::
	// backgroundFill()'s own PaintKind::None default, unchanged), a
	// RootView always *is* the whole real window's own background - give
	// it a real one by default, rather than leaving every RootView's
	// window content area rendering as solid black (unpainted) until some
	// caller sets this explicitly. UIColorManager::colorFor() is called
	// once here (a live query, not a value baked into this class), same
	// as any other caller of it - if the OS theme later changes, this
	// initial value goes stale the same way overlay1.cpp's own one-time
	// setBackgroundColor() call would; re-tracking that live is a
	// separate concern (Frame::handleMessage()'s WM_THEMECHANGED handling
	// refreshes ThemedViewStyle's native theme handles, not plain
	// ViewStyle fills - see its own comment).
	void RootView::initDefaultBackground() {
		style().backgroundFill().setKind(gfx::PaintKind::Color);
		style().backgroundFill().setColor(UIColorManager::colorFor(UIColorRole::WindowBackground));
	}

	RootView::~RootView() {
		// See aliveFlag_'s own doc comment (rootview.h) - a repaint task
		// posted via scheduleRepaint() can still be sitting in RunLoop's
		// idle queue after this point; this is what tells it to no-op
		// instead of touching a destroyed RootView.
		*aliveFlag_ = false;
	}

	void RootView::setBounds(const Rect& bounds) {
		if (bounds == bounds_) {
			return;
		}

		bounds_ = bounds;

		// Before onSizeChanged()/updateLayout()/resizeImageBuffer() below -
		// none of those trigger the actual repaint synchronously except
		// resizeImageBuffer() (via repaint()), but viewSized()
		// is for updating overlay_'s own extra state, not for painting, so
		// it just needs to run before that eventual repaint, same as
		// updateLayout() needing to run before it for childViews_.
		if (overlay_) {
			overlay_->viewSized(bounds_);
		}

		// updateLayout() before resizeImageBuffer(): the latter is what
		// triggers the actual repaint (via repaint()), so
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

	void RootView::resizeImageBuffer(int width, int height) {
		// The surface owns the pixel buffer and its zero-fill - see
		// PresentSurface::resize()/GdiPresentSurface::resize().
		if (!surface_->resize(width, height, imageBufferFormat())) {
			return;
		}

		dirtyRect_ = newui::Rect( 0,0, width, height);
		repaint();
	}

	PresentBackend RootView::presentBackend() const {
		return surface_->backend();
	}

	void RootView::setPresentBackend(PresentBackend backend) {
		surface_ = createPresentSurface(backend);
		surface_->setWindow(viewHwnd_);
	}

	BLFormat RootView::imageBufferFormat() const {
		return BL_FORMAT_XRGB32;
	}

	void RootView::markDirty() {
		dirtyRect_ = snappedToPixels(this->getClientBounds());
		scheduleRepaint();
	}

	void RootView::repaintNow() {
		dirtyRect_ = snappedToPixels(this->getClientBounds());
		repaint();
	}


	void RootView::markDirty(const View* fromView, const newui::Rect& rect)
	{
		// Union, not assignment: several markDirty() calls can land before the
		// next repaint() consumes dirtyRect_ (scheduleRepaint() defers it to
		// an idle pass), and each one's region has to survive - that
		// accumulation is the whole reason the deferral coalesces a burst into
		// one repaint. The incoming rect is snapped to whole pixels first (see
		// snappedToPixels()), and dirtyRect_ already is, so the union stays
		// integer.
		dirtyRect_ = dirtyRect_.united(snappedToPixels(fromViewToLocal(fromView, rect)));
		scheduleRepaint();
	}

	void RootView::refreshThemes() {
		closeThemedStyles(*this);
		markDirty();
	}

	void RootView::scheduleRepaint() {
		// repaintScheduled_ only flips true once a postIdle() task is
		// genuinely queued to reset it, hence the RunLoop::current() check
		// first - real, confirmed bug otherwise: a markDirty() before
		// RunLoop::run() has started on this thread (e.g. building a
		// Splitter/ScrollView/TreeView tree via addChild() before app.run())
		// used to set the flag with no task ever queued to clear it, silently
		// dropping every later scheduleRepaint() for the rest of the process's
		// life (the window then only repainted via a real OS resize, which
		// reaches repaint() through resizeImageBuffer() instead, bypassing
		// this flag entirely).
		if (repaintScheduled_ || !RunLoop::current()) {
			return;
		}
		repaintScheduled_ = true;

		// Captured by value - this queued task may outlive *this*, see
		// aliveFlag_'s own doc comment (rootview.h).
		std::shared_ptr<bool> alive = aliveFlag_;
		RunLoop::current().postIdle([this, alive]() {
			if (*alive) {
				repaintScheduled_ = false;
				// Nothing pending means whatever asked for this repaint was already covered by one that
				// ran in the meantime (a resize repaints in full straight away) - don't do a second.
				if (!dirtyRect_.empty()) {
					repaint();
				}
			}
			return true; // one-shot - done after running once
		});
	}

	void RootView::presentRepaintedBuffer() {
		surface_->present(dirtyRect_);
	}

	// Synchronously renders this RootView's tree into the surface, then hands the region that changed
	// (dirtyRect_) to the screen: a blank buffer first, then onRedrawNeeded (for content that draws ahead of
	// this RootView's own tree - see its doc comment, rootview.h), then the tree itself, then
	// presentRepaintedBuffer(). Reached from scheduleRepaint()'s deferred idle task (markDirty()), and
	// directly from resizeImageBuffer()/repaintNow().
	//
	// Either the whole window is re-rendered from a blank buffer (RepaintMode::Full) or just dirtyRect_ is
	// (RepaintMode::Dirty) - in both cases blank first, then everything that shows there drawn fresh, so the
	// result is a pure function of the tree's current state and repainting is idempotent: a repaint can't
	// change a pixel of anything that hasn't itself changed. (It used to fill the root background only
	// inside dirtyRect_ while still redrawing every child over the whole window, so anything outside the
	// dirty rect - a transparent Label's anti-aliased glyph edges most visibly - was composited onto its own
	// previous pixels on every unrelated repaint and thickened a little each time.) In Dirty mode the region
	// is also all that's *touched*: every draw is clipped to it and children that miss it are skipped, which
	// is what makes a small hover repaint cheap.
	void RootView::repaint() {
		const bool hasBuffer = surface_->isValid();
		const newui::Rect whole = hasBuffer
			? newui::Rect(0.0f, 0.0f, float(surface_->image().size().w), float(surface_->image().size().h))
			: newui::Rect();

		// A listener draws straight into the buffer, unclipped, ahead of the tree - it can't be confined to a
		// region, so a RootView with one always repaints in full.
		const bool prune = hasBuffer && repaintMode_ == RepaintMode::Dirty && onRedrawNeeded.empty();
		const newui::Rect region = prune ? dirtyRect_.intersected(whole) : whole;
		// Nothing to do when pruning and nothing is dirty (or it's all outside the buffer).
		const bool render = hasBuffer && (!prune || (region.width() > 0.0f && region.height() > 0.0f));

		// Blank first, and before onRedrawNeeded so a handler that draws ahead of the tree keeps what it
		// draws (and, like everything else, redraws it fresh on the next repaint). Transparent black is exactly
		// what a brand-new buffer holds, so the first frame is unchanged.
		if (render) {
			BLContext ctx(surface_->image());
			if (prune) {
				ctx.clip_to_rect(region);
			}
			ctx.clear_all();
			ctx.end();
		}

		onRedrawNeeded(*this);

		if (render) {
			paintTree(surface_->image(), region, prune);
			if (prune && verifyRepaint_) {
				verifyPrunedRepaint(region);
			}
		}

		presentRepaintedBuffer();

		// Here, not inside presentRepaintedBuffer(): that's an overridable
		// hook (PopupTool replaces it outright), and whether dirtyRect_ gets
		// consumed must not depend on what a subclass's version remembers to do.
		dirtyRect_.clear();
	}

	void RootView::paintTree(BLImage& image, const newui::Rect& region, bool clip) {
		BLContext ctx(image);

		// The clip is set once, here, and every phase below stays inside it: View::paintChildren()'s
		// pre/post-paint phases (focus ring, drop shadow) call restore_clipping(), but that only restores to
		// the state saved just before - i.e. it drops the *child's own* clip, never an ancestor's.
		if (clip) {
			ctx.clip_to_rect(region);
		}

		paintStyle(ctx);
		paint(ctx);

		// paintChildren() walks every child (no dirty-rect pruning by the old whole-window walk - see its own
		// comment (view.h) for why that was tried and reverted) but skips any whose whole drawn extent lies
		// outside the part of this window that can be seen: here that's `region` - the whole window for a full
		// repaint, just the dirty region when pruning. Each child still only ever clips to its own full bounds
		// via the unchanged ctx.clip_to_rect() inside paintChildren(), which stays intersected with the region
		// clip above - both whole-pixel rects, which is what keeps themed/pattern-filled children (uxtheme's
		// DrawThemeBackground - ThemedViewStyle, viewstyle.h) away from a real Blend2D JIT bug
		// ('is_rect_fill()' assertion) that a *combined* clip with fractional edges hits. Redrawing is safe
		// because the region was blanked first: each child draws over a fresh backdrop, so one with no opaque
		// background of its own (a plain Label) reproduces the same pixels every time.
		visibleRegion_ = region;
		hasVisibleRegion_ = true;
		paintChildren(ctx);

		// Last, on top of every child - see Overlay's own class comment (overlay.h). Inside the same clip.
		if (overlay_ && overlay_->visible()) {
			overlay_->paint(ctx, Rect(0.0f, 0.0f, bounds_.size().width, bounds_.size().height));
		}

		ctx.end();
	}

	namespace {

		// How far (per channel, out of 255) a pruned repaint's pixel may be from a full repaint's and still
		// count as the same. Not zero on purpose: Blend2D's rasterizer clips vector edges (a glyph's
		// outline, say) at the clip box with fixed-point rounding, so the pixels right along a clip edge can
		// land a level off from an unclipped render of the same thing - measured at exactly 1 across 486
		// clip regions cutting through text, buttons and sliders (RootViewPruning's edge test). That's
		// invisible and can't accumulate (each repaint restarts from a blank region), whereas a change that
		// wasn't invalidated shows up as a large difference.
		constexpr int kVerifyTolerance = 2;

		bool pixelsDiffer(const std::uint8_t* a, const std::uint8_t* b) {
			for (int channel = 0; channel < 4; ++channel) {
				const int delta = int(a[channel]) - int(b[channel]);
				if (delta > kVerifyTolerance || delta < -kVerifyTolerance) {
					return true;
				}
			}
			return false;
		}

	}

	void RootView::verifyPrunedRepaint(const newui::Rect& region) {
		BLImage& actual = surface_->image();
		const int width = actual.size().w;
		const int height = actual.size().h;

		BLImage expected;
		if (expected.create(width, height, actual.format()) != BL_SUCCESS) {
			return;
		}
		{
			BLContext ctx(expected);
			ctx.clear_all();
			ctx.end();
		}
		paintTree(expected, newui::Rect(0.0f, 0.0f, float(width), float(height)), false);

		BLImageData a, e;
		actual.get_data(&a);
		expected.get_data(&e);
		int differing = 0, minX = width, minY = height, maxX = -1, maxY = -1;
		for (int y = 0; y < height; ++y) {
			const auto* rowA = static_cast<const std::uint8_t*>(a.pixel_data) + intptr_t(y) * a.stride;
			const auto* rowE = static_cast<const std::uint8_t*>(e.pixel_data) + intptr_t(y) * e.stride;
			if (std::memcmp(rowA, rowE, size_t(width) * 4) == 0) {
				continue;
			}
			for (int x = 0; x < width; ++x) {
				if (pixelsDiffer(rowA + size_t(x) * 4, rowE + size_t(x) * 4)) {
					++differing;
					minX = x < minX ? x : minX;
					minY = y < minY ? y : minY;
					maxX = x > maxX ? x : maxX;
					maxY = y > maxY ? y : maxY;
				}
			}
		}
		if (differing == 0) {
			return;
		}

		++verifyMismatches_;
		char message[320];
		std::snprintf(message, sizeof(message),
			"newui: RootView '%s': the pruned repaint of (%d,%d %dx%d) left %d px that differ from a full repaint, within (%d,%d)-(%d,%d) - "
			"something changed there without invalidating it\n",
			name_.c_str(), int(region.left()), int(region.top()), int(region.width()), int(region.height()),
			differing, minX, minY, maxX, maxY);
		std::fputs(message, stderr);
		::OutputDebugStringA(message);
	}

	void RootView::setOverlay(std::unique_ptr<Overlay> overlay) {
		overlay_ = std::move(overlay);
		if (overlay_) {
			overlay_->viewSized(bounds_);
		}
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
		// Through the surface, not a bare ::InvalidateRect(): that only asks Windows for a WM_PAINT,
		// which does nothing at all under DXGI (paint() is passive there) - so anything that drew
		// straight into getImageBuffer() and then called this was never shown. present() is the one
		// backend-neutral "put this region of the buffer on the screen" (GDI: InvalidateRect; DXGI:
		// upload the region and Present).
		//
		// And it deliberately leaves dirtyRect_ alone. That's a *different* thing - the region still
		// waiting for the deferred repaint() (see markDirty()) - and it used to be cleared here too,
		// so calling this while a repaint was pending made that repaint present an empty region and
		// the pending update was never shown.
		surface_->present(invalidArea != nullptr ? *invalidArea : newui::Rect(newui::Point(0.0f, 0.0f), bounds_.size()));
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
		// Same fix as SubView::setVisible() (subview.cpp) - see its own
		// comment for the real bug this guards against.
		redraw();
	}

	void RootView::addChild(SubView* child)
	{
		child->setParentView(this);
		// propagateRootView(), not setRootView(): child may already have
		// its own subtree (built before being attached here), and every
		// descendant in it needs to pick up this RootView too, not just
		// child itself.
		child->propagateRootView(rootView());
		// Raw parent_ bookkeeping via internal_setParent() (view.h) - see
		// SubView::addChild()'s own comment (subview.cpp) for why not setParent().
		child->internal_setParent(this);
		View::addChild(child);

	}

	void RootView::removeChild(SubView* child) {
		notifySubViewRemoved(child);
		View::removeChild(child);
		child->setParentView(nullptr);
		child->internal_setParent(nullptr);  // see addChild()'s own comment on why not setParent()
		child->propagateRootView(nullptr);
	}

	std::string RootView::generateDefaultName(const View& view) {
		const reflection::Class* cls = reflection::classinfo(typeid(view));
		std::string base = (cls != nullptr) ? cls->name() : std::string("view");
		if (!base.empty()) {
			base[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(base[0])));
		}
		return nameManager_.generateName(base);
	}

	Point RootView::accumulatedOffset(const SubView* view) const {
		return (view != nullptr) ? view->localToRoot(Point(0.0f, 0.0f)) : Point(0.0f, 0.0f);
	}

	void RootView::updateHoveredSubView(SubView* target, const Point& rootPt) {
		if (target == hoveredSubView_) {
			return;
		}

		if (hoveredSubView_ != nullptr) {
			SubView* left = hoveredSubView_;
			if (!left->isDesignTime()) {
				left->onMouseLeft(*left, rootPt - accumulatedOffset(left), 0, 0);
				left->setHighlighted(false);
				left->style().markDirty();
			}
		}

		hoveredSubView_ = target;

		if (hoveredSubView_ != nullptr) {
			if (!hoveredSubView_->isDesignTime()) {
				hoveredSubView_->onMouseEntered(*hoveredSubView_, rootPt - accumulatedOffset(hoveredSubView_), 0, 0);
				hoveredSubView_->setHighlighted(true);
				hoveredSubView_->style().markDirty();
			}
		}
	}

	void RootView::setFocusedSubView(SubView* target) {
		if (target == focusedSubView_) {
			return;
		}

		if (focusedSubView_ != nullptr && !focusedSubView_->canResignFocus()) {
			return;
		}

		if (target != nullptr) {

			if (!target->canBecomeFocused()) {
				return;
			}
		}

		// markDirty() on both the outgoing and incoming View - same
		// "whoever's tracking this state is also responsible for
		// scheduling the repaint it visually depends on" convention
		// updateHoveredSubView() already established just above for
		// hoveredSubView_/isHighlighted(). Without this, isFocused()
		// (view.h) genuinely does flip for both Views the instant this
		// function returns - Control::canPerformCommand()/keyEvent()
		// dispatch, TextController's own markDirty() calls, etc. all
		// already worked correctly - but nothing ever asked for a
		// repaint, so View::postPaintStyle()'s ViewStyle::postPaint()
		// call (the generic dashed ring every non-ThemedEditStyle control
		// relies on - Button, Toggle, Slider, a FocusScope panel, ...)
		// never actually ran again until some *unrelated* later event
		// happened to trigger one - a real, confirmed live bug: tabbing
		// from a Button through several more controls visibly changed
		// nothing at all, even though focus really was moving underneath.
		if (focusedSubView_ != nullptr) {
			focusedSubView_->style().markDirty();
		}

		if (focusedSubView_ != nullptr) {
			focusedSubView_->onLostFocus(*focusedSubView_);
		}

		focusedSubView_ = target;

		if (focusedSubView_ != nullptr) {
			focusedSubView_->onGotFocus(*focusedSubView_);
			focusedSubView_->style().markDirty();
		}
	}

	bool RootView::canPerformCommand(const CommandId& cmd) const {
		// v != this excludes RootView itself from the polymorphic walk -
		// v->canPerformCommand(cmd) is a virtual call, and this override
		// *is* View::canPerformCommand for a RootView, so letting the
		// walk reach `this` and call it polymorphically would re-enter
		// this same function (a direct child's parent() is this
		// RootView) - infinite recursion. The explicit
		// View::canPerformCommand(cmd) call below reaches RootView-as-
		// leaf's own answer (defaulted false unless a subclass overrides
		// it) via ordinary non-virtual base dispatch instead.
		for (View* v = focusedSubView_; v != nullptr && v != this; v = v->parent()) {
			if (v->canPerformCommand(cmd) && !v->isDesignTime()) {
				return true;
			}
		}
		return View::canPerformCommand(cmd);
	}

	void RootView::performCommand(const CommandId& cmd) {
		// Same v != this reasoning as canPerformCommand() above.
		for (View* v = focusedSubView_; v != nullptr && v != this; v = v->parent()) {
			if (v->canPerformCommand(cmd)) {
				v->performCommand(cmd);
				return;
			}
		}
		if (View::canPerformCommand(cmd)) {
			View::performCommand(cmd);
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
			// Recover to the nearest still-alive ancestor that can hold
			// focus, rather than just dropping it to nullptr - walk up
			// from removedSubtreeRoot's own parent (still intact at this
			// point; only removedSubtreeRoot's own subtree is going away,
			// same as hoveredSubView_/capturedSubView_ above). Stops
			// naturally at `this` (a RootView is never itself a focus
			// target - see moveFocus()'s own candidate gathering,
			// uiinputmanager.cpp) or wherever the SubView chain runs out.
			//
			// Deliberately bypasses setFocusedSubView()'s
			// canResignFocus() veto and never fires onLostFocus() on the
			// doomed view - it's being destroyed regardless of what it
			// wants, same as hoveredSubView_/capturedSubView_ being
			// silently cleared just above with no onMouseLeft/etc. fired
			// either. onGotFocus() *is* fired on the recovered target
			// though (unlike the silent hover/capture clears, which have
			// no "recovered to" concept at all) - real controls
			// (ThemedEditStyle-based ones especially, see this class's
			// own "visual focus indication" follow-up) rely on that hook
			// actually firing to show real focus feedback, not just
			// isFocused() flipping.
			SubView* recovered = nullptr;
			for (View* v = removedSubtreeRoot->parent(); v != nullptr && v != this; v = v->parent()) {
				SubView* candidate = dynamic_cast<SubView*>(v);
				if (candidate != nullptr && candidate->canBecomeFocused()) {
					recovered = candidate;
					break;
				}
			}
			focusedSubView_ = recovered;
			if (focusedSubView_ != nullptr) {
				focusedSubView_->onGotFocus(*focusedSubView_);
				focusedSubView_->style().markDirty();
			}
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
	SubView* RootView::resolveInteractiveHit(const Point& pt, Point& outLocalPt) const {
		SubView* hit = hitTestChildren(pt, outLocalPt);
		//return (hit != nullptr && !hit->isDesignTime()) ? hit : nullptr;
		return hit;//(hit != nullptr) ? hit : nullptr;
	}

	void RootView::mouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseDown(*this, pt, btnMask, keyMask);

		Point localPt;
		SubView* target = resolveInteractiveHit(pt, localPt);
		capturedSubView_ = target;

		// Mouse *capture* (and therefore which View this gesture's own
		// onMouseDown/onMouseMove/onMouseUp reach) always stays the exact
		// hit target above - only which View this click hands *keyboard
		// focus* to goes through UIInputManager's policy: a plain
		// non-focusable SubView (a container/decoration, or a control
		// like Stepper/ScrollBar that deliberately shouldn't steal focus -
		// see View::acceptsFocus(), view.h) walks up to the nearest
		// focusable ancestor instead of focusing itself, same as clicking
		// a Button's own drawn label still focuses the Button.
		setFocusedSubView(UIInputManager::instance().resolveClickFocusTarget(target));

		if (target != nullptr) {
			if (!target->isDesignTime()) {
				target->onMouseDown(*target, localPt, btnMask, keyMask);
			}
		}

		// Arms a potential outgoing drag - only for a left-button press on
		// a View that actually has a DropSource with something listening
		// (View::dragSource(), never allocated just by asking - see its
		// own comment, view.h). mouseMove() below decides whether this
		// turns into a real drag once the movement threshold is crossed;
		// always (re)assigned here, including back to nullptr, so a stale
		// arm from an unrelated earlier press never lingers.
		DropSource* dragSource = (target != nullptr) ? target->dragSource() : nullptr;
		bool hasDragPayload = dragSource != nullptr
			&& (!dragSource->onProvideFiles.empty() || !dragSource->onProvideText.empty());
		if (hasDragPayload && (btnMask & mbmLeftButton) != 0) {
			activeDragView_ = target;
			activeDragLocalPt_ = localPt;
		} else {
			activeDragView_ = nullptr;
		}
	}

	void RootView::mouseMove(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask)
	{
		onMouseMove(*this, pt, btnMask, keyMask);

		Point hoverLocalPt;
		SubView* hoverTarget = resolveInteractiveHit(pt, hoverLocalPt);
		updateHoveredSubView(hoverTarget, pt);

		SubView* dispatchTarget = capturedSubView_ != nullptr ? capturedSubView_ : hoverTarget;
		if (dispatchTarget != nullptr) {
			
			::SetCursor(dispatchTarget->cursor().handle());

			Point localPt = (dispatchTarget == hoverTarget) ? hoverLocalPt : (pt - accumulatedOffset(dispatchTarget));
			if (!dispatchTarget->isDesignTime()) {
				dispatchTarget->onMouseMove(*dispatchTarget, localPt, btnMask, keyMask);
			}
		}
		else {
			::SetCursor(this->cursor().handle());
		}
		// Outgoing-drag gesture detection - only while a drag is actually
		// armed (mouseDown() above set this on a View with a DropSource
		// listener) and the left button is still held. Re-fetches
		// dragSource() rather than trusting one captured at arm time,
		// since it's a plain settable pointer application code could in
		// principle clear mid-gesture.
		if (activeDragView_ != nullptr) {

			if (!activeDragView_->isDesignTime()) {
				DropSource* dragSource = activeDragView_->dragSource();
				if (dragSource == nullptr || (btnMask & mbmLeftButton) == 0) {
					activeDragView_ = nullptr;
				}
				else {
					Point currentLocalPt = pt - accumulatedOffset(activeDragView_);
					Point delta = currentLocalPt - activeDragLocalPt_;
					const float thresholdSquared = 16.0f;  // ~4px - distinguishes a drag from a click
					if ((delta.x * delta.x + delta.y * delta.y) >= thresholdSquared) {
						SubView* dragView = activeDragView_;
						activeDragView_ = nullptr;

						// Files take priority over text, matching DropTarget's
						// own CF_HDROP-before-CF_UNICODETEXT convention
						// (dragndrop.h) - only one kind of drag can actually
						// start per gesture.
						std::vector<VirtualFile> files;
						std::wstring text;
						bool providingFiles = dragSource->onProvideFiles.syncCallFirst(*dragSource, files).handled();
						bool providingText = !providingFiles
							&& dragSource->onProvideText.syncCallFirst(*dragSource, text).handled();

						if (providingFiles || providingText) {
							// DoDragDrop() (inside StartDragOperation()/
							// StartVirtualFileDrag()) does its own mouse
							// tracking/capture internally - release this
							// RootView's own Win32 capture first rather than
							// leaving two independent capture holders active at
							// once (capturedSubView_ itself is left as-is;
							// DoDragDrop() consumes the terminating button-up
							// directly, so it never sees a matching mouseUp() -
							// a known, harmless rough edge until this class
							// learns to let go of its own bookkeeping around a
							// drag more deliberately).
							::ReleaseCapture();
							Point windowPt = accumulatedOffset(dragView) + currentLocalPt;
							POINT ptClient{ static_cast<LONG>(windowPt.x), static_cast<LONG>(windowPt.y) };

							DWORD rawEffect = DROPEFFECT_NONE;
							if (providingFiles) {
								StartVirtualFileDrag(windowHandle(), std::move(files), ptClient, DROPEFFECT_COPY, &rawEffect);
							}
							else {
								StartDragOperation(windowHandle(), text, ptClient, DROPEFFECT_COPY, &rawEffect);
							}
							dragSource->onDragComplete(*dragSource, toDropEffect(rawEffect));
						}
					}
				}
			}
		}
	}

	void RootView::mouseWheel(const Point& pt, float mouseDelta, std::uint32_t /*btnMask*/, std::uint32_t /*keyMask*/)
	{
		onMouseWheel(*this, pt, mouseDelta);

		Point localPt;
		SubView* target = resolveInteractiveHit(pt, localPt);

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
			if (!sv->isDesignTime()) {
				if (sv->onMouseWheel.syncCallFirst(*sv, localPt, mouseDelta).handled()) {
					return;
				}
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
			target = resolveInteractiveHit(pt, localPt);
		}

		capturedSubView_ = nullptr;
		activeDragView_ = nullptr;

		if (target != nullptr) {
			if (!target->isDesignTime()) {
				target->onMouseUp(*target, localPt, btnMask, keyMask);
			}
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
		SubView* target = resolveInteractiveHit(pt, localPt);
		capturedSubView_ = target;
		// See mouseDown()'s own comment - same capture-vs-focus split.
		setFocusedSubView(UIInputManager::instance().resolveClickFocusTarget(target));

		if (target != nullptr) {
			if (!target->isDesignTime()) {
				target->onMouseDblClick(*target, localPt, btnMask, keyMask);
			}
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
		// Tab is reserved for UIInputManager's focus navigation, never
		// forwarded to onKeyDown/onKeyPress/onKeyUp - this RootView's own
		// or focusedSubView_'s - the same way a real dialog's tab order
		// swallows Tab rather than letting a control see it as an
		// ordinary keystroke. keKeyDown is the one that actually
		// navigates; keKeyPress (WM_CHAR's synthesized '\t', delivered
		// via handleMessage()'s TranslateMessage() call same as any other
		// character) and keKeyUp for the same physical keypress are just
		// as deliberately ignored here, not merely unhandled.
		//
		// Unless the currently focused View opts out via wantsTabKey()
		// (view.h) - e.g. a future code editor that wants a literal tab
		// character instead of a focus change - in which case Tab isn't
		// intercepted at all here; it falls straight through to the
		// ordinary dispatch below, same as any other key.
		bool focusedViewWantsTabKey = focusedSubView_ != nullptr && focusedSubView_->wantsTabKey();
		if (VKeyCode == vkTab && !focusedViewWantsTabKey) {
			if (eventType == keKeyDown) {
				UIInputManager::instance().moveFocus(*this,
					(keyMask & kmShift) != 0 ? FocusNavigationDirection::Previous : FocusNavigationDirection::Next);
			}
			return;
		}

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

		// Arrow keys route through UIInputManager instead of the plain
		// focusedSubView_->onKeyDown() call below, same "detect the key,
		// hand the whole thing off to UIInputManager" shape the vkTab
		// block above already uses - routeArrowKeyDown() does its own
		// dispatch to focusedSubView_ first (a ListView moving its
		// selection, a Slider changing its value, ...) and only falls
		// back to a cross-control spatial jump if that dispatch goes
		// unhandled, so this doesn't *also* need to call
		// focusedSubView_->onKeyDown() itself - see routeArrowKeyDown()'s
		// own doc comment (uiinputmanager.h) for the rest. Only
		// keKeyDown - keKeyPress never fires for a non-character key like
		// an arrow anyway, and keKeyUp still wants the plain dispatch
		// below (a View reacting to the key actually being released is
		// no different for an arrow than for any other key).
		bool isArrowKey = VKeyCode == vkUpArrow || VKeyCode == vkDownArrow || VKeyCode == vkLeftArrow || VKeyCode == vkRightArrow;
		if (isArrowKey && eventType == keKeyDown) {
			UIInputManager::instance().routeArrowKeyDown(*this, keyMask, keyCharVal, repeatCount, VKeyCode);
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

			case WM_SIZE: {
				// Only for a standalone RootView (no Frame) - Windows
				// delivers this directly when something else (e.g. a VSIX
				// host, or this RootView's own setBounds() below via its
				// ::SetWindowPos() call - WM_SIZE is dispatched synchronously,
				// reentrantly, before that call even returns) resizes this
				// RootView's own real HWND, and nothing external does that
				// C++-side setBounds() call for it the way
				// Frame::updateViewBounds() does in the Frame-owned case.
				// Explicitly gated on parentFrame_ rather than relying on
				// setBounds()'s own "bounds == bounds_" early-out to make a
				// Frame-owned call here harmless - Frame is already the one
				// true owner of this RootView's bounds in that case, and
				// this stays a deliberate no-op (falls through to
				// DefWindowProcA) rather than a second, merely-redundant
				// path to the same result.
				//
				// bounds_.pos() here, not a hardcoded Point(0,0) - WM_SIZE's
				// lParam only ever carries the new client size, never a
				// position, so this has to reuse whatever position bounds_
				// already holds rather than inventing one. That happens to
				// already equal (0,0) for the one pre-existing standalone
				// use case (a RootView hosted as a WS_CHILD inside a VSIX
				// host's own window, always at that parent's origin), so
				// this is a no-op change there - but a WS_POPUP standalone
				// RootView (PopupTool, popuptool.h) has real *screen*
				// coordinates in bounds_, and a hardcoded Point(0,0) here
				// would silently snap it to the screen's top-left corner on
				// every resize (via the reentrant path above) instead of
				// leaving its position alone.
				if (nullptr == parentFrame_) {
					Size sz(LOWORD(lParam), HIWORD(lParam));
					setBounds(Rect(bounds_.pos(), sz));
					result = true;
				}
			}
			break;

			case WM_PAINT: {

				//this checks if we truly have work to do,
				//if non zero return then returns with no painting
				if (!GetUpdateRect(viewHwnd_, NULL, FALSE)) {
					outLRESULT = 0;
					result = true;
					return result;
				}

				PAINTSTRUCT ps;
				HDC hdc = ::BeginPaint(viewHwnd_, &ps);

				newui::Rect paintRect = ps.rcPaint;

				surface_->paint(hdc, paintRect);
				::EndPaint(viewHwnd_, &ps);
				result = true;
			}
			break;

			case WM_ERASEBKGND: {
				// surface_->paint() (WM_PAINT above) always covers
				// the whole client area from the surface's buffer - a separate erase
				// first just flashes the window class's own background brush
				// before that real paint overwrites it, causing visible
				// flicker on every resize/redraw.
				outLRESULT = 1;
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
						printf("DEBUG WM_KEYDOWN wParam=%llu (0x%llx) -> VKeyCode=%u focusedSubView_=%p\n",
							(unsigned long long)wParam, (unsigned long long)wParam, keyData.VKeyCode, (void*)focusedSubView_);
						fflush(stdout);
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

			case WM_RENDERFORMAT: {
				// No OpenClipboard()/CloseClipboard() here - this
				// message's own documented contract is that the
				// clipboard is already open and owned by this window
				// for its duration. See ClipboardManager::
				// setDelayedRenderer()/handleRenderFormat().
				ClipboardManager::handleRenderFormat(static_cast<UINT>(wParam));
				result = true;
			}
			break;

			case WM_RENDERALLFORMATS: {
				// Fired just before this window (the clipboard owner)
				// is destroyed - unlike WM_RENDERFORMAT, this one does
				// need its own OpenClipboard()/CloseClipboard(), which
				// ClipboardManager::handleRenderAllFormats() does
				// internally.
				ClipboardManager::handleRenderAllFormats(viewHwnd_);
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
			thisPtr->surface_->setWindow(hWnd);
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

	void RootView::preCreateHints(size_t& wndClassFlags, size_t& windowStyleFlags, size_t& windowExStyleFlags)
	{

	}


	bool RootView::initialize()
	{
		bool result = true;

		HWND parent = parentFrame_ ? parentFrame_->frameHandle() : externalParentHwnd_;
		HINSTANCE hinst = parentFrame_ ? Application::instance().instanceHandle() : externalInstanceHandle_;

		if (nullptr == parent) {
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
		wcex.hInstance = hinst;
		wcex.hIcon = NULL;
		wcex.hCursor = NULL;//LoadCursor(NULL, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_HIGHLIGHT+1);
		wcex.lpszMenuName = NULL;
		wcex.lpszClassName = className.c_str();
		wcex.hIconSm = NULL;


		size_t wndClassFlags = wcex.style;
		size_t windowStyleFlags = SIMPLE_VIEW;
		size_t windowExStyleFlags = 0;
		preCreateHints(wndClassFlags, windowStyleFlags, windowExStyleFlags);
		wcex.style = static_cast<UINT>(wndClassFlags);

		RegisterClassExA(&wcex);

		auto hwnd = ::CreateWindowExA( static_cast<DWORD>(windowExStyleFlags), className.c_str(), "",
						windowStyleFlags,
						bounds_.left(),
						bounds_.top(),
						bounds_.size().width,
						bounds_.size().height,
						parent,
						NULL,
						hinst,
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

		postCreate();

		::ShowWindow(viewHwnd_, SW_SHOW);
		::SetFocus(viewHwnd_);

		return true;
	}

	void RootView::postCreate()
	{

	}

	void RootView::viewCreated()
	{
		onCreated(*this);

		// Every window gets exactly one real IDropTarget, registered
		// automatically - see comDropTarget_'s own comment (rootview.h).
		// RegisterDragDrop() needs OLE initialized on this thread -
		// RunLoop::run() does that, but only once it actually starts
		// (Application::run() calls Frame::initialize() - which is what
		// gets here via WM_CREATE - before runLoop_.run() even begins, so
		// this genuinely fires first). OleInitialize() is reference-
		// counted per thread, so calling it again here is safe/idempotent
		// even once RunLoop::run() goes on to make its own call - matched
		// by OleUninitialize() in destroy() below. Both OleInitialize()
		// and RegisterDragDrop() can fail gracefully (a still-not-fatal
		// stance - same as CoCreateInstance failures throughout
		// dragndrop.cpp) - comDropTarget_ just stays null either way.
		if (SUCCEEDED(::OleInitialize(nullptr))) {
			comDropTarget_ = Microsoft::WRL::Make<COMDropTarget>(*this);
			if (FAILED(::RegisterDragDrop(viewHwnd_, comDropTarget_.Get()))) {
				comDropTarget_.Reset();
				::OleUninitialize();
			}
		}
	}

	void RootView::destroy() {
		View::destroy();
		releaseWindow();
	}

	void RootView::releaseWindow() {
		if (nullptr != viewHwnd_) {
			if (comDropTarget_) {
				::RevokeDragDrop(viewHwnd_);
				comDropTarget_.Reset();
				::OleUninitialize();
			}
			DestroyWindow(viewHwnd_);
			viewHwnd_ = nullptr;
			surface_->setWindow(nullptr);
		}
	}

}

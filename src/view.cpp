#include "newui/view.h"
#include "newui/subview.h"
#include "newui/rootview.h"
#include <cassert>

namespace newui {

	View::View()
	{
		this->style_->setView(this);
	}

	bool View::initialize()
	{
		return true;
	}

	void View::destroy()
	{
		markDestroying();
		// Deliberately re-reads childViews_.front() each pass rather than
		// holding an iterator across the loop: child->destroy() removes
		// itself from its parent's childViews_ (see SubView::destroy()'s
		// parent_->removeChild(this)) - when that parent is *this* (the
		// common case, destroying a whole subtree top-down), a live
		// range-based-for iterator would be invalidated out from under it
		// mid-walk. count is a sanity check, not the loop's termination
		// condition (that's childViews_.empty()) - it catches "still had
		// N-original-children left over after N iterations" (e.g.
		// something re-added a child mid-destroy), not "a child failed to
		// remove itself" (that fails earlier and louder: the next
		// iteration's front() would still be the just-deleted child,
		// crashing on the use-after-free before this check is ever
		// reached).
		std::size_t count = childViews_.size();
		while (!childViews_.empty()) {
			SubView* child = childViews_.front();
			child->destroy();  // removes itself from childViews_ - see above
			delete child;
			--count;
			if (count == 0 && !childViews_.empty()) {
				throw std::runtime_error("removal logic busted in this view");
			}
		}

		onDestroyed(*this);
	}

	Point View::localToRoot(const Point& localPt) const
	{
		Point pt = localPt;
		for (const View* cur = this; cur != nullptr; ) {
			const SubView* sv = dynamic_cast<const SubView*>(cur);
			if (sv == nullptr) {
				break;  // reached the RootView (or a detached non-SubView top)
			}
			pt += sv->bounds().pos();
			// The parent may have scrolled its children (origin()) - undo that shift, the
			// exact inverse of paintChildren()'s -origin() translate at each level.
			const View* parent = sv->parent();
			if (parent != nullptr) {
				pt -= parent->origin();
			}
			cur = parent;
		}
		return pt;
	}

	Point View::rootToLocal(const Point& rootPt) const
	{
		Point origin = localToRoot(Point(0.0f, 0.0f));
		return Point(rootPt.x - origin.x, rootPt.y - origin.y);
	}

	Point View::localToScreen(const Point& localPt) const
	{
		Point rootPt = localToRoot(localPt);
		const RootView* root = rootView();
		HWND hwnd = (root != nullptr) ? root->windowHandle() : nullptr;
		if (hwnd == nullptr) {
			return rootPt;
		}
		POINT pt = rootPt;
		::ClientToScreen(hwnd, &pt);
		return pt;
	}

	Point View::screenToLocal(const Point& screenPt) const
	{
		const RootView* root = rootView();
		HWND hwnd = (root != nullptr) ? root->windowHandle() : nullptr;
		Point rootPt = screenPt;
		if (hwnd != nullptr) {
			POINT pt = screenPt;
			::ScreenToClient(hwnd, &pt);
			rootPt = pt;
		}
		return rootToLocal(rootPt);
	}

	Rect View::localToScreen(const Rect& localRect) const
	{
		return Rect(localToScreen(localRect.pos()), localRect.size());
	}

	Rect View::screenToLocal(const Rect& screenRect) const
	{
		return Rect(screenToLocal(screenRect.pos()), screenRect.size());
	}

	Rect View::screenBounds() const
	{
		return localToScreen(Rect(Point(0.0f, 0.0f), bounds().size()));
	}

	Point View::mapTo(const View& target, const Point& localPt) const
	{
		if (rootView() == target.rootView()) {
			return target.rootToLocal(localToRoot(localPt));
		}
		return target.screenToLocal(localToScreen(localPt));
	}

	Rect View::mapTo(const View& target, const Rect& localRect) const
	{
		return Rect(mapTo(target, localRect.pos()), localRect.size());
	}

	void View::addChild(SubView* child)
	{
		childViews_.push_back(child);

		updateLayout();
		// A brand-new child usually gets drawn anyway (the next unrelated
		// repaint walks every current child, this one now included) - but
		// nothing *guarantees* one happens, and updateLayout() above can
		// also reflow every existing sibling around it. redraw() invalidates
		// this whole View's own bounds (not just the new child's) for
		// exactly that reason - see removeChild()'s own comment below,
		// same fix, same reasoning, just the "child appeared" side of it.
		redraw();
	}

	void View::removeChild(SubView* child) {
		childViews_.erase(std::remove(childViews_.begin(), childViews_.end(), child), childViews_.end());
		updateLayout();
		// redraw() (not just updateLayout() above) - a real, confirmed
		// live bug without this: updateLayout() only repositions the
		// *remaining* children via the attached Layout, which has no
		// reason to ever touch the pixels the just-removed child used to
		// occupy. Nothing else invalidates that region either - the next
		// full paint() walk simply skips a child that's no longer in
		// childViews_, it never erases what an earlier frame already
		// composited there - so the old bitmap content stays on screen
		// until some *unrelated* later event happens to repaint over it.
		// Invalidates this whole View's own bounds, not a narrower rect
		// around just the vacated spot, since updateLayout() may also have
		// moved surviving siblings into (or out of) that same area.
		redraw();
	}

	void View::reorderChild(SubView* child, std::size_t newIndex) {
		auto it = std::find(childViews_.begin(), childViews_.end(), child);
		if (it == childViews_.end()) {
			return;
		}
		childViews_.erase(it);
		if (newIndex > childViews_.size()) {
			newIndex = childViews_.size();
		}
		childViews_.insert(childViews_.begin() + newIndex, child);
		updateLayout();
		// Same reasoning as removeChild() above - reordering can move
		// every sibling to a new on-screen position, and nothing else
		// guarantees a repaint of wherever they used to be.
		redraw();
	}

	bool View::setParent(View* newParent) {
		if (newParent == parent_) {
			return true;
		}

		RootView* rootView = dynamic_cast<RootView*>(this);
		if (rootView != nullptr) {
			return false;  // a RootView can never be anyone's child
		}
		SubView* self = dynamic_cast<SubView*>(this);

		for (View* ancestor = newParent; ancestor != nullptr; ancestor = ancestor->parent()) {
			if (ancestor == this) {
				return false;  // would create a cycle
			}
		}

		if (View* oldParent = parent_) {
			oldParent->removeChild(self);
		}
		if (newParent != nullptr) {
			newParent->addChild(self);
		}
		return true;
	}

	void View::setLayout(std::unique_ptr<Layout> layout) {
		layout_ = std::move(layout);
		updateLayout();
	}

	void View::markDestroying() {
		setDestroying();
		for (SubView* child : childViews_) {
			child->markDestroying();
		}
	}

	void View::updateLayout() {
		if (isDestroying()) {
			return;
		}
		if (layout_) {
			layout_->arrange(*this);
		}
	}

	void View::setName(const std::string& name) {
		if (rootView_ != nullptr && !name.empty()) {
			rootView_->nameManager().reserve(name);
		}
		Component::setName(name);
	}

	void View::propagateRootView(RootView* root) {
		setRootView(root);
		if (root != nullptr) {
			if (name_.empty()) {
				name_ = root->generateDefaultName(*this);
			} else {
				root->nameManager().reserve(name_);
			}
		}
		for (SubView* child : childViews_) {
			child->propagateRootView(root);
		}
	}

	View* View::findView(const std::string& name) {
		if (name_ == name) {
			return this;
		}
		for (SubView* child : childViews_) {
			if (View* found = child->findView(name)) {
				return found;
			}
		}
		return nullptr;
	}

	const View* View::findView(const std::string& name) const {
		if (name_ == name) {
			return this;
		}
		for (const SubView* child : childViews_) {
			if (const View* found = child->findView(name)) {
				return found;
			}
		}
		return nullptr;
	}

	void View::paintChildren(BLContext& ctx) {
		// Children are skipped only when their whole drawn extent lies outside
		// the part of this view that can currently be seen (visibleRegion_,
		// below). For a full repaint that's the window; for a pruned one
		// (RepaintMode::Dirty, rootview.h) it's just the dirty region - which is
		// how dirty-rect pruning works here.
		//
		// An earlier attempt at pruning, done as a separate dirty-rect test on top
		// of a repaint that only blanked the dirty region's *background*, produced
		// visual corruption live (wrong colours, stale content on siblings) and
		// was reverted. The likeliest cause, now that it's understood: with the
		// whole tree redrawn on every repaint, a control that changed without
		// invalidating itself was quietly fixed by the next repaint anywhere, so
		// pruning exposed every such missing invalidation at once (SubView::
		// setBounds() was one - it invalidated nothing). Pruning is safe to use
		// now because it's built the other way round: blank the region, redraw
		// exactly what shows in it, clip everything to it - and NEWUI_VERIFY_REPAINT
		// (RootView::verifyPrunedRepaint()) renders a full frame after each pruned
		// repaint and reports any difference, so a missed invalidation is a report
		// naming the region and pixels, not a mystery.
		//
		// origin_ shifts all children uniformly (a scroll offset - see its
		// own doc comment, view.h) via one translate before the loop,
		// rather than per-child - the outer clip a parent already
		// established on *this* view before calling paintChildren() (the
		// ctx.clip_to_rect() below, one level up the call stack) stays in
		// effect through the translate, so scrolled content is still
		// correctly clipped to this view's own bounds without needing a
		// second, redundant clip here.
		// Where, in this view's content space (the space its children's bounds are in), anything can
		// currently be seen: the visible part of this view's own space, shifted by the scroll offset the
		// same way the children are.
		Rect visibleContent;
		if (hasVisibleRegion_) {
			visibleContent = Rect(visibleRegion_.pos() + origin_, visibleRegion_.size());
		}

		ctx.save();
		ctx.translate(-origin_.x, -origin_.y);
		for (SubView* child : childViews_) {
			if (!child->isVisible()) {
				continue;
			}

			// Snapped outward to whole pixels before translating/clipping -
			// a real, reproduced crash otherwise: a child positioned at a
			// generically-fractional coordinate (e.g. a Slider's thumb,
			// proportional to its current value) leaves ctx's clip with
			// fractional edges, which stops Blend2D's JIT from taking its
			// fast axis-aligned "box fill" pipeline - a themed child's own
			// buffered-paint blit_image() (ThemedViewStyle::paint(),
			// viewstyle.cpp) running under the resulting non-box path trips
			// a real BL_ASSERT(is_rect_fill()) in fetchpatternpart.cpp. See
			// Rect::snappedOutwardToPixels()'s own comment (geometry.h) -
			// same root-cause class rootview.cpp's own (now-shared)
			// snappedToPixels() already documented once, just never
			// applied here too until this crash surfaced it.
			Rect bounds = child->bounds().snappedOutwardToPixels();

			// Skip a child whose *whole* drawn extent lies outside what can be seen. Not just its bounds:
			// its focus ring and drop shadow are painted unclipped (phases 1 and 3 below), so the extent is
			// its bounds plus computePrePaintBounds()'s padding - the same extent redraw() invalidates.
			// Anything it draws lands outside the visible region, so skipping it changes no pixel. (This
			// culls on visibility only; dirty-rect pruning is a different, riskier thing - see above.)
			if (hasVisibleRegion_) {
				Rect extent;
				child->computePrePaintBounds(extent);
				extent.setPos(extent.pos() + bounds.pos());
				if (!extent.intersects(visibleContent)) {
					continue;
				}

				// What this child's own children can see: the part of its bounds that's visible, in its
				// own local space.
				Rect shown = bounds.intersected(visibleContent);
				shown.setPos(shown.pos() - bounds.pos());
				child->visibleRegion_ = shown;
				child->hasVisibleRegion_ = true;
			}
			else {
				child->hasVisibleRegion_ = false;  // unknown here, so unknown below - never a stale region
			}

			// Phase 1 (pre-paint) - translated but deliberately NOT
			// clipped, its own separate save()/restore() scope so an
			// effect that needs to extend outside bounds (a drop shadow,
			// ...) has real room to do so - see ViewStyle::prePaint()'s
			// own doc comment (viewstyle.h) for the full reasoning.
			ctx.save();
			ctx.restore_clipping();
			ctx.translate(bounds.left(), bounds.top());
			child->prePaintStyle(ctx);
			ctx.restore();

			// Phase 2 (regular paint) - translated AND clipped to this
			// child's own bounds, unchanged from before this 3-phase
			// split - the crash-prevention/cross-sibling-paint-corruption
			// clip below stays exactly as strict as it's always been.
			ctx.save();
			ctx.translate(bounds.left(), bounds.top());
			ctx.clip_to_rect(BLRect(0, 0, bounds.size().width, bounds.size().height));

			child->paintStyle(ctx);
			child->paint(ctx);
			child->paintChildren(ctx);

			ctx.restore();

			// Phase 3 (post-paint) - same unclipped shape as phase 1, on
			// the other side of the clipped phase 2 - see ViewStyle::
			// postPaint()'s own doc comment (viewstyle.h). This is what
			// makes the focus ring (postPaint()'s own default effect)
			// actually able to extend past this child's own bounds
			// instead of being clipped away by phase 2's clip above.
			ctx.save();
			ctx.restore_clipping();
			ctx.translate(bounds.left(), bounds.top());
			child->postPaintStyle(ctx);
			ctx.restore();
		}
		ctx.restore();
	}

	SubView* View::hitTestChildren(const Point& localPt, Point& outLocalPt) const {
		// Undoes paintChildren()'s -origin_ shift, so a point in this
		// view's own (unscrolled) local space maps onto its children's
		// bounds exactly the way they were actually drawn - see origin()'s
		// own doc comment (view.h).
		Point contentPt = localPt + origin_;
		for (auto it = childViews_.rbegin(); it != childViews_.rend(); ++it) {
			SubView* child = *it;
			if (!child->isVisible()) {
				continue;
			}

			const Rect& bounds = child->bounds();
			if (!bounds.contains(contentPt)) {
				continue;
			}

			Point childLocalPt(contentPt.x - bounds.left(), contentPt.y - bounds.top());

			Point deeperLocalPt;
			if (SubView* deeper = child->hitTestChildren(childLocalPt, deeperLocalPt)) {
				outLocalPt = deeperLocalPt;
				return deeper;
			}

			outLocalPt = childLocalPt;
			return child;
		}

		return nullptr;
	}

	bool View::isFocused() const {
		return rootView_ != nullptr && rootView_->focusedSubView() == dynamic_cast<const SubView*>(this);
	}

	void View::prePaintStyle(BLContext& ctx) {
		if (style_) {
			style_->prePaint(ctx, bounds_.size(), highlighted_);
		}
	}

	void View::paintStyle(BLContext& ctx) {
		if (style_) {
			Rect clientBounds;
			style_->paint(ctx, bounds_.size(), highlighted_, clientBounds);
		}
	}

	void View::postPaintStyle(BLContext& ctx) {
		if (style_) {
			// Recomputed, not carried over from paintStyle()'s own
			// clientBounds - that local went out of scope with the
			// clipped ctx save/restore scope paintStyle() ran inside
			// (see View::paintChildren(), view.cpp), and computeClientBounds()
			// is cheap/pure (no BLContext needed) precisely so recomputing
			// it here is the normal, expected way to get it back rather
			// than something this needs to avoid.
			Rect clientBounds = style_->computeClientBounds(bounds_.size());
			style_->postPaint(ctx, bounds_.size(), highlighted_, clientBounds);
		}
	}

	void View::computePrePaintBounds(Rect& outDirtyBounds) const
	{		
		newui::Rect r(0.0f, 0.0f, bounds_.size().width, bounds_.size().height);
		outDirtyBounds = r;
		if (style_) {
			style_->computePrePaintBounds(outDirtyBounds);
		}
	}

	void View::redraw()
	{
		if (nullptr != rootView_) {
			// The view's own full local bounds, not getClientBounds() -
			// that's deliberately deflated by style()'s border/3D-edge/
			// theme-content-rect chrome (ViewStyle::computeClientBounds()),
			// which is exactly the part a scoped repaint still needs to
			// cover. Invalidating only the client rect leaves that chrome
			// band's on-screen pixels stale (e.g. a themed control's edge
			// never gets its "unhover" repaint), visible as leftover
			// artifacts while hovering across bordered/themed controls.
			newui::Rect r(0.0f, 0.0f, bounds_.size().width, bounds_.size().height);
			computePrePaintBounds(r);
			rootView_->markDirty(this, r);
		}
	}

}
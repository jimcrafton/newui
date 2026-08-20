#include "newui/view.h"
#include "newui/subview.h"
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

	void View::addChild(SubView* child)
	{
		childViews_.push_back(child);
		
		updateLayout();
	}

	void View::removeChild(SubView* child) {
		childViews_.erase(std::remove(childViews_.begin(), childViews_.end(), child), childViews_.end());
		updateLayout();
	}

	void View::setLayout(std::unique_ptr<Layout> layout) {
		layout_ = std::move(layout);
		updateLayout();
	}

	void View::updateLayout() {
		if (layout_) {
			layout_->arrange(*this);
		}
	}

	void View::propagateRootView(RootView* root) {
		setRootView(root);
		for (SubView* child : childViews_) {
			child->propagateRootView(root);
		}
	}

	void View::paintChildren(BLContext& ctx) {
		// Deliberately no dirty-rect pruning here (tried and reverted -
		// see HANDOFF.md): skipping a child whose bounds don't intersect
		// the region being repainted looked correct on paper (the
		// translate/intersect math checks out) but produced real visual
		// corruption live - wrong colors and stale content on siblings
		// that should have been left untouched. Confirmed via a controlled
		// test: removing pruning while keeping everything else (including
		// RootView's own narrow top-level clip) fixed it immediately, so
		// every visible child is always walked unconditionally - the
		// original, safe behavior.
		//
		// origin_ shifts all children uniformly (a scroll offset - see its
		// own doc comment, view.h) via one translate before the loop,
		// rather than per-child - the outer clip a parent already
		// established on *this* view before calling paintChildren() (the
		// ctx.clip_to_rect() below, one level up the call stack) stays in
		// effect through the translate, so scrolled content is still
		// correctly clipped to this view's own bounds without needing a
		// second, redundant clip here.
		ctx.save();
		ctx.translate(-origin_.x, -origin_.y);
		for (SubView* child : childViews_) {
			if (!child->isVisible()) {
				continue;
			}

			const Rect& bounds = child->bounds();

			ctx.save();
			ctx.translate(bounds.left(), bounds.top());
			ctx.clip_to_rect(BLRect(0, 0, bounds.size().width, bounds.size().height));

			child->paintStyle(ctx);
			child->paint(ctx);
			child->paintChildren(ctx);

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

	void View::paintStyle(BLContext& ctx) {
		if (style_) {
			Rect unused;
			style_->paint(ctx, bounds_.size(), highlighted_, unused);
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
			rootView_->markDirty(this, r);
		}
	}

}
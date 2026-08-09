#include "newui/view.h"
#include "newui/subview.h"

namespace newui {
	bool View::initialize()
	{
		return true;
	}

	void View::destroy()
	{
		// Remove all children from this SubView
		for (SubView* child : childViews_) {
			child->destroy();
			delete child;
		}
		childViews_.clear();

		onDestroyed(*this);
	}

	void View::addChild(SubView* child)
	{
		childViews_.push_back(child);
	}

	void View::removeChild(SubView* child) {
		childViews_.erase(std::remove(childViews_.begin(), childViews_.end(), child), childViews_.end());
	}

	void View::propagateRootView(RootView* root) {
		setRootView(root);
		for (SubView* child : childViews_) {
			child->propagateRootView(root);
		}
	}

	void View::paintChildren(BLContext& ctx) {
		for (SubView* child : childViews_) {
			if (!child->isVisible()) {
				continue;
			}

			const Rect& bounds = child->getBounds();

			ctx.save();
			ctx.translate(bounds.left(), bounds.top());
			ctx.clip_to_rect(BLRect(0, 0, bounds.size().width, bounds.size().height));

			child->paintStyle(ctx);
			child->paint(ctx);
			child->paintChildren(ctx);

			ctx.restore();
		}
	}

	void View::paintStyle(BLContext& ctx) {
		clientBounds_ = Rect(0.0f, 0.0f, bounds_.size().width, bounds_.size().height);

		if (style_) {
			style_->paint(ctx, bounds_.size(), highlighted_, clientBounds_);
		} 
	}
}
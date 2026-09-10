#include "newui/subview.h"

namespace newui {

SubView::SubView() = default;

SubView::~SubView()
{
	
}


void SubView::setBounds(const Rect& bounds) {
    if (bounds == bounds_) {
        return;
    }

    bounds_ = bounds;
    onSizeChanged(*this, bounds_.size());
    updateLayout();
}

void SubView::setVisible(bool visible) {
    if (visible == visible_) {
        return;
    }

    visible_ = visible;
    onVisibilityChanged(*this);
}

void SubView::addChild(SubView* child) {
    // Raw parent_ bookkeeping via internal_setParent() (view.h) - not the real, safe
    // View::setParent(), which calls addChild()/removeChild() itself (to support an
    // already-attached child moving between containers); routing this through it would
    // recurse forever.
    child->internal_setParent(this);
	View::addChild(child);
	// propagateRootView(), not setRootView(): child may already have its
	// own subtree (built before being attached here), and every
	// descendant in it needs to pick up this rootView() too, not just
	// child itself.
	child->propagateRootView(rootView());
}

void SubView::removeChild(SubView* child) {
    // child's own rootView() (still valid - propagateRootView(nullptr)
    // below hasn't run yet) is whatever RootView owns this whole tree, if
    // any - it may be holding a raw hoveredSubView_/capturedSubView_/
    // focusedSubView_ pointer into child's subtree that's about to be
    // detached (see RootView::notifySubViewRemoved()'s doc comment for
    // why this matters - same cleanup RootView::removeChild() does for a
    // direct child, needed here too since a SubView removing one of its
    // own nested children never goes through RootView::removeChild() at
    // all).
    if (RootView* root = child->rootView()) {
        root->notifySubViewRemoved(child);
    }

    View::removeChild(child);
    child->internal_setParent(nullptr);  // see addChild()'s own comment on why not setParent()
    child->propagateRootView(nullptr);
}

bool SubView::initialize()
{
    return true;
}

void SubView::destroy()
{
	parentView_ = nullptr;

    // Remove this SubView from its parent's children list if it has a parent
    if (parent_) {
        parent_->removeChild(this);
    }
	parent_ = nullptr;

	View::destroy();
}




}
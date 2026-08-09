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
}

void SubView::setVisible(bool visible) {
    if (visible == visible_) {
        return;
    }

    visible_ = visible;
    onVisibilityChanged(*this);
}

void SubView::addChild(SubView* child) {
	View::addChild(child);
	child->parent_ = this;
}

void SubView::removeChild(SubView* child) {
    View::removeChild(child);
    child->parent_ = nullptr;
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
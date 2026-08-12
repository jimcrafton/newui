#pragma once

#include <newui/view.h>
#include <newui/rootview.h>

namespace newui {

// Heap-only, like View - see View's class comment. Construct with
// new SubView(...), not on the stack.
class SubView : public View {
public:
    SubView();
    virtual ~SubView();

	View* parent() const {
		return parent_;
	}

	void setParent(View* newParent) {
		parent_ = newParent;
	}

	void setBounds(const Rect& bounds);
	void setVisible(bool visible);

	LayoutParams* layoutParams() const {
		return layoutParams_.get();
	}

	// Attaches per-child metadata for whichever Layout this SubView's
	// parent uses to arrange it (e.g. std::make_unique<AnchorLayoutParams>
	// (Anchor::Left | Anchor::Right)) - see Layout, and the parent
	// View's setLayout()/updateLayout(). Does not itself trigger a
	// re-arrange; call parent()->updateLayout() (or the RootView's, if
	// this SubView is a direct child of one) afterward if the parent's
	// Layout has already run at least once and needs to pick up the
	// change immediately.
	void setLayoutParams(std::unique_ptr<LayoutParams> params) {
		layoutParams_ = std::move(params);
	}

	void setParentView(RootView* parentView) {
		parentView_ = parentView;
	}

	RootView* parentView() const {
		return parentView_;
	}

	virtual bool initialize();
	virtual void destroy();

	virtual void addChild(SubView* child);
	virtual void removeChild(SubView* child);

private:
    RootView* parentView_ = nullptr;

	View* parent_ = nullptr;

	std::unique_ptr<LayoutParams> layoutParams_;
};

}

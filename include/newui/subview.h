#pragma once

#include <newui/view.h>
#include <newui/rootview.h>
#include <deque>

namespace newui {

// Heap-only, like View - see View's class comment. Construct with
// new SubView(...), not on the stack.
// @reflect category=containers
class SubView : public View {
public:
    SubView();
    virtual ~SubView();

	

	void setBounds(const Rect& bounds) override;
	void setVisible(bool visible) override;

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

	// Non-owning upward back-reference to the owning RootView - reachable
	// downward already via that RootView's own childViews - same "would
	// recurse straight back into the tree ObjectReader/ObjectWriter are
	// already walking" reasoning View::rootView() (view.h) is ignore-
	// annotated for.
	//@reflect ignore=true
	RootView* parentView() const {
		return parentView_;
	}

	virtual bool initialize() override;
	virtual void destroy() override;

	virtual void addChild(SubView* child) override;
	virtual void removeChild(SubView* child) override;
private:
    RootView* parentView_ = nullptr;

	

	std::unique_ptr<LayoutParams> layoutParams_;
};

}

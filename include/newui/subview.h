#pragma once

#include <newui/view.h>
#include <newui/rootview.h>

namespace newui {

class SubView : public View {
public:
	
	

    SubView();
    virtual ~SubView(); 

	
	SubView* parent() const {
		return parent_;
	}

	void setBounds(const Rect& bounds);
	void setVisible(bool visible);

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

	SubView* parent_ = nullptr;
    
};

}

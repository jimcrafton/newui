#include "newui/viewstyle.h"
#include "newui/view.h"
#include "newui/rootview.h"


namespace newui {

	void ViewStyle::markDirty()
	{
		if (nullptr != view_) {
			view_->rootView()->markDirty();
		}
	}

}
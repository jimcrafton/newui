#include "newui/rootviewproxy.h"
#include "newui/uicolormanager.h"

namespace newui {

RootViewProxy::RootViewProxy() {
    style().setBackgroundColor(UIColorManager::colorFor(UIColorRole::WindowBackground));
}

}

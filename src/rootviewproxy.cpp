#include "newui/rootviewproxy.h"
#include "newui/uicolormanager.h"

namespace newui {

RootViewProxy::RootViewProxy() {
    setVisible(true);
    style().setBackgroundColor(UIColorManager::colorFor(UIColorRole::WindowBackground));
}

}

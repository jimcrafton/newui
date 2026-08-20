#include "newui/newui.h"
#include "newui/property.h"

namespace newui {

PropertyManager::~PropertyManager() {
    for (auto& entry : properties_) {
        delete entry.second;
    }
    properties_.clear();
}

PropertyBase* PropertyManager::getProperty(void* source, const std::string& name)  
{
    auto& pm = PropertyManager::instance();
    auto it = pm.properties_.find(Key{name, source});
    return it != pm.properties_.end() ? it->second : nullptr;
}

void PropertyManager::removeProperty(void* source, const std::string& name) {
    auto& pm = PropertyManager::instance();

    auto it = pm.properties_.find(Key{name, source});
    if (it != pm.properties_.end()) {
        delete it->second;
        pm.properties_.erase(it);
    }
}

void PropertyManager::clear() {
    auto& pm = PropertyManager::instance();
    for (auto& entry : pm.properties_) {
        delete entry.second;
    }
    pm.properties_.clear();
}

}

#include "newui/property.h"

namespace newui {

PropertyManager::~PropertyManager() {
    for (auto& entry : properties_) {
        delete entry.second;
    }
    properties_.clear();
}

PropertyBase* PropertyManager::getProperty(void* source, const std::string& name) const {
    auto it = properties_.find(Key{name, source});
    return it != properties_.end() ? it->second : nullptr;
}

void PropertyManager::removeProperty(void* source, const std::string& name) {
    auto it = properties_.find(Key{name, source});
    if (it != properties_.end()) {
        delete it->second;
        properties_.erase(it);
    }
}

void PropertyManager::clear() {
    for (auto& entry : properties_) {
        delete entry.second;
    }
    properties_.clear();
}

}

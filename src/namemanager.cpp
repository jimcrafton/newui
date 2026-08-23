#include "newui/namemanager.h"

namespace newui {

    std::string NameManager::generateName(const std::string& base) {
        unsigned int& next = nextCandidate_[base];
        if (next == 0) {
            next = 1;
        }
        for (;; ++next) {
            std::string candidate = base + std::to_string(next);
            if (takenNames_.find(candidate) == takenNames_.end()) {
                takenNames_.insert(candidate);
                ++next;
                return candidate;
            }
        }
    }

    bool NameManager::reserve(const std::string& name) {
        return takenNames_.insert(name).second;
    }

    bool NameManager::isTaken(const std::string& name) const {
        return takenNames_.find(name) != takenNames_.end();
    }

    void NameManager::release(const std::string& name) {
        takenNames_.erase(name);
    }

    void NameManager::clear() {
        takenNames_.clear();
        nextCandidate_.clear();
    }

}

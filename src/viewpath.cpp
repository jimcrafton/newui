#include "newui/viewpath.h"

#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/view.h"

#include <algorithm>
#include <sstream>
#include <vector>

namespace newui {

    std::string computeViewPath(RootView& root, const View* target) {
        if (target == nullptr) {
            return std::string();
        }
        if (target == static_cast<const View*>(&root)) {
            return std::string();
        }

        // Walks target->parent() up to root, recording each hop's own
        // index within its parent's childViews() - reversed into the
        // final path below, since this direction only ever knows "my
        // parent, and where I sit in it," not the other way around.
        std::vector<std::size_t> indices;
        const View* current = target;
        while (current != nullptr && current != static_cast<const View*>(&root)) {
            const View* parent = current->parent();
            if (parent == nullptr) {
                return std::string();
            }
            const std::vector<SubView*>& siblings = parent->childViews();
            auto it = std::find(siblings.begin(), siblings.end(), current);
            if (it == siblings.end()) {
                return std::string();
            }
            indices.push_back(static_cast<std::size_t>(it - siblings.begin()));
            current = parent;
        }
        if (current != static_cast<const View*>(&root)) {
            // Walked off the top (parent() chain ran out) without ever
            // reaching root - target isn't actually inside this tree.
            return std::string();
        }

        std::ostringstream path;
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            if (it != indices.rbegin()) {
                path << '/';
            }
            path << "childViews[" << *it << ']';
        }
        return path.str();
    }

    View* resolveViewPath(RootView& root, const std::string& path) {
        if (path.empty()) {
            return &root;
        }

        static const std::string kPrefix = "childViews[";

        View* current = &root;
        std::size_t pos = 0;
        while (pos < path.size()) {
            std::size_t segEnd = path.find('/', pos);
            std::string segment = path.substr(pos, segEnd == std::string::npos ? std::string::npos : segEnd - pos);

            if (segment.rfind(kPrefix, 0) != 0 || segment.empty() || segment.back() != ']') {
                return nullptr;
            }
            std::string indexStr = segment.substr(kPrefix.size(), segment.size() - kPrefix.size() - 1);

            std::size_t index = 0;
            try {
                std::size_t consumed = 0;
                index = static_cast<std::size_t>(std::stoul(indexStr, &consumed));
                if (consumed != indexStr.size()) {
                    return nullptr;
                }
            } catch (...) {
                return nullptr;
            }

            const std::vector<SubView*>& siblings = current->childViews();
            if (index >= siblings.size()) {
                return nullptr;
            }
            current = siblings[index];

            if (segEnd == std::string::npos) {
                break;
            }
            pos = segEnd + 1;
        }
        return current;
    }

}

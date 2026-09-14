#include "newui/uiinputmanager.h"
#include "newui/rootview.h"
#include "newui/subview.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

    // Recurses node and every visible descendant, collecting whichever
    // ones actually want keyboard focus - mirrors
    // RootView::notifySubViewRemoved()'s isWithinSubtree() walk shape
    // (rootview.cpp), just downward instead of up. Doesn't recurse into
    // an invisible node's children at all, rather than checking each
    // candidate's own ancestor chain for visibility individually - an
    // invisible container's children are never real tab stops regardless
    // of their own isVisible(), and pruning the walk here gets that for
    // free.
    void gatherFocusable(newui::SubView* node, std::vector<newui::SubView*>& out) {
        if (node == nullptr || !node->isVisible()) {
            return;
        }
        if (node->canBecomeFocused()) {
            out.push_back(node);
        }
        for (newui::SubView* child : node->childViews()) {
            gatherFocusable(child, out);
        }
    }

}

namespace newui {

    UIInputManager& UIInputManager::instance() {
        static UIInputManager mgr;
        return mgr;
    }

    SubView* UIInputManager::resolveClickFocusTarget(SubView* hitView) const {
        // Same parent()-walk-with-a-cast shape as
        // RootView::notifySubViewRemoved()'s isWithinSubtree() helper
        // (rootview.cpp) - naturally stops once the walk reaches the
        // RootView itself (View::parent() on a RootView is always
        // nullptr; dynamic_cast<SubView*> on it also fails, either way
        // ending the loop) without needing to special-case RootView here.
        for (SubView* v = hitView; v != nullptr; v = dynamic_cast<SubView*>(v->parent())) {
            if (v->canBecomeFocused()) {
                return v;
            }
        }
        return nullptr;
    }

    void UIInputManager::moveFocus(RootView& root, FocusNavigationDirection direction) const {
        std::vector<SubView*> candidates;
        for (SubView* child : root.childViews()) {
            gatherFocusable(child, candidates);
        }
        if (candidates.empty()) {
            return;
        }

        // Reading order: top-to-bottom, then left-to-right within a row -
        // same 5px tolerance a real toolkit's tab order needs to treat two
        // controls whose tops are nearly, but not exactly, level (a Label
        // and its paired TextField, say) as being on the same row rather
        // than sorting purely on a pixel-perfect y coordinate.
        std::sort(candidates.begin(), candidates.end(), [&root](SubView* a, SubView* b) {
            Point posA = root.accumulatedOffset(a);
            Point posB = root.accumulatedOffset(b);
            if (std::abs(posA.y - posB.y) > 5.0f) {
                return posA.y < posB.y;
            }
            return posA.x < posB.x;
        });

        SubView* current = root.focusedSubView();
        auto it = current != nullptr ? std::find(candidates.begin(), candidates.end(), current) : candidates.end();

        SubView* next = nullptr;
        if (it == candidates.end()) {
            next = (direction == FocusNavigationDirection::Previous) ? candidates.back() : candidates.front();
        } else {
            std::size_t idx = static_cast<std::size_t>(std::distance(candidates.begin(), it));
            if (direction == FocusNavigationDirection::Previous) {
                idx = (idx == 0) ? candidates.size() - 1 : idx - 1;
            } else {
                idx = (idx + 1) % candidates.size();
            }
            next = candidates[idx];
        }

        root.setFocusedSubView(next);
    }

}

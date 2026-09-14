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
    //
    // A focus scope (View::isFocusScope(), view.h) is never recursed
    // into - it's added as a single opaque stop instead (and only if it
    // can actually receive that stop's focus itself - see moveFocus()'s
    // own doc comment, uiinputmanager.h, for why a non-focusable scope is
    // skipped here rather than added as a dead stop). Its own descendants
    // only ever join a *different* candidate list - the one gathered once
    // something inside it is the active scope (findActiveScope() below).
    void gatherFocusableInScope(newui::SubView* node, std::vector<newui::SubView*>& out) {
        if (node == nullptr || !node->isVisible()) {
            return;
        }
        if (node->isFocusScope()) {
            if (node->canBecomeFocused()) {
                out.push_back(node);
            }
            return;
        }
        if (node->canBecomeFocused()) {
            out.push_back(node);
        }
        for (newui::SubView* child : node->childViews()) {
            gatherFocusableInScope(child, out);
        }
    }

    // Walks up from current (current itself included) looking for the
    // nearest View flagged as a focus scope - nullptr means "no scope
    // applies", i.e. the whole RootView tree is the (unscoped) candidate
    // pool, same as before focus scopes existed. Same parent()-walk-with-
    // a-cast shape as UIInputManager::resolveClickFocusTarget() - stops
    // naturally once it reaches the RootView itself.
    newui::SubView* findActiveScope(newui::SubView* current) {
        for (newui::SubView* runner = current; runner != nullptr; runner = dynamic_cast<newui::SubView*>(runner->parent())) {
            if (runner->isFocusScope()) {
                return runner;
            }
        }
        return nullptr;
    }

    // Follows a chain of FocusGuide::redirectTarget()s (uiinputmanager.h)
    // until it reaches a real, non-guide SubView - the "never actually
    // focus the guide itself" contract both UIInputManager::moveFocus()
    // and ::resolveClickFocusTarget() rely on this for. target itself may
    // already be a real (non-guide) SubView, in which case this returns
    // it unchanged immediately. Returns nullptr for a guide whose chain
    // never reaches a real SubView - an unset redirectTarget(), one
    // pointing at something that isn't even a SubView, or a cycle among
    // several guides (guarded via visited, so a misconfigured cycle can
    // never spin this loop forever) - callers treat that the same as "no
    // candidate to focus", a safe no-op rather than a crash or a stuck
    // guide.
    newui::SubView* resolveFocusRedirect(newui::SubView* target) {
        std::vector<newui::FocusGuide*> visited;
        while (target != nullptr) {
            auto* guide = dynamic_cast<newui::FocusGuide*>(target);
            if (guide == nullptr) {
                return target;
            }
            if (std::find(visited.begin(), visited.end(), guide) != visited.end()) {
                return nullptr;
            }
            visited.push_back(guide);
            target = dynamic_cast<newui::SubView*>(guide->redirectTarget());
        }
        return nullptr;
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
                return resolveFocusRedirect(v);
            }
        }
        return nullptr;
    }

    void UIInputManager::moveFocus(RootView& root, FocusNavigationDirection direction) const {
        SubView* current = root.focusedSubView();
        SubView* activeScope = (current != nullptr) ? findActiveScope(current) : nullptr;

        std::vector<SubView*> candidates;
        if (activeScope != nullptr) {
            for (SubView* child : activeScope->childViews()) {
                gatherFocusableInScope(child, candidates);
            }
        } else {
            for (SubView* child : root.childViews()) {
                gatherFocusableInScope(child, candidates);
            }
        }
        if (candidates.empty()) {
            return;
        }

        // Reading order: top-to-bottom, then left-to-right within a row -
        // same 5px tolerance a real toolkit's tab order needs to treat two
        // controls whose tops are nearly, but not exactly, level (a Label
        // and its paired TextField, say) as being on the same row rather
        // than sorting purely on a pixel-perfect y coordinate. stable_sort,
        // not sort - two candidates at the exact same position (a
        // FocusGuide deliberately placed exactly where a real View already
        // sits, say) need a deterministic relative order (gather order),
        // not whatever an unstable sort happens to produce.
        std::stable_sort(candidates.begin(), candidates.end(), [&root](SubView* a, SubView* b) {
            Point posA = root.accumulatedOffset(a);
            Point posB = root.accumulatedOffset(b);
            if (std::abs(posA.y - posB.y) > 5.0f) {
                return posA.y < posB.y;
            }
            return posA.x < posB.x;
        });

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

        next = resolveFocusRedirect(next);
        if (next == nullptr) {
            return;
        }

        root.setFocusedSubView(next);
    }

}

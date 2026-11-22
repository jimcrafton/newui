#include "newui/actioncontext.h"
#include "newui/action.h"

namespace newui {

    void ActionContextTree::setParent(const ActionContext& child, const ActionContext& parent) {
        if (child == ActionContext::global() || child == parent) {
            return;
        }
        parents_[child.hash()] = parent;
    }

    ActionContext ActionContextTree::parent(const ActionContext& context) const {
        auto it = parents_.find(context.hash());
        if (it == parents_.end()) {
            return ActionContext::global();
        }
        return it->second;
    }

    int ActionContextTree::depth(const ActionContext& context) const {
        int result = 0;
        ActionContext current = context;
        // The step cap stops a parent cycle from looping forever.
        while (current != ActionContext::global() && result < 64) {
            current = parent(current);
            ++result;
        }
        return result;
    }

    bool ActionContextTree::inherits(const ActionContext& context, const ActionContext& ancestor) const {
        ActionContext current = context;
        for (int step = 0; step < 64; ++step) {
            if (current == ancestor) {
                return true;
            }
            if (current == ActionContext::global()) {
                return false;
            }
            current = parent(current);
        }
        return false;
    }

    bool ActionContextTree::overlaps(const ActionContext& a, const ActionContext& b) const {
        return inherits(a, b) || inherits(b, a);
    }

    namespace {
        bool bindingActive(const ActionBinding& binding, const ActionContextTree& tree, const std::vector<ActionContext>& activeContexts) {
            if (binding.context == ActionContext::global()) {
                return true;
            }
            for (const ActionContext& activeContext : activeContexts) {
                if (tree.inherits(activeContext, binding.context)) {
                    return true;
                }
            }
            return false;
        }
    }

    Action* ActionMatcher::find(const std::vector<Action*>& actions,
                                std::uint32_t key,
                                std::uint32_t mask,
                                const ActionContextTree& tree,
                                const std::vector<ActionContext>& activeContexts) {
        return findChord(actions, key, mask, static_cast<std::uint32_t>(vkUndefined), kmUndefined, tree, activeContexts);
    }

    Action* ActionMatcher::findChord(const std::vector<Action*>& actions,
                                     std::uint32_t firstKey,
                                     std::uint32_t firstMask,
                                     std::uint32_t secondKey,
                                     std::uint32_t secondMask,
                                     const ActionContextTree& tree,
                                     const std::vector<ActionContext>& activeContexts) {
        Action* best = nullptr;
        int bestDepth = -1;
        for (Action* action : actions) {
            for (const ActionBinding& binding : action->bindings()) {
                if (static_cast<std::uint32_t>(binding.key) != firstKey || binding.mask != firstMask) {
                    continue;
                }
                if (static_cast<std::uint32_t>(binding.key2) != secondKey || (binding.isChord() && binding.mask2 != secondMask)) {
                    continue;
                }
                if (!bindingActive(binding, tree, activeContexts)) {
                    continue;
                }
                int depth = tree.depth(binding.context);
                if (depth > bestDepth) {
                    best = action;
                    bestDepth = depth;
                }
            }
        }
        return best;
    }

    bool ActionMatcher::startsChord(const std::vector<Action*>& actions,
                                    std::uint32_t key,
                                    std::uint32_t mask,
                                    const ActionContextTree& tree,
                                    const std::vector<ActionContext>& activeContexts) {
        for (Action* action : actions) {
            for (const ActionBinding& binding : action->bindings()) {
                if (binding.isChord() && static_cast<std::uint32_t>(binding.key) == key && binding.mask == mask &&
                    bindingActive(binding, tree, activeContexts)) {
                    return true;
                }
            }
        }
        return false;
    }

}

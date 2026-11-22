#pragma once

#include "newui/keyboard_constants.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace newui {

class Action;

// Name-based, open-ended id for "where a key binding applies" (canvas,
// textInput, ...), minted from a string like CommandId (command.h) with no
// central registration. Identity is the hash alone.
//@reflect ignore=true
class ActionContext {
public:
    ActionContext() : ActionContext("global") {}
    ActionContext(std::string name)
        : name_(std::move(name)), hash_(fnv1a(name_)) {
    }

    bool operator==(const ActionContext& other) const { return hash_ == other.hash_; }
    bool operator!=(const ActionContext& other) const { return hash_ != other.hash_; }

    std::uint64_t hash() const { return hash_; }
    const std::string& toString() const { return name_; }

    static const ActionContext& global() {
        static const ActionContext ctx("global");
        return ctx;
    }

private:
    static std::uint64_t fnv1a(const std::string& s) {
        std::uint64_t h = 14695981039346656037ull;
        for (unsigned char ch : s) {
            h ^= static_cast<std::uint64_t>(ch);
            h *= 1099511628211ull;
        }
        return h;
    }

    std::string name_;
    std::uint64_t hash_;
};

// One key an Action answers to, and where. A chord is two strokes in a row
// (Ctrl+K, then Ctrl+C): key2 is vkUndefined for an ordinary one-stroke binding.
//@reflect ignore=true
struct ActionBinding {
    ActionContext context;
    VirtualKeyCode key = vkUndefined;
    std::uint32_t mask = kmUndefined;
    VirtualKeyCode key2 = vkUndefined;
    std::uint32_t mask2 = kmUndefined;

    bool isChord() const { return key2 != vkUndefined; }

    bool operator==(const ActionBinding& other) const {
        return context == other.context && key == other.key && mask == other.mask && key2 == other.key2 && mask2 == other.mask2;
    }
};

// Parent links between contexts (trace -> canvas -> global). A context with
// no recorded parent sits directly under global; global has none.
//@reflect ignore=true
class ActionContextTree {
public:
    void setParent(const ActionContext& child, const ActionContext& parent);

    // Global's parent is global itself.
    ActionContext parent(const ActionContext& context) const;

    // global = 0, a direct child of global = 1, ...
    int depth(const ActionContext& context) const;

    // True when ancestor is context itself or one of its ancestors.
    bool inherits(const ActionContext& context, const ActionContext& ancestor) const;

    // Two contexts can be active at the same time exactly when one inherits
    // the other, which is when two bindings of the same key clash.
    bool overlaps(const ActionContext& a, const ActionContext& b) const;

private:
    std::unordered_map<std::uint64_t, ActionContext> parents_;
};

// The context-aware lookup RunLoop uses for a keystroke; separate from the
// message loop so it can be tested without one.
//@reflect ignore=true
class ActionMatcher {
public:
    // Among the actions with a binding for key/mask whose context is active
    // (equal to or an ancestor of one of activeContexts), returns the one
    // bound in the deepest context; the first registered wins a tie. An
    // empty activeContexts means just global. Enabled state is not checked.
    // Chord bindings are ignored here; see findChord.
    static Action* find(const std::vector<Action*>& actions,
                        std::uint32_t key,
                        std::uint32_t mask,
                        const ActionContextTree& tree,
                        const std::vector<ActionContext>& activeContexts);

    // The action whose active chord binding is first then second (deepest context wins, first registered on a tie).
    static Action* findChord(const std::vector<Action*>& actions,
                             std::uint32_t firstKey,
                             std::uint32_t firstMask,
                             std::uint32_t secondKey,
                             std::uint32_t secondMask,
                             const ActionContextTree& tree,
                             const std::vector<ActionContext>& activeContexts);

    // True when an active chord binding begins with this stroke, so the stroke should wait for the next one.
    static bool startsChord(const std::vector<Action*>& actions,
                            std::uint32_t key,
                            std::uint32_t mask,
                            const ActionContextTree& tree,
                            const std::vector<ActionContext>& activeContexts);
};

}

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace newui {

    // Generic "hand out names that don't collide" registry - not tied to
    // View/reflection or anything else, so any system that needs
    // auto-generated, guaranteed-unique-within-this-manager names (today:
    // RootView's per-tree default View names - "button1", "button2", ... -
    // see RootView::generateDefaultName()) can reuse the same bookkeeping
    // instead of re-deriving it. A NameManager instance defines one
    // independent namespace - two separate instances never know about each
    // other's names.
    class NameManager {
    public:
        // Returns base + the smallest positive integer N such that
        // (base + std::to_string(N)) isn't already taken (per isTaken()),
        // and marks that name taken before returning it. Amortized O(1):
        // each base's own "next candidate to try" counter only ever moves
        // forward (see release()'s own comment for why a released name's
        // exact slot is never reused).
        std::string generateName(const std::string& base);

        // Explicitly marks name taken - e.g. a caller-hand-assigned name
        // that didn't come from generateName() - so a later
        // generateName() call for the same base skips over it. Returns
        // false (no-op) if name is already taken.
        bool reserve(const std::string& name);

        bool isTaken(const std::string& name) const;

        // Frees name so isTaken()/generateName() no longer treat it as
        // taken. Deliberately does NOT rewind whichever base's counter
        // produced it - a released name's exact slot is never handed out
        // again by a later generateName() call for the same base. Matches
        // how real UI toolkits' default-object-naming already behaves
        // (names only ever count up), so two different objects that
        // existed at different times never end up confusingly sharing the
        // same auto-generated name.
        void release(const std::string& name);

        void clear();

    private:
        std::unordered_set<std::string> takenNames_;
        std::unordered_map<std::string, unsigned int> nextCandidate_;
    };

}

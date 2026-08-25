#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace newui {

    class Action;

    // Open-ended, name-based command identity - the C++ analogue of an
    // Objective-C selector (UIKit's copy:/paste:/delete:, resolved by
    // name rather than a fixed enum). Any code - core controls, an
    // app, a plugin - mints a new CommandId from its own string with
    // zero central registration and no risk of colliding with anyone
    // else's numeric choice, unlike a shared enum where two
    // independent extensions could both claim the same value. Owns its
    // name (rather than pointing at a caller-supplied buffer) so it
    // can safely be built from a deserialized string at load time
    // (Action::setCommandName(), action.h), not just a compile-time
    // literal like commands:: below. Identity is hash_ alone (64-bit
    // FNV-1a) - two different names colliding is astronomically
    // unlikely for any realistic number of commands, so equality never
    // falls back to comparing name_ itself.
    //@reflect ignore=true
    class CommandId {
    public:
        CommandId(std::string name)
            : name_(std::move(name)), hash_(fnv1a(name_)) {
        }

        bool operator==(const CommandId& other) const {
            return hash_ == other.hash_;
        }
        bool operator!=(const CommandId& other) const {
            return hash_ != other.hash_;
        }

        std::uint64_t hash() const {
            return hash_;
        }

        // The name this was built from - kept only for logging (e.g.
        // "unhandled command 'foo'"); hash_ is the real identity
        // everywhere else.
        const std::string& toString() const {
            return name_;
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

    // Names the framework's own controls (TextController's Cut/Copy/
    // Paste, an eventual Undo stack, ...) are expected to agree on -
    // not an exhaustive or closed set. An app defines its own with
    // CommandId("myapp.thing") right at the call site, no header edit
    // required here.
    namespace commands {
        inline const CommandId cut = CommandId("cut");
        inline const CommandId copy = CommandId("copy");
        inline const CommandId paste = CommandId("paste");
        inline const CommandId selectAll = CommandId("selectAll");
        inline const CommandId deleteSelection = CommandId("delete");
        inline const CommandId undo = CommandId("undo");
        inline const CommandId redo = CommandId("redo");
    }

    // Non-owning CommandId -> Action lookup, letting a View register
    // the same Actions it already hands to a Control/MenuItem
    // (Action::setAction()) so RootView's chain-walk performCommand()/
    // canPerformCommand() (rootview.h) can *also* reach them by name -
    // one Action, several possible entry points, all sharing the same
    // enabled()/hotkey() state automatically. Doesn't own what it
    // points at - same ownership model as Action itself (see its own
    // class comment): whoever constructs the Action (typically the
    // View registering it here, as a plain member) keeps owning it for
    // as long as this table references it.
    //
    // Deliberately never reflected - it's derived, reconstructible
    // state (same category as RootView's hoveredSubView_/
    // capturedSubView_/focusedSubView_), not owned data: the owning
    // View's constructor/initialize() re-populates it via add() every
    // time, independent of whatever a saved document did or didn't
    // contain. Method bodies live in command.cpp rather than inline
    // here purely so this header doesn't need Action's full definition
    // (action.h already needs CommandId, from above - defining
    // CommandTable's bodies inline here would make that circular).
    //@reflect ignore=true
    class CommandTable {
    public:
        // No-op if action.commandId() is empty - nothing to key it by.
        void add(Action& action);

        Action* find(const CommandId& id) const;

        // Looks up id and, if registered, fires its update() then
        // reports enabled() - false for an id nothing here has
        // registered at all.
        bool canPerform(const CommandId& id) const;

        // Looks up id and, if registered and currently enabled, calls
        // perform() - mirrors Action::perform()'s own "no-op while
        // disabled" behavior. Returns whether anything actually ran.
        bool perform(const CommandId& id);

    private:
        std::unordered_map<std::uint64_t, Action*> actions_;
    };

}

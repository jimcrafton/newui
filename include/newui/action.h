#pragma once

#include "newui/command.h"
#include "newui/delegate.h"
#include "newui/keyboard_constants.h"

#include <cstdint>
#include <optional>
#include <string>

namespace newui {

// A target-action style command: an independently enable/disable-able,
// optionally hotkey-bindable unit of behavior that a Control
// (controls.h) or MenuItem (menus.h) can point at instead of (or in
// addition to) wiring up its own onClick - so several UI elements (e.g.
// a toolbar button and a menu item) can share one underlying command.
// Not owned by whatever points at it - see Control::setAction()/
// MenuItem::setAction() - a plain value the application constructs and
// owns for as long as anything references it.
//
// Not a View/SubView - has no bounds, no drawing, no place in a View
// tree. Register it with a RunLoop (see RunLoop::registerAction()) to
// have its hotkey matched against incoming keystrokes.
class Action {
public:
    typedef Delegate<Action> PerformDelegate;
    typedef Delegate<Action, bool&> UpdateDelegate;

    Action() = default;
    explicit Action(std::string text) : text(std::move(text)) {}
    // An Action meant to be reachable via RootView's chain-walk
    // performCommand()/canPerformCommand() (rootview.h) registers
    // itself under this id - see CommandTable::add() (command.h).
    // Optional (see commandId() below) because most Actions - a plain
    // toolbar button's, say - are only ever pointed at directly and
    // don't need one.
    Action(CommandId id, std::string text) : commandId_(std::move(id)), text(std::move(text)) {}

    // Fired by perform() - the actual command behavior lives in a
    // listener here, not in this class.
    PerformDelegate onActionPerformed;

    // Fired by update() so a listener can reconsider whether this
    // Action currently makes sense (e.g. "Paste" while the clipboard is
    // empty) - enabled arrives pre-filled with this Action's current
    // enabled() and is written back afterward, whether or not a
    // listener touched it.
    UpdateDelegate onActionUpdated;

    std::string text;

    // Fires onActionPerformed(*this). Does nothing if !enabled().
    void perform() {
        if (!enabled_) {
            return;
        }
        onActionPerformed(*this);
    }

    // Fires onActionUpdated(*this, enabled), seeded with this Action's
    // current enabled(), and stores back whatever a listener left it as.
    void update() {
        bool enabled = enabled_;
        onActionUpdated(*this, enabled);
        enabled_ = enabled;
    }

    bool enabled() const { return enabled_; }
    void setEnabled(bool v) { enabled_ = v; }

    // vkUndefined (the default) means "no hotkey assigned" - see
    // matchesHotkey().
    VirtualKeyCode hotkey() const { return hotkey_; }
    void setHotkey(VirtualKeyCode key) { hotkey_ = key; }

    // A combination of KeyboardMasks (kmAlt|kmShift|kmCtrl) that must
    // accompany hotkey() for matchesHotkey() to match.
    std::uint32_t hotkeyMask() const { return hotkeyMask_; }
    void setHotkeyMask(std::uint32_t mask) { hotkeyMask_ = mask; }

    // Convenience for setting both at once.
    void setHotkey(VirtualKeyCode key, std::uint32_t mask) {
        hotkey_ = key;
        hotkeyMask_ = mask;
    }

    // True if VKeyCode/keyMask - as delivered by RunLoop::run()'s own
    // WM_KEYDOWN/WM_SYSKEYDOWN handling (see translateVirtualKey()/
    // translateKeyMask(), utils.h) - match this Action's hotkey()/
    // hotkeyMask() exactly. Always false while hotkey() is vkUndefined.
    bool matchesHotkey(std::uint32_t VKeyCode, std::uint32_t keyMask) const {
        return hotkey_ != vkUndefined
            && static_cast<std::uint32_t>(hotkey_) == VKeyCode
            && hotkeyMask_ == keyMask;
    }

    // Which command this Action answers, if any - see CommandTable
    // (command.h). Not reflected directly (CommandId itself is
    // @reflect ignore=true, and has no default constructor) -
    // commandName()/setCommandName() below is the reflected surface
    // for this same state.
    //@reflect ignore=true
    std::optional<CommandId> commandId() const { return commandId_; }
    void setCommandId(CommandId id) { commandId_ = std::move(id); }

    // Reflected string form of commandId() - "" means "no command id"
    // (matches commandId()'s std::nullopt). A saved document records
    // this string, not commandId_ directly, so an app-defined
    // CommandId("myapp.thing") round-trips through it exactly the same
    // as one of commands::'s own names.
    std::string commandName() const {
        return commandId_ ? commandId_->toString() : std::string();
    }
    void setCommandName(std::string name) {
        commandId_ = name.empty() ? std::optional<CommandId>()
                                   : std::optional<CommandId>(CommandId(std::move(name)));
    }

private:
    std::optional<CommandId> commandId_;
    bool enabled_ = true;
    VirtualKeyCode hotkey_ = vkUndefined;
    std::uint32_t hotkeyMask_ = kmUndefined;
};

}

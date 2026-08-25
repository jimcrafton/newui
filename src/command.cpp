#include "newui/command.h"
#include "newui/action.h"

namespace newui {

    void CommandTable::add(Action& action) {
        if (action.commandId()) {
            actions_[action.commandId()->hash()] = &action;
        }
    }

    Action* CommandTable::find(const CommandId& id) const {
        auto it = actions_.find(id.hash());
        return it != actions_.end() ? it->second : nullptr;
    }

    bool CommandTable::canPerform(const CommandId& id) const {
        Action* action = find(id);
        if (action == nullptr) {
            return false;
        }
        action->update();
        return action->enabled();
    }

    bool CommandTable::perform(const CommandId& id) {
        Action* action = find(id);
        if (action == nullptr) {
            return false;
        }
        action->update();
        if (!action->enabled()) {
            return false;
        }
        action->perform();
        return true;
    }

}

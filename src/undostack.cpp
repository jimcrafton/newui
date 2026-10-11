#include "newui/undostack.h"

namespace newui {

    void UndoStack::push(UndoableAction action) {
        if (action.doIt) {
            action.doIt();
        }
        redoStack_.clear();
        undoStack_.push_back(std::move(action));
        onActionPushed(*this, undoStack_.back());
    }

    void UndoStack::undo() {
        if (undoStack_.empty()) {
            return;
        }
        UndoableAction action = std::move(undoStack_.back());
        undoStack_.pop_back();
        if (action.undoIt) {
            action.undoIt();
        }
        redoStack_.push_back(std::move(action));
    }

    void UndoStack::redo() {
        if (redoStack_.empty()) {
            return;
        }
        UndoableAction action = std::move(redoStack_.back());
        redoStack_.pop_back();
        if (action.doIt) {
            action.doIt();
        }
        undoStack_.push_back(std::move(action));
    }

    void UndoStack::clear() {
        undoStack_.clear();
        redoStack_.clear();
    }

    const std::string& UndoStack::undoDescription() const {
        static const std::string empty;
        return undoStack_.empty() ? empty : undoStack_.back().description;
    }

    const std::string& UndoStack::redoDescription() const {
        static const std::string empty;
        return redoStack_.empty() ? empty : redoStack_.back().description;
    }

}

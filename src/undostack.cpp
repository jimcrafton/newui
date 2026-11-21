#include "newui/undostack.h"

#include <iterator>
#include <memory>

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

    void UndoStack::mergeSince(std::size_t mark, const std::string& description) {
        if (mark >= undoStack_.size()) {
            return;
        }
        if (undoStack_.size() - mark == 1) {
            undoStack_.back().description = description;
            return;
        }
        auto parts = std::make_shared<std::vector<UndoableAction>>(std::make_move_iterator(undoStack_.begin() + mark),
                                                                   std::make_move_iterator(undoStack_.end()));
        undoStack_.erase(undoStack_.begin() + mark, undoStack_.end());
        UndoableAction merged;
        merged.description = description;
        merged.doIt = [parts]() {
            for (UndoableAction& part : *parts) {
                if (part.doIt) {
                    part.doIt();
                }
            }
        };
        merged.undoIt = [parts]() {
            for (auto it = parts->rbegin(); it != parts->rend(); ++it) {
                if (it->undoIt) {
                    it->undoIt();
                }
            }
        };
        undoStack_.push_back(std::move(merged));
    }

    void UndoStack::rollbackTo(std::size_t mark) {
        if (mark >= undoStack_.size()) {
            return;
        }
        while (undoStack_.size() > mark) {
            undo();
        }
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

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <newui/delegate.h>

namespace newui {

    // One undo/redo step - doIt() performs it (also used for redo), undoIt()
    // reverses it. See UndoStack below.
    struct UndoableAction
    {
        std::string description;
        std::function<void()> doIt;
        std::function<void()> undoIt;
    };

    // Generic undo/redo stack - no VS/editor dependency, usable by any
    // newui app (including testharness.exe). See
    // bluesky/designer-plan.md (cpp_codetools) §4.4 for the VS-side
    // IOleUndoManager bridge this feeds (extension-side, not here).
    class UndoStack
    {
    public:
        typedef Delegate<UndoStack, const UndoableAction&> ActionPushedDelegate;

        // Calls action.doIt(), then records it. Discards any pending redo
        // history - same as any other undo/redo stack, pushing after an
        // undo invalidates the old future.
        void push(UndoableAction action);

        bool canUndo() const { return !undoStack_.empty(); }
        bool canRedo() const { return !redoStack_.empty(); }

        void undo();
        void redo();

        void clear();

        // description() of the action undo()/redo() would act on next, or
        // empty if canUndo()/canRedo() is false.
        const std::string& undoDescription() const;
        const std::string& redoDescription() const;

        // Fired by push(), right after the action is recorded - the
        // IOleUndoManager bridge (extension) listens here to build a
        // matching NativeUndoUnit. Not fired by undo()/redo() themselves.
        ActionPushedDelegate onActionPushed;

    private:
        std::vector<UndoableAction> undoStack_;
        std::vector<UndoableAction> redoStack_;
    };

}

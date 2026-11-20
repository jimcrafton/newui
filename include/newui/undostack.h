#pragma once

#include <functional>
#include <string>
#include <vector>

#include <newui/delegate.h>
#include <newui/geometry.h>

namespace newui {

    // One undo/redo step - doIt() performs it (also used for redo), undoIt()
    // reverses it. See UndoStack below.
    struct UndoableAction
    {
        std::string description;
        std::function<void()> doIt;
        std::function<void()> undoIt;

        // What visual region this action touches, in whatever coordinate
        // space the app that pushed it uses - UndoStack itself never reads
        // it, it just carries it to onActionApplied listeners (e.g. a
        // document turning it into a repaint/re-upload region). The same
        // region applies to doIt() and undoIt(): undoing changes the same
        // pixels redoing does. An empty Rect (the default) means "not
        // given" - a listener should treat that as "anything may have
        // changed", not "nothing changed".
        Rect affectedRegion;
    };

    // Generic undo/redo stack - no VS/editor dependency, usable by any
    // newui app (including testharness.exe). See
    // bluesky/designer-plan.md (cpp_codetools) §4.4 for the VS-side
    // IOleUndoManager bridge this feeds (extension-side, not here).
    class UndoStack
    {
    public:
        typedef Delegate<UndoStack, const UndoableAction&> ActionPushedDelegate;
        typedef Delegate<UndoStack, const UndoableAction&> ActionAppliedDelegate;

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

        // Fired every time an action was just applied or reverted, for any
        // reason: push() (after doIt() and onActionPushed), undo() (after
        // undoIt()) and redo() (after doIt()). This is the one to listen to
        // for "the document just changed" - onActionPushed misses undo and
        // redo. It fires once per push()/undo()/redo() call that acts (not
        // for undo()/redo() on an empty stack), including for an action
        // whose doIt/undoIt is empty. By then the stacks are already in
        // their final state, so canUndo()/canRedo()/the descriptions
        // reflect the result. The action reference is the stack's own copy:
        // a listener must not push(), undo(), redo() or clear() from inside
        // it (that could invalidate the reference).
        ActionAppliedDelegate onActionApplied;

    private:
        std::vector<UndoableAction> undoStack_;
        std::vector<UndoableAction> redoStack_;
    };

}

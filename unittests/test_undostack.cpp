#include "newui/undostack.h"

#include <gtest/gtest.h>

namespace {

// Delegate::Callback is a std::function, so a capturing lambda is fine here
// (unlike Delegate::FunctionPtr elsewhere in this codebase's tests).
newui::UndoableAction makeCounterAction(int& counter, std::string description) {
    newui::UndoableAction action;
    action.description = std::move(description);
    action.doIt = [&counter] { ++counter; };
    action.undoIt = [&counter] { --counter; };
    return action;
}

}  // namespace

TEST(UndoStack, PushPerformsDoItImmediately) {
    newui::UndoStack stack;
    int counter = 0;

    stack.push(makeCounterAction(counter, "increment"));

    EXPECT_EQ(counter, 1);
}

TEST(UndoStack, CanUndoCanRedoReflectStackState) {
    newui::UndoStack stack;
    int counter = 0;

    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());

    stack.push(makeCounterAction(counter, "increment"));
    EXPECT_TRUE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());

    stack.undo();
    EXPECT_FALSE(stack.canUndo());
    EXPECT_TRUE(stack.canRedo());
}

TEST(UndoStack, UndoCallsUndoItAndRedoCallsDoItAgain) {
    newui::UndoStack stack;
    int counter = 0;

    stack.push(makeCounterAction(counter, "increment"));
    EXPECT_EQ(counter, 1);

    stack.undo();
    EXPECT_EQ(counter, 0);

    stack.redo();
    EXPECT_EQ(counter, 1);
}

TEST(UndoStack, MultipleActionsUndoInReverseOrder) {
    newui::UndoStack stack;
    std::string log;

    newui::UndoableAction a;
    a.doIt = [&log] { log += "A"; };
    a.undoIt = [&log] { log += "a"; };
    newui::UndoableAction b;
    b.doIt = [&log] { log += "B"; };
    b.undoIt = [&log] { log += "b"; };

    stack.push(a);
    stack.push(b);
    EXPECT_EQ(log, "AB");

    stack.undo();
    stack.undo();
    EXPECT_EQ(log, "ABba");
}

TEST(UndoStack, PushingAfterUndoDiscardsTheRedoHistory) {
    newui::UndoStack stack;
    int counter = 0;

    stack.push(makeCounterAction(counter, "first"));
    stack.undo();
    EXPECT_TRUE(stack.canRedo());

    stack.push(makeCounterAction(counter, "second"));
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStack, UndoRedoOnAnEmptyStackIsANoOp) {
    newui::UndoStack stack;
    stack.undo();
    stack.redo();
    // No crash, nothing to assert beyond that.
}

TEST(UndoStack, DescriptionsReflectTheTopOfEachStack) {
    newui::UndoStack stack;
    int counter = 0;

    EXPECT_EQ(stack.undoDescription(), "");
    EXPECT_EQ(stack.redoDescription(), "");

    stack.push(makeCounterAction(counter, "increment"));
    EXPECT_EQ(stack.undoDescription(), "increment");
    EXPECT_EQ(stack.redoDescription(), "");

    stack.undo();
    EXPECT_EQ(stack.undoDescription(), "");
    EXPECT_EQ(stack.redoDescription(), "increment");
}

TEST(UndoStack, ClearDropsBothStacks) {
    newui::UndoStack stack;
    int counter = 0;

    stack.push(makeCounterAction(counter, "increment"));
    stack.undo();
    ASSERT_TRUE(stack.canRedo());

    stack.clear();
    EXPECT_FALSE(stack.canUndo());
    EXPECT_FALSE(stack.canRedo());
}

TEST(UndoStack, OnActionPushedFiresWithTheJustPushedAction) {
    newui::UndoStack stack;
    int counter = 0;
    std::string seenDescription;

    stack.onActionPushed.add([&seenDescription](newui::UndoStack&, const newui::UndoableAction& action) {
        seenDescription = action.description;
        return newui::SyncReturn::Ignored;
    });

    stack.push(makeCounterAction(counter, "increment"));
    EXPECT_EQ(seenDescription, "increment");
}

TEST(UndoStack, OnActionPushedDoesNotFireOnUndoOrRedo) {
    newui::UndoStack stack;
    int counter = 0;
    int pushCount = 0;

    stack.onActionPushed.add([&pushCount](newui::UndoStack&, const newui::UndoableAction&) {
        ++pushCount;
        return newui::SyncReturn::Ignored;
    });

    stack.push(makeCounterAction(counter, "increment"));
    stack.undo();
    stack.redo();

    EXPECT_EQ(pushCount, 1);
}

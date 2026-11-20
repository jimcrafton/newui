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

// ---- onActionApplied ------------------------------------------------------
// Fires after every push()/undo()/redo() that acted, unlike onActionPushed (push only).

TEST(UndoStack, OnActionAppliedFiresForPushUndoAndRedo) {
    newui::UndoStack stack;
    int counter = 0;
    int appliedCount = 0;

    stack.onActionApplied.add([&appliedCount](newui::UndoStack&, const newui::UndoableAction&) {
        ++appliedCount;
        return newui::SyncReturn::Ignored;
    });

    stack.push(makeCounterAction(counter, "increment"));
    EXPECT_EQ(appliedCount, 1);
    stack.undo();
    EXPECT_EQ(appliedCount, 2);
    stack.redo();
    EXPECT_EQ(appliedCount, 3);
}

TEST(UndoStack, OnActionAppliedDoesNotFireWhenUndoRedoHaveNothingToDo) {
    newui::UndoStack stack;
    int appliedCount = 0;

    stack.onActionApplied.add([&appliedCount](newui::UndoStack&, const newui::UndoableAction&) {
        ++appliedCount;
        return newui::SyncReturn::Ignored;
    });

    stack.undo();
    stack.redo();
    EXPECT_EQ(appliedCount, 0);
}

TEST(UndoStack, OnActionAppliedFiresAfterTheActionHasRun) {
    newui::UndoStack stack;
    int counter = 0;
    std::vector<int> counterAtNotification;

    stack.onActionApplied.add([&counter, &counterAtNotification](newui::UndoStack&, const newui::UndoableAction&) {
        counterAtNotification.push_back(counter);
        return newui::SyncReturn::Ignored;
    });

    stack.push(makeCounterAction(counter, "increment"));  // doIt ran: 1
    stack.undo();                                          // undoIt ran: 0
    stack.redo();                                          // doIt ran: 1

    EXPECT_EQ(counterAtNotification, (std::vector<int>{ 1, 0, 1 }));
}

TEST(UndoStack, OnActionAppliedFiresAfterOnActionPushedOnPush) {
    newui::UndoStack stack;
    int counter = 0;
    std::string order;

    stack.onActionPushed.add([&order](newui::UndoStack&, const newui::UndoableAction&) {
        order += "P";
        return newui::SyncReturn::Ignored;
    });
    stack.onActionApplied.add([&order](newui::UndoStack&, const newui::UndoableAction&) {
        order += "A";
        return newui::SyncReturn::Ignored;
    });

    stack.push(makeCounterAction(counter, "increment"));
    EXPECT_EQ(order, "PA");
    stack.undo();
    stack.redo();
    EXPECT_EQ(order, "PAAA");  // undo/redo: Applied only
}

TEST(UndoStack, OnActionAppliedCarriesTheSameActionAndAffectedRegion) {
    newui::UndoStack stack;
    int counter = 0;
    std::vector<std::string> descriptions;
    std::vector<newui::Rect> regions;

    stack.onActionApplied.add([&](newui::UndoStack&, const newui::UndoableAction& action) {
        descriptions.push_back(action.description);
        regions.push_back(action.affectedRegion);
        return newui::SyncReturn::Ignored;
    });

    newui::UndoableAction first = makeCounterAction(counter, "first");
    first.affectedRegion = newui::Rect(10, 20, 30, 40);
    newui::UndoableAction second = makeCounterAction(counter, "second");  // no region given
    stack.push(first);
    stack.push(second);
    stack.undo();  // reverts "second"
    stack.undo();  // reverts "first"
    stack.redo();  // re-applies "first"

    EXPECT_EQ(descriptions, (std::vector<std::string>{ "first", "second", "second", "first", "first" }));
    ASSERT_EQ(regions.size(), 5u);
    EXPECT_TRUE(regions[0] == newui::Rect(10, 20, 30, 40));
    EXPECT_TRUE(regions[3] == newui::Rect(10, 20, 30, 40));  // undo reports the same region as do
    EXPECT_TRUE(regions[4] == newui::Rect(10, 20, 30, 40));
    EXPECT_TRUE(regions[1] == newui::Rect());                // default: empty = "not given"
}

TEST(UndoStack, OnActionAppliedSeesTheStacksInTheirFinalState) {
    newui::UndoStack stack;
    int counter = 0;
    std::vector<std::string> states;  // "<canUndo><canRedo>" at each notification

    stack.onActionApplied.add([&states](newui::UndoStack& s, const newui::UndoableAction&) {
        states.push_back(std::string(s.canUndo() ? "U" : "-") + (s.canRedo() ? "R" : "-"));
        return newui::SyncReturn::Ignored;
    });

    stack.push(makeCounterAction(counter, "increment"));  // U-
    stack.undo();                                          // -R
    stack.redo();                                          // U-

    EXPECT_EQ(states, (std::vector<std::string>{ "U-", "-R", "U-" }));
}

TEST(UndoStack, OnActionAppliedFiresEvenIfTheActionHasNoCallbacks) {
    newui::UndoStack stack;
    int appliedCount = 0;

    stack.onActionApplied.add([&appliedCount](newui::UndoStack&, const newui::UndoableAction&) {
        ++appliedCount;
        return newui::SyncReturn::Ignored;
    });

    newui::UndoableAction empty;
    empty.description = "region only";
    stack.push(empty);
    stack.undo();
    stack.redo();

    EXPECT_EQ(appliedCount, 3);
}

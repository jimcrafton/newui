#include "newui/command.h"
#include "newui/action.h"

#include <gtest/gtest.h>

TEST(CommandId, EqualityComparesByNameNotByIdentity) {
    newui::CommandId a("copy");
    newui::CommandId b("copy");
    newui::CommandId c("paste");

    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

TEST(CommandId, ToStringReturnsTheNameItWasBuiltFrom) {
    newui::CommandId id("myapp.thing");
    EXPECT_EQ(id.toString(), "myapp.thing");
}

TEST(CommandId, WellKnownNamesMatchAFreshlyBuiltCommandIdWithTheSameName) {
    EXPECT_EQ(newui::commands::copy, newui::CommandId("copy"));
    EXPECT_EQ(newui::commands::paste, newui::CommandId("paste"));
    EXPECT_NE(newui::commands::copy, newui::commands::paste);
}

TEST(ActionCommandId, DefaultConstructedActionHasNoCommandId) {
    newui::Action action;
    EXPECT_FALSE(action.commandId().has_value());
    EXPECT_EQ(action.commandName(), "");
}

TEST(ActionCommandId, ConstructingWithACommandIdMakesItReflectedAsCommandName) {
    newui::Action action(newui::commands::copy, "Copy");
    ASSERT_TRUE(action.commandId().has_value());
    EXPECT_EQ(*action.commandId(), newui::commands::copy);
    EXPECT_EQ(action.commandName(), "copy");
}

TEST(ActionCommandId, SetCommandNameRoundTripsThroughCommandId) {
    newui::Action action;
    action.setCommandName("myapp.thing");
    ASSERT_TRUE(action.commandId().has_value());
    EXPECT_EQ(action.commandId()->toString(), "myapp.thing");
    EXPECT_EQ(action.commandName(), "myapp.thing");
}

TEST(ActionCommandId, SetCommandNameWithEmptyStringClearsCommandId) {
    newui::Action action(newui::commands::copy, "Copy");
    action.setCommandName("");
    EXPECT_FALSE(action.commandId().has_value());
    EXPECT_EQ(action.commandName(), "");
}

TEST(CommandTable, CanPerformIsFalseForAnUnregisteredCommand) {
    newui::CommandTable table;
    EXPECT_FALSE(table.canPerform(newui::commands::copy));
}

TEST(CommandTable, AddWithNoCommandIdIsANoOp) {
    newui::Action action("Copy");  // no CommandId
    newui::CommandTable table;
    table.add(action);

    EXPECT_EQ(table.find(newui::commands::copy), nullptr);
}

TEST(CommandTable, FindReturnsTheRegisteredActionByHash) {
    newui::Action action(newui::commands::copy, "Copy");
    newui::CommandTable table;
    table.add(action);

    EXPECT_EQ(table.find(newui::commands::copy), &action);
    EXPECT_EQ(table.find(newui::commands::paste), nullptr);
}

TEST(CommandTable, CanPerformReflectsTheActionsEnabledStateAfterUpdate) {
    newui::Action action(newui::commands::copy, "Copy");
    bool hasSelection = false;
    action.onActionUpdated.add([&hasSelection](newui::Action&, bool& enabled) {
        enabled = hasSelection;
        return newui::SyncReturn::Handled;
    });

    newui::CommandTable table;
    table.add(action);

    EXPECT_FALSE(table.canPerform(newui::commands::copy));

    hasSelection = true;
    EXPECT_TRUE(table.canPerform(newui::commands::copy));
}

TEST(CommandTable, PerformInvokesTheActionAndReturnsTrueWhenEnabled) {
    newui::Action action(newui::commands::copy, "Copy");
    int performCount = 0;
    action.onActionUpdated.add([](newui::Action&, bool& enabled) {
        enabled = true;
        return newui::SyncReturn::Handled;
    });
    action.onActionPerformed.add([&performCount](newui::Action&) {
        ++performCount;
        return newui::SyncReturn::Handled;
    });

    newui::CommandTable table;
    table.add(action);

    EXPECT_TRUE(table.perform(newui::commands::copy));
    EXPECT_EQ(performCount, 1);
}

TEST(CommandTable, PerformDoesNothingAndReturnsFalseWhenTheActionIsDisabled) {
    newui::Action action(newui::commands::copy, "Copy");
    int performCount = 0;
    action.onActionUpdated.add([](newui::Action&, bool& enabled) {
        enabled = false;
        return newui::SyncReturn::Handled;
    });
    action.onActionPerformed.add([&performCount](newui::Action&) {
        ++performCount;
        return newui::SyncReturn::Handled;
    });

    newui::CommandTable table;
    table.add(action);

    EXPECT_FALSE(table.perform(newui::commands::copy));
    EXPECT_EQ(performCount, 0);
}

TEST(CommandTable, PerformReturnsFalseForAnUnregisteredCommand) {
    newui::CommandTable table;
    EXPECT_FALSE(table.perform(newui::commands::copy));
}

#include "newui/namemanager.h"

#include <gtest/gtest.h>

TEST(NameManagerGenerateName, FirstCallForABaseReturnsBase1) {
    newui::NameManager mgr;

    EXPECT_EQ(mgr.generateName("button"), "button1");
}

TEST(NameManagerGenerateName, SecondCallForTheSameBaseReturnsBase2) {
    newui::NameManager mgr;

    mgr.generateName("button");

    EXPECT_EQ(mgr.generateName("button"), "button2");
}

TEST(NameManagerGenerateName, DifferentBasesCountIndependently) {
    newui::NameManager mgr;

    EXPECT_EQ(mgr.generateName("button"), "button1");
    EXPECT_EQ(mgr.generateName("slider"), "slider1");
    EXPECT_EQ(mgr.generateName("button"), "button2");
}

TEST(NameManagerGenerateName, SkipsOverAnAlreadyReservedName) {
    newui::NameManager mgr;

    ASSERT_TRUE(mgr.reserve("button1"));

    EXPECT_EQ(mgr.generateName("button"), "button2");
}

TEST(NameManagerReserve, ReturnsFalseWhenAlreadyTaken) {
    newui::NameManager mgr;

    ASSERT_TRUE(mgr.reserve("thing"));

    EXPECT_FALSE(mgr.reserve("thing"));
}

TEST(NameManagerIsTaken, ReflectsGenerateNameAndReserve) {
    newui::NameManager mgr;

    EXPECT_FALSE(mgr.isTaken("button1"));

    mgr.generateName("button");
    EXPECT_TRUE(mgr.isTaken("button1"));

    mgr.reserve("thing");
    EXPECT_TRUE(mgr.isTaken("thing"));
}

TEST(NameManagerRelease, FreesTheNameForIsTakenButNotForReuseByGenerateName) {
    newui::NameManager mgr;
    std::string first = mgr.generateName("button");
    ASSERT_EQ(first, "button1");

    mgr.release(first);

    EXPECT_FALSE(mgr.isTaken("button1"));
    // Doesn't rewind the counter - see NameManager::release()'s own doc
    // comment for why.
    EXPECT_EQ(mgr.generateName("button"), "button2");
}

TEST(NameManagerClear, ResetsBothTakenNamesAndCounters) {
    newui::NameManager mgr;
    mgr.generateName("button");
    mgr.reserve("thing");

    mgr.clear();

    EXPECT_FALSE(mgr.isTaken("button1"));
    EXPECT_FALSE(mgr.isTaken("thing"));
    EXPECT_EQ(mgr.generateName("button"), "button1");
}

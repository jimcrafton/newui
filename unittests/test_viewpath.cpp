// newui/newui.h must be the first include in this file - it defines
// NOMINMAX before anything pulls in <windows.h> for the first time. See
// test_shapes.cpp's own copy of this comment / feedback_no_std_minmax
// (memory) / utils.h's own doc comment for why.
#include "newui/newui.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/viewpath.h"

#include <gtest/gtest.h>

using namespace newui;

TEST(ViewPath, RootItselfHasAnEmptyPath) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    EXPECT_EQ(computeViewPath(root, &root), "");
}

TEST(ViewPath, DirectChildPathIsIndexZero) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    SubView* child = new SubView();
    root.addChild(child);

    EXPECT_EQ(computeViewPath(root, child), "childViews[0]");
}

TEST(ViewPath, SecondChildGetsIndexOne) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    SubView* first = new SubView();
    SubView* second = new SubView();
    root.addChild(first);
    root.addChild(second);

    EXPECT_EQ(computeViewPath(root, second), "childViews[1]");
}

TEST(ViewPath, GrandchildChainsBothIndices) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    SubView* parent = new SubView();
    root.addChild(parent);
    SubView* child = new SubView();
    parent->addChild(child);

    EXPECT_EQ(computeViewPath(root, child), "childViews[0]/childViews[0]");
}

TEST(ViewPath, TargetNotInTreeReturnsEmpty) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    SubView orphan;  // never added to root

    EXPECT_EQ(computeViewPath(root, &orphan), "");
}

TEST(ViewPath, NullTargetReturnsEmpty) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    EXPECT_EQ(computeViewPath(root, nullptr), "");
}

TEST(ViewPath, ResolveEmptyPathReturnsRoot) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    EXPECT_EQ(resolveViewPath(root, ""), &root);
}

TEST(ViewPath, ResolveSingleHopFindsTheRightChild) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    SubView* first = new SubView();
    SubView* second = new SubView();
    root.addChild(first);
    root.addChild(second);

    EXPECT_EQ(resolveViewPath(root, "childViews[1]"), second);
}

TEST(ViewPath, ResolveTwoHopsWalksIntoTheGrandchild) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    SubView* parent = new SubView();
    root.addChild(parent);
    SubView* child = new SubView();
    parent->addChild(child);

    EXPECT_EQ(resolveViewPath(root, "childViews[0]/childViews[0]"), child);
}

TEST(ViewPath, ResolveOutOfRangeIndexReturnsNull) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    root.addChild(new SubView());

    EXPECT_EQ(resolveViewPath(root, "childViews[5]"), nullptr);
}

TEST(ViewPath, ResolveMalformedSegmentReturnsNull) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    EXPECT_EQ(resolveViewPath(root, "notAPath"), nullptr);
}

TEST(ViewPath, ComputeThenResolveRoundTrips) {
    RootView root(nullptr, Rect(0, 0, 100, 100), "root");
    SubView* parent = new SubView();
    root.addChild(parent);
    SubView* childA = new SubView();
    SubView* childB = new SubView();
    parent->addChild(childA);
    parent->addChild(childB);

    std::string path = computeViewPath(root, childB);
    EXPECT_EQ(resolveViewPath(root, path), childB);
}

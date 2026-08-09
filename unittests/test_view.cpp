#include "newui/subview.h"

#include <gtest/gtest.h>

namespace {

// propagateRootView()/addChild()/removeChild() only ever store or compare
// this pointer - never dereference it - so a sentinel value stands in for
// a real (Win32-backed) RootView, which these tests don't need.
newui::RootView* SentinelRoot() {
    return reinterpret_cast<newui::RootView*>(0x1);
}

}  // namespace

TEST(ViewPropagateRootView, SetsItOnASingleView) {
    newui::SubView view;

    view.propagateRootView(SentinelRoot());

    EXPECT_EQ(view.rootView(), SentinelRoot());
}

TEST(ViewPropagateRootView, NullClearsIt) {
    newui::SubView view;
    view.propagateRootView(SentinelRoot());

    view.propagateRootView(nullptr);

    EXPECT_EQ(view.rootView(), nullptr);
}

TEST(SubViewAddChild, PropagatesRootViewToNewChild) {
    newui::SubView parent;
    newui::SubView child;
    parent.propagateRootView(SentinelRoot());

    parent.addChild(&child);

    EXPECT_EQ(child.rootView(), SentinelRoot());
}

TEST(SubViewAddChild, PropagatesRootViewToPreexistingGrandchildren) {
    // Build a subtree (grandchild under child) BEFORE either has a
    // RootView - the gap this guards against: attaching just the
    // immediate child to a rooted parent used to leave already-existing
    // descendants (grandchild here) with a stale/null rootView().
    newui::SubView grandchild;
    newui::SubView child;
    child.addChild(&grandchild);

    EXPECT_EQ(grandchild.rootView(), nullptr);  // nothing rooted yet

    newui::SubView parent;
    parent.propagateRootView(SentinelRoot());

    parent.addChild(&child);

    EXPECT_EQ(child.rootView(), SentinelRoot());
    EXPECT_EQ(grandchild.rootView(), SentinelRoot());  // propagated through
}

TEST(SubViewRemoveChild, PropagatesNullToWholeDetachedSubtree) {
    newui::SubView grandchild;
    newui::SubView child;
    child.addChild(&grandchild);

    newui::SubView parent;
    parent.propagateRootView(SentinelRoot());
    parent.addChild(&child);
    ASSERT_EQ(grandchild.rootView(), SentinelRoot());

    parent.removeChild(&child);

    EXPECT_EQ(child.rootView(), nullptr);
    EXPECT_EQ(grandchild.rootView(), nullptr);
}

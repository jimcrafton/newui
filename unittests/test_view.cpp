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
    auto* view = new newui::SubView();

    view->propagateRootView(SentinelRoot());

    EXPECT_EQ(view->rootView(), SentinelRoot());

    delete view;
}

TEST(ViewPropagateRootView, NullClearsIt) {
    auto* view = new newui::SubView();
    view->propagateRootView(SentinelRoot());

    view->propagateRootView(nullptr);

    EXPECT_EQ(view->rootView(), nullptr);

    delete view;
}

TEST(SubViewAddChild, PropagatesRootViewToNewChild) {
    auto* parent = new newui::SubView();
    auto* child = new newui::SubView();
    parent->propagateRootView(SentinelRoot());

    parent->addChild(child);

    EXPECT_EQ(child->rootView(), SentinelRoot());

    delete child;
    delete parent;
}

TEST(SubViewAddChild, PropagatesRootViewToPreexistingGrandchildren) {
    // Build a subtree (grandchild under child) BEFORE either has a
    // RootView - the gap this guards against: attaching just the
    // immediate child to a rooted parent used to leave already-existing
    // descendants (grandchild here) with a stale/null rootView().
    auto* grandchild = new newui::SubView();
    auto* child = new newui::SubView();
    child->addChild(grandchild);

    EXPECT_EQ(grandchild->rootView(), nullptr);  // nothing rooted yet

    auto* parent = new newui::SubView();
    parent->propagateRootView(SentinelRoot());

    parent->addChild(child);

    EXPECT_EQ(child->rootView(), SentinelRoot());
    EXPECT_EQ(grandchild->rootView(), SentinelRoot());  // propagated through

    delete grandchild;
    delete child;
    delete parent;
}

TEST(SubViewRemoveChild, PropagatesNullToWholeDetachedSubtree) {
    auto* grandchild = new newui::SubView();
    auto* child = new newui::SubView();
    child->addChild(grandchild);

    auto* parent = new newui::SubView();
    parent->propagateRootView(SentinelRoot());
    parent->addChild(child);
    ASSERT_EQ(grandchild->rootView(), SentinelRoot());

    parent->removeChild(child);

    EXPECT_EQ(child->rootView(), nullptr);
    EXPECT_EQ(grandchild->rootView(), nullptr);

    delete grandchild;
    delete child;
    delete parent;
}

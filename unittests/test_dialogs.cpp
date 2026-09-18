#include "newui/dialogs.h"
#include "newui/subview.h"

#include <gtest/gtest.h>

// Dialog composes a Frame (see dialogs.h's class comment). A Dialog that
// is never shown - like a Frame that is never initialize()'d - is safe to
// destroy normally (see Frame::~Frame() in frame.cpp): it only throws
// over a *live* window whose rootView_ was never torn down through
// WM_DESTROY, not over one that was simply never created. None of the
// tests below ever call show()/showModal() successfully (the only paths
// that create a live window - and showModal()'s blocking native message
// loop, plus every native common/message-box dialog in dialogs.h, isn't
// unit-testable at all: same reasoning ContextMenu::show() gets in
// test_menus.cpp), so every Dialog here is a plain stack value.

TEST(Dialog, TitleAndBoundsForwardToUnderlyingFrame) {
    newui::Dialog dialog;
    dialog.setTitle("Preferences");
    dialog.setBounds(newui::Rect(10, 20, 400, 300));

    EXPECT_EQ(dialog.getTitle(), "Preferences");
    EXPECT_EQ(dialog.getBounds(), newui::Rect(10, 20, 400, 300));
}

TEST(Dialog, StartsUnclosedWithNoResult) {
    newui::Dialog dialog;

    EXPECT_FALSE(dialog.isClosed());
    EXPECT_EQ(dialog.result(), newui::DialogResult::None);
    EXPECT_EQ(dialog.dialogHandle(), nullptr);
}

TEST(Dialog, CloseBeforeShowFinalizesImmediately) {
    newui::Dialog dialog;

    int closedCount = 0;
    newui::DialogResult seenResult = newui::DialogResult::None;
    dialog.onClosed.add([&](newui::Dialog& d) {
        ++closedCount;
        seenResult = d.result();
        return newui::SyncReturn::Handled;
    });

    dialog.close(newui::DialogResult::Ok);

    EXPECT_TRUE(dialog.isClosed());
    EXPECT_EQ(dialog.result(), newui::DialogResult::Ok);
    EXPECT_EQ(closedCount, 1);
    EXPECT_EQ(seenResult, newui::DialogResult::Ok);
}

TEST(Dialog, SecondCloseCallIsIgnored) {
    newui::Dialog dialog;

    dialog.close(newui::DialogResult::Yes);
    dialog.close(newui::DialogResult::No);  // ignored - first close() already won

    EXPECT_EQ(dialog.result(), newui::DialogResult::Yes);
}

TEST(Dialog, ShowModalReturnsCancelWithoutBlockingOnceClosed) {
    // showModal() bails out (Cancel, no window, no message loop) up front
    // whenever ensureInitialized() fails - exercised here via the
    // "already closed" case, so this needs no live Application/HWND
    // (initialize() itself needs Application::instance().instanceHandle(),
    // which none of these tests set up).
    newui::Dialog dialog;
    dialog.close();

    EXPECT_EQ(dialog.showModal(), newui::DialogResult::Cancel);
}

// Regression: closing a Dialog used to free its whole child SubView tree synchronously inside
// WM_DESTROY - before showModal() returned - so any read of a child widget's state afterward
// (the natural way to fetch a result) was a use-after-free. Needs a real (never-pumped) native
// window; WM_CLOSE is sent, not posted, so the whole close path runs synchronously.
TEST(Dialog, ChildTreeSurvivesWindowDestruction) {
    newui::Dialog dialog;
    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(1, 2, 30, 40));
    dialog.rootView().addChild(child);

    ASSERT_TRUE(dialog.show());
    HWND hwnd = dialog.dialogHandle();
    ASSERT_NE(hwnd, nullptr);

    ::SendMessage(hwnd, WM_CLOSE, 0, 0);

    EXPECT_TRUE(dialog.isClosed());
    EXPECT_EQ(dialog.dialogHandle(), nullptr);
    EXPECT_EQ(dialog.rootView().windowHandle(), nullptr);
    ASSERT_EQ(dialog.rootView().childViews().size(), 1u);
    EXPECT_EQ(dialog.rootView().childViews()[0], child);
    EXPECT_FLOAT_EQ(child->bounds().size().width, 30.0f);  // still live, readable memory
}

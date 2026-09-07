#include "newui/segmentedcontrol.h"

#include <gtest/gtest.h>

TEST(SegmentedControl, StartsWithNoSegmentsAndSelectedIndexZero) {
    auto* control = new newui::SegmentedControl();
    EXPECT_TRUE(control->segments().empty());
    EXPECT_EQ(control->selectedIndex(), 0u);
    delete control;
}

TEST(SegmentedControl, SetSegmentsResetsSelectionAndEnabledState) {
    auto* control = new newui::SegmentedControl();
    control->setSelectedIndex(0);  // no-op, nothing set yet

    control->setSegments({"Design", "Source", "Data Flow"});
    EXPECT_EQ(control->segments().size(), 3u);
    EXPECT_EQ(control->selectedIndex(), 0u);
    EXPECT_TRUE(control->isSegmentEnabled(0));
    EXPECT_TRUE(control->isSegmentEnabled(1));
    EXPECT_TRUE(control->isSegmentEnabled(2));
    delete control;
}

TEST(SegmentedControl, SetSelectedIndexChangesSelectionAndFiresOnSelectionChanged) {
    auto* control = new newui::SegmentedControl();
    control->setSegments({"Design", "Source"});

    int changedCount = 0;
    control->onSelectionChanged.add([&changedCount](newui::SegmentedControl&) {
        ++changedCount;
        return newui::SyncReturn::Handled;
    });

    control->setSelectedIndex(1);
    EXPECT_EQ(control->selectedIndex(), 1u);
    EXPECT_EQ(changedCount, 1);
    delete control;
}

TEST(SegmentedControl, SetSelectedIndexToAlreadySelectedIsANoOp) {
    auto* control = new newui::SegmentedControl();
    control->setSegments({"Design", "Source"});

    int changedCount = 0;
    control->onSelectionChanged.add([&changedCount](newui::SegmentedControl&) {
        ++changedCount;
        return newui::SyncReturn::Handled;
    });

    control->setSelectedIndex(0);  // already selected
    EXPECT_EQ(changedCount, 0);
    delete control;
}

TEST(SegmentedControl, DisabledSegmentCanNeverBecomeSelected) {
    auto* control = new newui::SegmentedControl();
    control->setSegments({"Design", "Source"});
    control->setSegmentEnabled(1, false);

    int changedCount = 0;
    control->onSelectionChanged.add([&changedCount](newui::SegmentedControl&) {
        ++changedCount;
        return newui::SyncReturn::Handled;
    });

    control->setSelectedIndex(1);
    EXPECT_EQ(control->selectedIndex(), 0u);
    EXPECT_EQ(changedCount, 0);
    delete control;
}

TEST(SegmentedControl, SetSelectedIndexOutOfRangeIsANoOp) {
    auto* control = new newui::SegmentedControl();
    control->setSegments({"Design", "Source"});

    control->setSelectedIndex(5);
    EXPECT_EQ(control->selectedIndex(), 0u);
    delete control;
}

TEST(SegmentedControl, NaturalSizeGrowsWithMoreOrLongerSegments) {
    auto* shortControl = new newui::SegmentedControl();
    shortControl->setSegments({"A"});
    newui::Size shortSize = shortControl->naturalSize();

    auto* longControl = new newui::SegmentedControl();
    longControl->setSegments({"Design", "Source", "Data Flow"});
    newui::Size longSize = longControl->naturalSize();

    EXPECT_GT(longSize.width, shortSize.width);
    EXPECT_GT(shortSize.width, 0.0f);
    EXPECT_GT(shortSize.height, 0.0f);

    delete shortControl;
    delete longControl;
}

// A real click - not a synthetic keyboard/focus simulation, this is the
// same "fire the real onMouseDown Delegate with a real Point" convention
// Splitter's own drag tests already use (test_splitter.cpp).
TEST(SegmentedControl, ClickingASegmentSelectsIt) {
    auto* control = new newui::SegmentedControl();
    control->setSegments({"Design", "Source", "Data Flow"});
    control->setBounds(newui::Rect(0, 0, control->naturalSize().width, control->naturalSize().height));

    // Click well inside the first segment first, to confirm index 0 too.
    control->onMouseDown(*control, newui::Point(2.0f, 2.0f), 1, 0);
    EXPECT_EQ(control->selectedIndex(), 0u);

    // Click past the first segment's own width (right edge of the whole
    // control, guaranteed to land in the last segment) to select it.
    control->onMouseDown(*control, newui::Point(control->naturalSize().width - 2.0f, 2.0f), 1, 0);
    EXPECT_EQ(control->selectedIndex(), 2u);

    delete control;
}

TEST(SegmentedControl, ClickingADisabledSegmentDoesNothing) {
    auto* control = new newui::SegmentedControl();
    control->setSegments({"Design", "Source"});
    control->setSegmentEnabled(1, false);
    control->setBounds(newui::Rect(0, 0, control->naturalSize().width, control->naturalSize().height));

    control->onMouseDown(*control, newui::Point(control->naturalSize().width - 2.0f, 2.0f), 1, 0);
    EXPECT_EQ(control->selectedIndex(), 0u);

    delete control;
}

TEST(SegmentedControl, PaintDoesNotCrashWithNoSegments) {
    auto* control = new newui::SegmentedControl();
    control->setBounds(newui::Rect(0, 0, 100, 24));

    BLImage image(100, 24, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    control->paint(ctx);
    ctx.end();

    delete control;
}

TEST(SegmentedControl, PaintDoesNotCrashWithRealSegments) {
    auto* control = new newui::SegmentedControl();
    control->setSegments({"Design", "Source", "Data Flow"});
    control->setSegmentEnabled(2, false);
    control->setBounds(newui::Rect(0, 0, control->naturalSize().width, control->naturalSize().height));

    BLImage image(int(control->naturalSize().width), int(control->naturalSize().height), BL_FORMAT_PRGB32);
    BLContext ctx(image);
    control->paint(ctx);
    ctx.end();

    delete control;
}

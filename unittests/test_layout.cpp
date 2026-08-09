#include "newui/layout.h"
#include "newui/subview.h"

#include <gtest/gtest.h>

namespace {

// Views are heap-only, never stack-allocated - see View's class comment -
// so every container/child a layout test needs is constructed with new
// and explicitly deleted at the end of the test. SubView (via View)
// also defaults to invisible until setVisible(true) - the same
// convention View::paintChildren() relies on - so a child a test wants
// arranged has to opt in explicitly, exactly like a real child would
// need to before it's painted.
newui::SubView* NewChild(newui::SubView* container, const newui::Rect& initialBounds) {
    auto* child = new newui::SubView();
    child->setBounds(initialBounds);
    child->setVisible(true);
    container->addChild(child);
    return child;
}

}  // namespace

// ---------------------------------------------------------------------
// AnchorLayout
// ---------------------------------------------------------------------

TEST(AnchorLayout, LeavesChildWithoutParamsUntouched) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = NewChild(container, newui::Rect(10, 10, 30, 30));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    EXPECT_EQ(child->getBounds(), newui::Rect(10, 10, 30, 30));

    delete child;
    delete container;
}

TEST(AnchorLayout, SkipsInvisibleChildren) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = new newui::SubView();
    child->setBounds(newui::Rect(1, 2, 3, 4));
    // left invisible - never setVisible(true)
    container->addChild(child);

    auto params = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Left | newui::Anchor::Top);
    params->width = 40.0f;
    params->height = 20.0f;
    child->setLayoutParams(std::move(params));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    EXPECT_EQ(child->getBounds(), newui::Rect(1, 2, 3, 4));

    delete child;
    delete container;
}

TEST(AnchorLayout, LeftTopPositionsAtMarginWithOwnSize) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = NewChild(container, newui::Rect());

    auto params = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Left | newui::Anchor::Top);
    params->leftMargin = 5.0f;
    params->topMargin = 8.0f;
    params->width = 40.0f;
    params->height = 20.0f;
    child->setLayoutParams(std::move(params));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    EXPECT_EQ(child->getBounds(), newui::Rect(5.0f, 8.0f, 40.0f, 20.0f));

    delete child;
    delete container;
}

TEST(AnchorLayout, RightBottomPositionsFromFarEdge) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = NewChild(container, newui::Rect());

    auto params = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Right | newui::Anchor::Bottom);
    params->rightMargin = 10.0f;
    params->bottomMargin = 5.0f;
    params->width = 30.0f;
    params->height = 20.0f;
    child->setLayoutParams(std::move(params));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    // x = 200 - 10 - 30 = 160, y = 100 - 5 - 20 = 75
    EXPECT_EQ(child->getBounds(), newui::Rect(160.0f, 75.0f, 30.0f, 20.0f));

    delete child;
    delete container;
}

TEST(AnchorLayout, OpposingAnchorsStretchThatAxis) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = NewChild(container, newui::Rect());

    auto params = std::make_unique<newui::AnchorLayoutParams>(
        newui::Anchor::Left | newui::Anchor::Right | newui::Anchor::Top);
    params->leftMargin = 10.0f;
    params->rightMargin = 20.0f;
    params->topMargin = 5.0f;
    params->height = 15.0f;
    child->setLayoutParams(std::move(params));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    // width = 200 - 10 - 20 = 170
    EXPECT_EQ(child->getBounds(), newui::Rect(10.0f, 5.0f, 170.0f, 15.0f));

    delete child;
    delete container;
}

TEST(AnchorLayout, CenterXCenterYCentersChild) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = NewChild(container, newui::Rect());

    auto params = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::CenterX | newui::Anchor::CenterY);
    params->width = 50.0f;
    params->height = 20.0f;
    child->setLayoutParams(std::move(params));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    // x = (200-50)/2 = 75, y = (100-20)/2 = 40
    EXPECT_EQ(child->getBounds(), newui::Rect(75.0f, 40.0f, 50.0f, 20.0f));

    delete child;
    delete container;
}

// ---------------------------------------------------------------------
// StackLayout
// ---------------------------------------------------------------------

TEST(StackLayout, VerticalStacksNaturalSizesWithSpacing) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));

    auto* childA = NewChild(container, newui::Rect(0, 0, 40, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    auto layout = std::make_unique<newui::StackLayout>(newui::Orientation::Vertical);
    layout->setSpacing(10.0f);
    container->setLayout(std::move(layout));

    // Default CrossAxisAlignment::Stretch fills the cross axis (width)
    // to the container's own width.
    EXPECT_EQ(childA->getBounds(), newui::Rect(0.0f, 0.0f, 100.0f, 20.0f));
    EXPECT_EQ(childB->getBounds(), newui::Rect(0.0f, 30.0f, 100.0f, 30.0f));

    delete childA;
    delete childB;
    delete container;
}

TEST(StackLayout, HorizontalStacksNaturalSizesWithSpacing) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 300, 100));

    auto* childA = NewChild(container, newui::Rect(0, 0, 40, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    auto layout = std::make_unique<newui::StackLayout>(newui::Orientation::Horizontal);
    layout->setSpacing(10.0f);
    container->setLayout(std::move(layout));

    EXPECT_EQ(childA->getBounds(), newui::Rect(0.0f, 0.0f, 40.0f, 100.0f));
    EXPECT_EQ(childB->getBounds(), newui::Rect(50.0f, 0.0f, 60.0f, 100.0f));

    delete childA;
    delete childB;
    delete container;
}

TEST(StackLayout, WeightedChildAbsorbsLeftoverSpace) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));

    auto* fixed = NewChild(container, newui::Rect(0, 0, 50, 50));

    auto* flexible = new newui::SubView();
    flexible->setLayoutParams(std::make_unique<newui::StackLayoutParams>(1.0f));
    flexible->setBounds(newui::Rect(0, 0, 50, 20));
    flexible->setVisible(true);
    container->addChild(flexible);

    container->setLayout(std::make_unique<newui::StackLayout>(newui::Orientation::Vertical));

    // fixed keeps its natural 50 height; flexible has flex-basis 0 (see
    // StackLayout::arrange()) so it claims all 250 leftover (300 - 50,
    // no spacing) regardless of the 20 its bounds started at.
    EXPECT_EQ(fixed->getBounds().size().height, 50.0f);
    EXPECT_EQ(flexible->getBounds().size().height, 250.0f);
    EXPECT_EQ(flexible->getBounds().top(), 50.0f);

    delete fixed;
    delete flexible;
    delete container;
}

TEST(StackLayout, WeightedChildShrinksAcrossRepeatedArranges) {
    // Regression test: a weighted child's resolved main-axis size used
    // to be read back as the next arrange() call's "natural" size,
    // compounding across repeated calls - most visibly, a window
    // shrinking after the row had already been laid out once would
    // leave the weighted child stuck at its old (now oversized) width
    // forever, since the inflated "natural" size clamped leftover to 0.
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 500, 100));

    auto* flexible = new newui::SubView();
    flexible->setLayoutParams(std::make_unique<newui::StackLayoutParams>(1.0f));
    flexible->setVisible(true);
    container->addChild(flexible);

    container->setLayout(std::make_unique<newui::StackLayout>(newui::Orientation::Horizontal));
    ASSERT_EQ(flexible->getBounds().size().width, 500.0f);

    // Shrink the container - same trigger RootView::setBounds() uses on
    // a real window resize (SubView::setBounds() -> updateLayout()).
    container->setBounds(newui::Rect(0, 0, 120, 100));

    EXPECT_EQ(flexible->getBounds().size().width, 120.0f);

    delete flexible;
    delete container;
}

TEST(StackLayout, MainAxisAlignmentCenterCentersUnweightedChildren) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 100));

    auto* child = NewChild(container, newui::Rect(0, 0, 100, 20));

    auto layout = std::make_unique<newui::StackLayout>(newui::Orientation::Vertical);
    layout->setMainAxisAlignment(newui::MainAxisAlignment::Center);
    container->setLayout(std::move(layout));

    // leftover = 100 - 20 = 80, centered => top = 40
    EXPECT_EQ(child->getBounds().top(), 40.0f);
    EXPECT_EQ(child->getBounds().size().height, 20.0f);

    delete child;
    delete container;
}

TEST(StackLayout, CrossAxisAlignmentStartKeepsNaturalCrossSize) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = NewChild(container, newui::Rect(0, 0, 50, 20));

    auto layout = std::make_unique<newui::StackLayout>(newui::Orientation::Vertical);
    layout->setCrossAxisAlignment(newui::CrossAxisAlignment::Start);
    container->setLayout(std::move(layout));

    EXPECT_EQ(child->getBounds(), newui::Rect(0.0f, 0.0f, 50.0f, 20.0f));

    delete child;
    delete container;
}

TEST(StackLayout, SkipsInvisibleChildren) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));

    auto* hidden = new newui::SubView();
    hidden->setBounds(newui::Rect(1, 2, 3, 4));
    // left invisible
    container->addChild(hidden);

    auto* visible = NewChild(container, newui::Rect(0, 0, 40, 20));

    container->setLayout(std::make_unique<newui::StackLayout>(newui::Orientation::Vertical));

    EXPECT_EQ(hidden->getBounds(), newui::Rect(1, 2, 3, 4));  // untouched
    EXPECT_EQ(visible->getBounds().top(), 0.0f);  // laid out as if alone

    delete hidden;
    delete visible;
    delete container;
}

// ---------------------------------------------------------------------
// CardLayout
// ---------------------------------------------------------------------

TEST(CardLayout, OnlyActiveChildIsVisibleAndFillsContainer) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* childA = NewChild(container, newui::Rect(0, 0, 10, 10));
    auto* childB = NewChild(container, newui::Rect(0, 0, 10, 10));

    container->setLayout(std::make_unique<newui::CardLayout>());

    EXPECT_TRUE(childA->isVisible());
    EXPECT_FALSE(childB->isVisible());
    EXPECT_EQ(childA->getBounds(), newui::Rect(0.0f, 0.0f, 200.0f, 100.0f));

    delete childA;
    delete childB;
    delete container;
}

TEST(CardLayout, ShowByIndexSwitchesImmediately) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* childA = NewChild(container, newui::Rect(0, 0, 10, 10));
    auto* childB = NewChild(container, newui::Rect(0, 0, 10, 10));

    container->setLayout(std::make_unique<newui::CardLayout>());
    auto* cardLayout = static_cast<newui::CardLayout*>(container->layout());

    cardLayout->show(1);

    EXPECT_FALSE(childA->isVisible());
    EXPECT_TRUE(childB->isVisible());
    EXPECT_EQ(childB->getBounds(), newui::Rect(0.0f, 0.0f, 200.0f, 100.0f));
    EXPECT_EQ(cardLayout->activeIndex(), 1u);

    delete childA;
    delete childB;
    delete container;
}

TEST(CardLayout, ShowByNameMatchesChildName) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* childA = NewChild(container, newui::Rect(0, 0, 10, 10));
    auto* childB = NewChild(container, newui::Rect(0, 0, 10, 10));
    childA->setName("first");
    childB->setName("second");

    container->setLayout(std::make_unique<newui::CardLayout>());
    auto* cardLayout = static_cast<newui::CardLayout*>(container->layout());

    cardLayout->show("second");

    EXPECT_FALSE(childA->isVisible());
    EXPECT_TRUE(childB->isVisible());

    delete childA;
    delete childB;
    delete container;
}

TEST(CardLayout, NextWrapsAroundToFirst) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* childA = NewChild(container, newui::Rect(0, 0, 10, 10));
    auto* childB = NewChild(container, newui::Rect(0, 0, 10, 10));

    container->setLayout(std::make_unique<newui::CardLayout>());
    auto* cardLayout = static_cast<newui::CardLayout*>(container->layout());

    cardLayout->next();  // 0 -> 1
    cardLayout->next();  // 1 -> 0 (wraps)

    EXPECT_EQ(cardLayout->activeIndex(), 0u);
    EXPECT_TRUE(childA->isVisible());

    delete childA;
    delete childB;
    delete container;
}

TEST(CardLayout, PreviousWrapsAroundToLast) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* childA = NewChild(container, newui::Rect(0, 0, 10, 10));
    auto* childB = NewChild(container, newui::Rect(0, 0, 10, 10));

    container->setLayout(std::make_unique<newui::CardLayout>());
    auto* cardLayout = static_cast<newui::CardLayout*>(container->layout());

    cardLayout->previous();  // 0 -> wraps to last (1)

    EXPECT_EQ(cardLayout->activeIndex(), 1u);
    EXPECT_TRUE(childB->isVisible());

    delete childA;
    delete childB;
    delete container;
}

// ---------------------------------------------------------------------
// View/SubView integration
// ---------------------------------------------------------------------

TEST(ViewLayout, ResizingContainerReRunsLayout) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 100));

    auto* child = NewChild(container, newui::Rect());

    auto params = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Right | newui::Anchor::Top);
    params->width = 20.0f;
    params->height = 20.0f;
    child->setLayoutParams(std::move(params));

    container->setLayout(std::make_unique<newui::AnchorLayout>());
    ASSERT_EQ(child->getBounds().left(), 80.0f);  // 100 - 20

    container->setBounds(newui::Rect(0, 0, 300, 100));

    EXPECT_EQ(child->getBounds().left(), 280.0f);  // 300 - 20, re-arranged automatically

    delete child;
    delete container;
}

TEST(ViewLayout, AddingChildReRunsLayout) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));
    container->setLayout(std::make_unique<newui::StackLayout>(newui::Orientation::Vertical));

    auto* childA = NewChild(container, newui::Rect(0, 0, 50, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 50, 30));

    EXPECT_EQ(childA->getBounds().top(), 0.0f);
    EXPECT_EQ(childB->getBounds().top(), 20.0f);

    delete childA;
    delete childB;
    delete container;
}

TEST(ViewLayout, NullLayoutStopsAutomaticArranging) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));
    container->setLayout(std::make_unique<newui::StackLayout>(newui::Orientation::Vertical));

    auto* child = NewChild(container, newui::Rect(5, 5, 50, 20));
    ASSERT_EQ(child->getBounds().top(), 0.0f);  // arranged by the StackLayout

    container->setLayout(nullptr);
    child->setBounds(newui::Rect(5, 5, 50, 20));  // manual positioning again

    EXPECT_EQ(child->getBounds(), newui::Rect(5, 5, 50, 20));

    delete child;
    delete container;
}

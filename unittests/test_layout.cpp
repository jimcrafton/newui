#include "newui/layout.h"
#include "newui/subview.h"
#include "newui/viewstyle.h"

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

TEST(AnchorLayout, ArrangesWithinStyledContainersClientBounds) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));
    auto style = std::make_unique<newui::ViewStyle>();
    style->borderWidth = 10.0f;
    container->setStyle(std::move(style));

    auto* child = NewChild(container, newui::Rect());

    auto params = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Left | newui::Anchor::Top);
    params->width = 20.0f;
    params->height = 20.0f;
    child->setLayoutParams(std::move(params));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    // client bounds = (10,10)-(180,80): Left|Top with no margin sits at
    // the client area's origin, not the container's raw (0,0).
    EXPECT_EQ(child->getBounds(), newui::Rect(10.0f, 10.0f, 20.0f, 20.0f));

    delete child;
    delete container;
}

TEST(AnchorLayout, OpposingAnchorsStretchWithinStyledContainersClientBounds) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));
    auto style = std::make_unique<newui::ViewStyle>();
    style->borderWidth = 10.0f;
    container->setStyle(std::move(style));

    auto* child = NewChild(container, newui::Rect());
    child->setLayoutParams(std::make_unique<newui::AnchorLayoutParams>(
        newui::Anchor::Left | newui::Anchor::Right | newui::Anchor::Top | newui::Anchor::Bottom));

    container->setLayout(std::make_unique<newui::AnchorLayout>());

    // Fully-stretched child fills the 180x80 client area starting at (10,10).
    EXPECT_EQ(child->getBounds(), newui::Rect(10.0f, 10.0f, 180.0f, 80.0f));

    delete child;
    delete container;
}

// ---------------------------------------------------------------------
// FlexLayout
// ---------------------------------------------------------------------

TEST(FlexLayout, VerticalStacksNaturalSizesWithSpacing) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));

    auto* childA = NewChild(container, newui::Rect(0, 0, 40, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
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

TEST(FlexLayout, HorizontalStacksNaturalSizesWithSpacing) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 300, 100));

    auto* childA = NewChild(container, newui::Rect(0, 0, 40, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    layout->setSpacing(10.0f);
    container->setLayout(std::move(layout));

    EXPECT_EQ(childA->getBounds(), newui::Rect(0.0f, 0.0f, 40.0f, 100.0f));
    EXPECT_EQ(childB->getBounds(), newui::Rect(50.0f, 0.0f, 60.0f, 100.0f));

    delete childA;
    delete childB;
    delete container;
}

TEST(FlexLayout, WeightedChildAbsorbsLeftoverSpace) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));

    auto* fixed = NewChild(container, newui::Rect(0, 0, 50, 50));

    auto* flexible = new newui::SubView();
    flexible->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    flexible->setBounds(newui::Rect(0, 0, 50, 20));
    flexible->setVisible(true);
    container->addChild(flexible);

    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

    // fixed keeps its natural 50 height; flexible has flex-basis 0 (see
    // FlexLayout::arrange()) so it claims all 250 leftover (300 - 50,
    // no spacing) regardless of the 20 its bounds started at.
    EXPECT_EQ(fixed->getBounds().size().height, 50.0f);
    EXPECT_EQ(flexible->getBounds().size().height, 250.0f);
    EXPECT_EQ(flexible->getBounds().top(), 50.0f);

    delete fixed;
    delete flexible;
    delete container;
}

TEST(FlexLayout, WeightedChildShrinksAcrossRepeatedArranges) {
    // Regression test: a weighted child's resolved main-axis size used
    // to be read back as the next arrange() call's "natural" size,
    // compounding across repeated calls - most visibly, a window
    // shrinking after the row had already been laid out once would
    // leave the weighted child stuck at its old (now oversized) width
    // forever, since the inflated "natural" size clamped leftover to 0.
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 500, 100));

    auto* flexible = new newui::SubView();
    flexible->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    flexible->setVisible(true);
    container->addChild(flexible);

    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
    ASSERT_EQ(flexible->getBounds().size().width, 500.0f);

    // Shrink the container - same trigger RootView::setBounds() uses on
    // a real window resize (SubView::setBounds() -> updateLayout()).
    container->setBounds(newui::Rect(0, 0, 120, 100));

    EXPECT_EQ(flexible->getBounds().size().width, 120.0f);

    delete flexible;
    delete container;
}

TEST(FlexLayout, MainAxisAlignmentCenterCentersUnweightedChildren) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 100));

    auto* child = NewChild(container, newui::Rect(0, 0, 100, 20));

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    layout->setMainAxisAlignment(newui::MainAxisAlignment::Center);
    container->setLayout(std::move(layout));

    // leftover = 100 - 20 = 80, centered => top = 40
    EXPECT_EQ(child->getBounds().top(), 40.0f);
    EXPECT_EQ(child->getBounds().size().height, 20.0f);

    delete child;
    delete container;
}

TEST(FlexLayout, CrossAxisAlignmentStartKeepsNaturalCrossSize) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));

    auto* child = NewChild(container, newui::Rect(0, 0, 50, 20));

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    layout->setCrossAxisAlignment(newui::CrossAxisAlignment::Start);
    container->setLayout(std::move(layout));

    EXPECT_EQ(child->getBounds(), newui::Rect(0.0f, 0.0f, 50.0f, 20.0f));

    delete child;
    delete container;
}

TEST(FlexLayout, SkipsInvisibleChildren) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));

    auto* hidden = new newui::SubView();
    hidden->setBounds(newui::Rect(1, 2, 3, 4));
    // left invisible
    container->addChild(hidden);

    auto* visible = NewChild(container, newui::Rect(0, 0, 40, 20));

    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

    EXPECT_EQ(hidden->getBounds(), newui::Rect(1, 2, 3, 4));  // untouched
    EXPECT_EQ(visible->getBounds().top(), 0.0f);  // laid out as if alone

    delete hidden;
    delete visible;
    delete container;
}

TEST(FlexLayout, ArrangesWithinStyledContainersClientBounds) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 300));
    auto style = std::make_unique<newui::ViewStyle>();
    style->borderWidth = 10.0f;
    container->setStyle(std::move(style));

    auto* childA = NewChild(container, newui::Rect(0, 0, 40, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

    // Client bounds = (10,10)-(80,280): Stretch fills the deflated cross
    // axis (width 80, not the raw 100), and both children start offset by
    // the 10px border on every side.
    EXPECT_EQ(childA->getBounds(), newui::Rect(10.0f, 10.0f, 80.0f, 20.0f));
    EXPECT_EQ(childB->getBounds(), newui::Rect(10.0f, 30.0f, 80.0f, 30.0f));

    delete childA;
    delete childB;
    delete container;
}

// ---------------------------------------------------------------------
// FlexLayout wrap
// ---------------------------------------------------------------------

TEST(FlexLayoutWrap, WrapsOntoASecondLineWhenMainAxisOverflows) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 200));

    // Horizontal, 100-wide container: two 60-wide children can't share a
    // line (60+60 > 100), so the second wraps onto its own line below.
    auto* childA = NewChild(container, newui::Rect(0, 0, 60, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    layout->setWrap(true);
    container->setLayout(std::move(layout));

    EXPECT_EQ(childA->getBounds(), newui::Rect(0.0f, 0.0f, 60.0f, 20.0f));
    // Second line starts at cross-axis offset = line 1's thickness (20,
    // the max cross size on that line) - no lineSpacing set (default 0).
    EXPECT_EQ(childB->getBounds(), newui::Rect(0.0f, 20.0f, 60.0f, 30.0f));

    delete childA;
    delete childB;
    delete container;
}

TEST(FlexLayoutWrap, LineSpacingGapsBetweenLines) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 200));

    auto* childA = NewChild(container, newui::Rect(0, 0, 60, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    layout->setWrap(true);
    layout->setLineSpacing(5.0f);
    container->setLayout(std::move(layout));

    EXPECT_EQ(childB->getBounds().top(), 25.0f);  // 20 (line 1 thickness) + 5 (lineSpacing)

    delete childA;
    delete childB;
    delete container;
}

TEST(FlexLayoutWrap, WeightedChildAbsorbsLeftoverWithinItsOwnLineOnly) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 200));

    // Line 1: a 60-wide fixed child + a weighted child - weighted claims
    // line 1's own leftover (100-60=40), not the whole container.
    auto* fixed1 = NewChild(container, newui::Rect(0, 0, 60, 20));
    auto* weighted1 = new newui::SubView();
    weighted1->setDesiredSize(newui::Size(10, 20));
    weighted1->setVisible(true);
    weighted1->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    container->addChild(weighted1);

    // Line 2 (forced by a wide child): another weighted child, alone on
    // its own line, claims that line's own leftover instead.
    auto* wide2 = new newui::SubView();
    wide2->setDesiredSize(newui::Size(70, 15));
    wide2->setVisible(true);
    container->addChild(wide2);
    auto* weighted2 = new newui::SubView();
    weighted2->setDesiredSize(newui::Size(10, 15));
    weighted2->setVisible(true);
    weighted2->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    container->addChild(weighted2);

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    layout->setWrap(true);
    container->setLayout(std::move(layout));

    // Line 1 leftover = 100 - 60 = 40, entirely claimed by weighted1.
    EXPECT_FLOAT_EQ(weighted1->getBounds().size().width, 40.0f);
    // Line 2 leftover = 100 - 70 = 30, entirely claimed by weighted2 -
    // unaffected by line 1's own leftover/weighted child.
    EXPECT_FLOAT_EQ(weighted2->getBounds().size().width, 30.0f);

    delete fixed1;
    delete weighted1;
    delete wide2;
    delete weighted2;
    delete container;
}

TEST(FlexLayoutWrap, AlignContentCentersLinesWithinLeftoverCrossSpace) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 100));

    // Two lines of thickness 20 each (forced by two 60-wide children in a
    // 100-wide container) - leftover cross space = 100 - 40 = 60.
    auto* childA = NewChild(container, newui::Rect(0, 0, 60, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 20));

    auto layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    layout->setWrap(true);
    layout->setAlignContent(newui::MainAxisAlignment::Center);
    container->setLayout(std::move(layout));

    // startOffset = 60 leftover / 2 = 30
    EXPECT_FLOAT_EQ(childA->getBounds().top(), 30.0f);
    EXPECT_FLOAT_EQ(childB->getBounds().top(), 50.0f);  // 30 + 20 (line 1 thickness)

    delete childA;
    delete childB;
    delete container;
}

TEST(FlexLayoutWrap, DefaultWrapFalseMatchesNonWrappingBehavior) {
    // Regression check: wrap's default (false) must reproduce the exact
    // non-wrap arrangement, even for children that would otherwise wrap -
    // this is what makes the StackLayout->FlexLayout rename behavior-
    // preserving for every existing caller that never opts into wrap().
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 200));

    auto* childA = NewChild(container, newui::Rect(0, 0, 60, 20));
    auto* childB = NewChild(container, newui::Rect(0, 0, 60, 30));

    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));

    // Default CrossAxisAlignment::Stretch fills the *whole container's*
    // cross size (200) when not wrapped - unlike wrap mode, where Stretch
    // fills only each line's own thickness (see the wrap tests above).
    EXPECT_EQ(childA->getBounds(), newui::Rect(0.0f, 0.0f, 60.0f, 200.0f));
    EXPECT_EQ(childB->getBounds(), newui::Rect(60.0f, 0.0f, 60.0f, 200.0f));  // spills past the container, not wrapped

    delete childA;
    delete childB;
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

TEST(CardLayout, ActiveChildFillsStyledContainersClientBounds) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 200, 100));
    auto style = std::make_unique<newui::ViewStyle>();
    style->borderWidth = 10.0f;
    container->setStyle(std::move(style));

    auto* childA = NewChild(container, newui::Rect(0, 0, 10, 10));
    auto* childB = NewChild(container, newui::Rect(0, 0, 10, 10));

    container->setLayout(std::make_unique<newui::CardLayout>());

    EXPECT_EQ(childA->getBounds(), newui::Rect(10.0f, 10.0f, 180.0f, 80.0f));

    delete childA;
    delete childB;
    delete container;
}

// ---------------------------------------------------------------------
// GridLayout
// ---------------------------------------------------------------------

TEST(GridLayout, FixedColumnsPlaceChildrenAtExactPixelOffsets) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 120, 30));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(50.0f);
    grid->addFixedColumn(70.0f);
    grid->addFixedRow(30.0f);
    container->setLayout(std::move(grid));

    auto* child = NewChild(container, newui::Rect());
    child->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 1));
    container->updateLayout();

    EXPECT_EQ(child->getBounds(), newui::Rect(50.0f, 0.0f, 70.0f, 30.0f));

    delete child;
    delete container;
}

TEST(GridLayout, StarColumnsSplitLeftoverProportionalToWeight) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 120, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(20.0f);
    grid->addStarColumn(1.0f);
    grid->addStarColumn(3.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    // leftover = 120 - 20 = 100, split 1:3 -> 25 and 75.
    auto* childInStar1 = NewChild(container, newui::Rect());
    childInStar1->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 1));
    auto* childInStar3 = NewChild(container, newui::Rect());
    childInStar3->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 2));
    container->updateLayout();

    EXPECT_EQ(childInStar1->getBounds(), newui::Rect(20.0f, 0.0f, 25.0f, 50.0f));
    EXPECT_EQ(childInStar3->getBounds(), newui::Rect(45.0f, 0.0f, 75.0f, 50.0f));

    delete childInStar1;
    delete childInStar3;
    delete container;
}

TEST(GridLayout, AutoColumnSizesToLargestDesiredSizeIgnoringSpanningChildren) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 70, 20));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addAutoColumn();
    grid->addFixedColumn(30.0f);
    grid->addFixedRow(20.0f);
    container->setLayout(std::move(grid));

    auto* narrow = new newui::SubView();
    narrow->setDesiredSize(newui::Size(25.0f, 10.0f));
    narrow->setVisible(true);
    narrow->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    container->addChild(narrow);

    auto* wide = new newui::SubView();
    wide->setDesiredSize(newui::Size(40.0f, 10.0f));
    wide->setVisible(true);
    wide->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    container->addChild(wide);

    // Spans both columns - ignored when auto-sizing column 0, even though
    // its own desiredSize() is much larger than either non-spanning child.
    auto* spanning = new newui::SubView();
    spanning->setDesiredSize(newui::Size(1000.0f, 10.0f));
    spanning->setVisible(true);
    auto spanParams = std::make_unique<newui::GridLayoutParams>(0, 0);
    spanParams->columnSpan = 2;
    spanning->setLayoutParams(std::move(spanParams));
    container->addChild(spanning);

    container->updateLayout();

    // Auto column 0 = max(25, 40) = 40, ignoring spanning's 1000.
    EXPECT_FLOAT_EQ(wide->getBounds().size().width, 40.0f);
    EXPECT_FLOAT_EQ(spanning->getBounds().left(), 0.0f);
    EXPECT_FLOAT_EQ(spanning->getBounds().size().width, 70.0f);  // 40 (auto) + 30 (fixed)

    delete narrow;
    delete wide;
    delete spanning;
    delete container;
}

TEST(GridLayout, ColumnSpanUnionsTrackExtents) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 60, 20));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(20.0f);
    grid->addFixedColumn(30.0f);
    grid->addFixedColumn(10.0f);
    grid->addFixedRow(20.0f);
    container->setLayout(std::move(grid));

    auto* child = NewChild(container, newui::Rect());
    auto params = std::make_unique<newui::GridLayoutParams>(0, 0);
    params->columnSpan = 2;
    child->setLayoutParams(std::move(params));
    container->updateLayout();

    EXPECT_EQ(child->getBounds(), newui::Rect(0.0f, 0.0f, 50.0f, 20.0f));  // 20 + 30

    delete child;
    delete container;
}

TEST(GridLayout, NonStretchAlignmentKeepsDesiredSizeAndCentersWithinCell) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(100.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    auto* child = new newui::SubView();
    child->setDesiredSize(newui::Size(40.0f, 20.0f));
    child->setVisible(true);
    auto params = std::make_unique<newui::GridLayoutParams>(0, 0);
    params->horizontalAlignment = newui::CrossAxisAlignment::Center;
    params->verticalAlignment = newui::CrossAxisAlignment::Center;
    child->setLayoutParams(std::move(params));
    container->addChild(child);
    container->updateLayout();

    EXPECT_EQ(child->getBounds(), newui::Rect(30.0f, 15.0f, 40.0f, 20.0f));

    delete child;
    delete container;
}

TEST(GridLayout, StretchDefaultFillsTheCell) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(100.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    auto* child = NewChild(container, newui::Rect());
    child->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    container->updateLayout();

    EXPECT_EQ(child->getBounds(), newui::Rect(0.0f, 0.0f, 100.0f, 50.0f));

    delete child;
    delete container;
}

TEST(GridLayout, ChildWithNoParamsIsLeftUntouched) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(100.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    auto* child = NewChild(container, newui::Rect(5, 5, 1, 1));
    container->updateLayout();

    EXPECT_EQ(child->getBounds(), newui::Rect(5, 5, 1, 1));

    delete child;
    delete container;
}

TEST(GridLayout, OutOfRangeRowOrColumnIsLeftUntouched) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 100, 50));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(100.0f);
    grid->addFixedRow(50.0f);
    container->setLayout(std::move(grid));

    auto* child = NewChild(container, newui::Rect(5, 5, 1, 1));
    child->setLayoutParams(std::make_unique<newui::GridLayoutParams>(3, 0));  // no row 3
    container->updateLayout();

    EXPECT_EQ(child->getBounds(), newui::Rect(5, 5, 1, 1));

    delete child;
    delete container;
}

TEST(GridLayout, ColumnAndRowSpacingOffsetTracks) {
    auto* container = new newui::SubView();
    container->setBounds(newui::Rect(0, 0, 55, 27));

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(20.0f);
    grid->addFixedColumn(30.0f);
    grid->setColumnSpacing(5.0f);
    grid->addFixedRow(20.0f);
    container->setLayout(std::move(grid));

    auto* child = NewChild(container, newui::Rect());
    child->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 1));
    container->updateLayout();

    EXPECT_EQ(child->getBounds().left(), 25.0f);  // 20 (column 0) + 5 (columnSpacing)

    delete child;
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
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

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
    container->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

    auto* child = NewChild(container, newui::Rect(5, 5, 50, 20));
    ASSERT_EQ(child->getBounds().top(), 0.0f);  // arranged by the FlexLayout

    container->setLayout(nullptr);
    child->setBounds(newui::Rect(5, 5, 50, 20));  // manual positioning again

    EXPECT_EQ(child->getBounds(), newui::Rect(5, 5, 50, 20));

    delete child;
    delete container;
}

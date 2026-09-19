#include "newui/tabcontrol.h"
#include "newui/keyboard_constants.h"
#include "newui/reflection.h"

#include <gtest/gtest.h>

// Unlike MenuBar/ContextMenu, TabControl needs no live HWND at all - a
// tab click just switches which already-built page is visible via
// CardLayout, entirely in-process (see tabcontrol.cpp's
// TabItemButtonClicked()) - so everything here, including a simulated
// click, runs fully headlessly.

namespace {

newui::SubView* MakePage(const std::string& name) {
    auto* page = new newui::SubView();
    page->setName(name);
    page->setVisible(true);
    return page;
}

// Delegate::FunctionPtr is a plain function pointer (no capturing
// lambdas), so state observed by a callback has to live at namespace
// scope - same convention test_menus.cpp's RecordClick()/g_clickLog
// already uses.
int g_tabChangedCallCount = 0;
std::size_t g_lastChangedIndex = 0;

newui::SyncReturn RecordTabChanged(newui::TabControl&, std::size_t index) {
    ++g_tabChangedCallCount;
    g_lastChangedIndex = index;
    return newui::SyncReturn::Handled;
}

}  // namespace

TEST(TabControl, ConstructedVisibleWithThemedPaneStyle) {
    auto* tabs = new newui::TabControl();

    EXPECT_TRUE(tabs->isVisible());
    EXPECT_NE(dynamic_cast<const newui::ThemedTabPaneStyle*>(&tabs->style()), nullptr);
    EXPECT_EQ(tabs->tabCount(), 0u);
    EXPECT_EQ(tabs->alignment(), newui::ThemedTabItemStyle::TabAlignment::Top);

    // Even with zero tabs, TabControl already has 2 children of its own
    // (stripRow_/pagesArea_, built in the constructor) - destroy() (not
    // a plain delete) is needed to avoid leaking them, same convention
    // as MenuBar's own tests for a TabControl/MenuBar with children.
    tabs->destroy();
    delete tabs;
}

TEST(TabControl, AddTabBuildsOneButtonAndOnePageEach) {
    auto* tabs = new newui::TabControl();

    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));

    EXPECT_EQ(tabs->tabCount(), 2u);
    ASSERT_NE(tabs->tabButton(0), nullptr);
    ASSERT_NE(tabs->tabButton(1), nullptr);
    ASSERT_NE(tabs->page(0), nullptr);
    ASSERT_NE(tabs->page(1), nullptr);
     EXPECT_EQ(tabs->page(0)->name(), "page1");
     EXPECT_EQ(tabs->page(1)->name(), "page2");
    EXPECT_EQ(tabs->tabButton(2), nullptr);
    EXPECT_EQ(tabs->page(2), nullptr);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, FirstTabAutoSelectsAndShowsItsPage) {
    auto* tabs = new newui::TabControl();

    tabs->addTab("First", MakePage("page1"));

    EXPECT_EQ(tabs->selectedIndex(), 0u);
    auto* button0Style = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(0)->style());
    ASSERT_NE(button0Style, nullptr);
    EXPECT_TRUE(button0Style->selected);
    EXPECT_TRUE(tabs->page(0)->isVisible());

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, PositionRecomputesAcrossMultipleTabs) {
    auto* tabs = new newui::TabControl();

    tabs->addTab("Only", MakePage("page1"));
    auto* only = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(0)->style());
    ASSERT_NE(only, nullptr);
    EXPECT_EQ(only->position, newui::ThemedTabItemStyle::Position::Only);

    tabs->addTab("Second", MakePage("page2"));
    auto* first = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(0)->style());
    auto* second = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(1)->style());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(first->position, newui::ThemedTabItemStyle::Position::Left);
    EXPECT_EQ(second->position, newui::ThemedTabItemStyle::Position::Right);

    tabs->addTab("Third", MakePage("page3"));
    first = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(0)->style());
    auto* middle = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(1)->style());
    auto* last = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(2)->style());
    EXPECT_EQ(first->position, newui::ThemedTabItemStyle::Position::Left);
    EXPECT_EQ(middle->position, newui::ThemedTabItemStyle::Position::Middle);
    EXPECT_EQ(last->position, newui::ThemedTabItemStyle::Position::Right);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, SelectTabUpdatesSelectedFlagAndPageVisibility) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));
    tabs->addTab("Third", MakePage("page3"));

    g_tabChangedCallCount = 0;
    tabs->onTabChanged.add(&RecordTabChanged);

    tabs->selectTab(2);

    EXPECT_EQ(tabs->selectedIndex(), 2u);
    EXPECT_EQ(g_tabChangedCallCount, 1);
    EXPECT_EQ(g_lastChangedIndex, 2u);

    EXPECT_FALSE(dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(0)->style())->selected);
    EXPECT_FALSE(dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(1)->style())->selected);
    EXPECT_TRUE(dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(2)->style())->selected);

    EXPECT_FALSE(tabs->page(0)->isVisible());
    EXPECT_FALSE(tabs->page(1)->isVisible());
    EXPECT_TRUE(tabs->page(2)->isVisible());

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, SelectTabIgnoresOutOfRangeIndex) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("First", MakePage("page1"));

    tabs->selectTab(99);

    EXPECT_EQ(tabs->selectedIndex(), 0u);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, SimulatedClickOnTabButtonSelectsIt) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));

    newui::SubView* secondButton = tabs->tabButton(1);
    ASSERT_NE(secondButton, nullptr);

    // Same "drive the already-wired handler directly, no real HWND/message
    // pump needed" pattern test_rootview.cpp already uses.
    secondButton->onMouseDown(*secondButton, newui::Point(), 0, 0);

    EXPECT_EQ(tabs->selectedIndex(), 1u);
    EXPECT_TRUE(tabs->page(1)->isVisible());

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, TopBottomUseVerticalOuterLayoutAndHorizontalStrip) {
    auto* topTabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Top);
    topTabs->addTab("A", MakePage("a"));
    EXPECT_EQ(topTabs->alignment(), newui::ThemedTabItemStyle::TabAlignment::Top);

    auto* bottomTabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Bottom);
    bottomTabs->addTab("A", MakePage("a"));
    EXPECT_EQ(bottomTabs->alignment(), newui::ThemedTabItemStyle::TabAlignment::Bottom);

    // Bottom-aligned: pages come before the strip in the outer layout's
    // child order (pages rendered above the strip, per the class comment).
    ASSERT_EQ(bottomTabs->childViews().size(), 2u);
     EXPECT_EQ(bottomTabs->childViews()[0]->name(), "TabControlPages");
     EXPECT_EQ(bottomTabs->childViews()[1]->name(), "TabControlStrip");

    // Top-aligned: strip first.
    ASSERT_EQ(topTabs->childViews().size(), 2u);
     EXPECT_EQ(topTabs->childViews()[0]->name(), "TabControlStrip");
     EXPECT_EQ(topTabs->childViews()[1]->name(), "TabControlPages");

    topTabs->destroy();
    delete topTabs;
    bottomTabs->destroy();
    delete bottomTabs;
}

TEST(TabControl, LeftRightAlignmentButtonsUseTheRightThemeAlignment) {
    auto* leftTabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Left);
    leftTabs->addTab("A", MakePage("a"));

    auto* style = dynamic_cast<newui::ThemedTabItemStyle*>(&leftTabs->tabButton(0)->style());
    ASSERT_NE(style, nullptr);
    EXPECT_EQ(style->alignment, newui::ThemedTabItemStyle::TabAlignment::Left);

    leftTabs->destroy();
    delete leftTabs;
}

// ---------------------------------------------------------------------
// Arrow-key tab switching - the whole strip is one Tab stop (see
// TabControl's own constructor comment, tabcontrol.cpp); once it has
// focus, arrow keys (not the individual buttons) switch which tab is
// selected. Driven directly via onKeyDown(), same "no real HWND/message
// pump needed" pattern SimulatedClickOnTabButtonSelectsIt above uses for
// onMouseDown().
// ---------------------------------------------------------------------

TEST(TabControl, RightArrowAdvancesToTheNextTabOnATopAlignedStrip) {
    auto* tabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Top);
    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));
    tabs->addTab("Third", MakePage("page3"));
    ASSERT_EQ(tabs->selectedIndex(), 0u);

    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkRightArrow);
    EXPECT_EQ(tabs->selectedIndex(), 1u);
    EXPECT_TRUE(tabs->page(1)->isVisible());

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, LeftArrowRetreatsAndWrapsToTheLastTab) {
    auto* tabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Top);
    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));
    tabs->addTab("Third", MakePage("page3"));
    ASSERT_EQ(tabs->selectedIndex(), 0u);

    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkLeftArrow);
    EXPECT_EQ(tabs->selectedIndex(), 2u) << "Left from the first tab must wrap to the last";

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, RightArrowWrapsFromTheLastTabBackToTheFirst) {
    auto* tabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Top);
    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));
    tabs->selectTab(1);

    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkRightArrow);
    EXPECT_EQ(tabs->selectedIndex(), 0u);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, UpDownArrowsAreIgnoredOnAHorizontalStrip) {
    auto* tabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Top);
    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));

    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkDownArrow);
    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkUpArrow);
    EXPECT_EQ(tabs->selectedIndex(), 0u);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, DownArrowAdvancesOnALeftAlignedVerticalStrip) {
    auto* tabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Left);
    tabs->addTab("First", MakePage("page1"));
    tabs->addTab("Second", MakePage("page2"));

    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkDownArrow);
    EXPECT_EQ(tabs->selectedIndex(), 1u);

    // The horizontal-strip keys must do nothing on a vertical strip -
    // this isn't just "Right also happens to work", it's the wrong axis
    // entirely.
    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkRightArrow);
    EXPECT_EQ(tabs->selectedIndex(), 1u);

    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkUpArrow);
    EXPECT_EQ(tabs->selectedIndex(), 0u);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, ArrowKeyOnAnEmptyTabControlIsIgnoredNotACrash) {
    auto* tabs = new newui::TabControl();

    tabs->onKeyDown(*tabs, 0, 0, 1, newui::vkRightArrow);
    EXPECT_EQ(tabs->selectedIndex(), 0u);

    tabs->destroy();
    delete tabs;
}

// Real bug: TabControl's only constructor is `explicit TabControl(TabAlignment = Top)`, and
// reflectgen registered just the one-argument form - so createInstance() with no arguments (what
// the Toolbox, the .newui Reader and anything else constructing by name does) had nothing to call
// and produced nothing. A constructor whose remaining parameters all have defaults is callable with
// fewer arguments, so every arity down to the required count has to be registered.
TEST(TabControlReflection, IsConstructibleByNameWithNoArguments) {
    const newui::reflection::Class* clazz = newui::reflection::classinfo("TabControl");
    ASSERT_NE(clazz, nullptr);

    void* raw = nullptr;
    clazz->createInstance(&raw);
    ASSERT_NE(raw, nullptr);

    auto* tabControl = static_cast<newui::TabControl*>(raw);
    EXPECT_EQ(tabControl->alignment(), newui::ThemedTabItemStyle::TabAlignment::Top);  // the default
    tabControl->destroy();
    delete tabControl;
}

TEST(TabControl, RemoveTabDetachesItsPageAndDestroysItsButton) {
    auto* tabs = new newui::TabControl();
    newui::SubView* page1 = MakePage("page1");
    newui::SubView* page2 = MakePage("page2");
    tabs->addTab("First", page1);
    tabs->addTab("Second", page2);

    newui::SubView* removed = tabs->removeTab(0);

    EXPECT_EQ(removed, page1);
    EXPECT_EQ(removed->parent(), nullptr);  // detached, still alive - the caller owns it now
    EXPECT_EQ(tabs->tabCount(), 1u);
    EXPECT_EQ(tabs->page(0), page2);

    removed->destroy();
    delete removed;
    tabs->destroy();
    delete tabs;
}

TEST(TabControl, RemoveTabOutOfRangeChangesNothing) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("First", MakePage("page1"));

    EXPECT_EQ(tabs->removeTab(1), nullptr);
    EXPECT_EQ(tabs->tabCount(), 1u);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, RemoveTabBeforeTheSelectedOneKeepsTheSameTabSelected) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("A", MakePage("a"));
    tabs->addTab("B", MakePage("b"));
    tabs->addTab("C", MakePage("c"));
    tabs->selectTab(2);
    newui::SubView* pageC = tabs->page(2);

    newui::SubView* removed = tabs->removeTab(0);

    EXPECT_EQ(tabs->selectedIndex(), 1u);
    EXPECT_EQ(tabs->page(tabs->selectedIndex()), pageC);

    removed->destroy();
    delete removed;
    tabs->destroy();
    delete tabs;
}

TEST(TabControl, RemovingTheSelectedLastTabSelectsTheNewLastTab) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("A", MakePage("a"));
    tabs->addTab("B", MakePage("b"));
    tabs->selectTab(1);

    newui::SubView* removed = tabs->removeTab(1);

    EXPECT_EQ(tabs->selectedIndex(), 0u);
    auto* style = dynamic_cast<newui::ThemedTabItemStyle*>(&tabs->tabButton(0)->style());
    ASSERT_NE(style, nullptr);
    EXPECT_TRUE(style->selected);
    EXPECT_EQ(style->position, newui::ThemedTabItemStyle::Position::Only);

    removed->destroy();
    delete removed;
    tabs->destroy();
    delete tabs;
}

TEST(TabControl, RemovingTheOnlyTabLeavesAnEmptyControlThatCanTakeANewTab) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("A", MakePage("a"));

    newui::SubView* removed = tabs->removeTab(0);
    EXPECT_EQ(tabs->tabCount(), 0u);

    // Re-adding the very same page (what an undo/redo pair does) restores a working tab.
    tabs->addTab("A", removed);
    EXPECT_EQ(tabs->tabCount(), 1u);
    EXPECT_EQ(tabs->selectedIndex(), 0u);
    EXPECT_EQ(tabs->page(0), removed);

    tabs->destroy();
    delete tabs;
}

TEST(TabControl, ClickingATabAfterAnEarlierOneWasRemovedSelectsTheRightPage) {
    auto* tabs = new newui::TabControl();
    tabs->addTab("A", MakePage("a"));
    tabs->addTab("B", MakePage("b"));
    tabs->addTab("C", MakePage("c"));
    newui::SubView* removed = tabs->removeTab(0);

    // C's button was index 2 before the removal - its click handler must now select index 1.
    tabs->tabButton(1)->onMouseDown(*tabs->tabButton(1), newui::Point(1, 1), 0, 0);
    EXPECT_EQ(tabs->selectedIndex(), 1u);
    EXPECT_EQ(tabs->page(1)->name(), "c");

    removed->destroy();
    delete removed;
    tabs->destroy();
    delete tabs;
}

TEST(TabControl, ItsStripPagesAreaAndButtonsAreInternalButItsPagesAreNot) {
    auto* tabs = new newui::TabControl();
    newui::SubView* page = MakePage("page1");
    tabs->addTab("First", page);

    newui::View* strip = tabs->tabButton(0)->parent();
    newui::View* pagesArea = page->parent();
    ASSERT_NE(strip, nullptr);
    ASSERT_NE(pagesArea, nullptr);

    EXPECT_TRUE(strip->isInternal());
    EXPECT_TRUE(pagesArea->isInternal());
    EXPECT_TRUE(tabs->tabButton(0)->isInternal());
    EXPECT_FALSE(page->isInternal());
    EXPECT_FALSE(tabs->isInternal());

    EXPECT_FALSE(strip->isSelectableAtDesignTime());
    EXPECT_FALSE(pagesArea->isSelectableAtDesignTime());
    EXPECT_FALSE(tabs->tabButton(0)->isSelectableAtDesignTime());
    EXPECT_TRUE(page->isSelectableAtDesignTime());
    EXPECT_TRUE(tabs->isSelectableAtDesignTime());

    tabs->destroy();
    delete tabs;
}

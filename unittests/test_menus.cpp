#include "newui/menus.h"
#include "newui/viewstyle.h"
#include "newui/layout.h"

#include <gtest/gtest.h>

// Native menu-tree building (ContextMenu) needs no live HWND
// (CreatePopupMenu()/InsertMenuItemA() all work headlessly, same as
// Cursor's HCURSOR-building constructors - see test_cursor.cpp) -
// everything here runs against real Win32 menu handles, just never
// actually shown. ContextMenu::show()'s real TrackPopupMenuEx()
// interaction blocks on real user input and isn't covered here -
// buildNativeMenu() (protected, exposed below via TestableContextMenu)
// is the same "build" logic show() itself calls, without the blocking
// half - same gap/workaround already established for
// ThemedViewStyle::paint()/RootView live-window features. MenuBar is an
// ordinary SubView now (no native menu of its own at all), so its own
// tests are plain View-tree assertions, no live window needed either.

namespace {

// Exposes ContextMenu's protected buildNativeMenu() for direct testing -
// same "protected seam + test-local subclass" convention
// TestableRootView/TestableThemedButtonStyle already use elsewhere in
// this codebase.
class TestableContextMenu : public newui::ContextMenu {
public:
    using ContextMenu::buildNativeMenu;
};

// Delegate::FunctionPtr is a plain function pointer (no capturing
// lambdas), so state observed by a callback has to live at namespace
// scope - same convention test_view.cpp's RecordDestroyed()/
// g_destroyedCount already uses.
std::vector<UINT> g_clickLog;

newui::SyncReturn RecordClick(newui::MenuItem& item) {
    g_clickLog.push_back(item.commandId());
    return newui::SyncReturn::Handled;
}

int g_measureCallCount = 0;
newui::Size g_lastMeasuredDefault;

newui::SyncReturn RecordMeasure(newui::MenuItem&, newui::Size& outSize) {
    ++g_measureCallCount;
    g_lastMeasuredDefault = outSize;
    outSize = newui::Size(123.0f, 45.0f);
    return newui::SyncReturn::Handled;
}

int g_drawCallCount = 0;

newui::Rect g_lastDrawRect;

newui::SyncReturn RecordDraw(newui::MenuItem& item, BLContext&, const newui::Rect& r) {
    ++g_drawCallCount;
    g_lastDrawRect = r;

    return newui::SyncReturn::Handled;
}

}  // namespace

// ---------------------------------------------------------------------------
// MenuItem - plain tree/model behavior, no ContextMenu/MenuBar involved.
// ---------------------------------------------------------------------------

TEST(MenuItem, AddChildSetsParentAndReturnsRawPointer) {
    newui::MenuItem root;
    newui::MenuItem* raw = root.addChild(std::make_unique<newui::MenuItem>("Child"));

    ASSERT_EQ(root.children().size(), 1u);
    EXPECT_EQ(root.children()[0].get(), raw);
    EXPECT_EQ(raw->parent(), &root);
    EXPECT_TRUE(root.hasChildren());
    EXPECT_FALSE(raw->hasChildren());
}

TEST(MenuItem, SeparatorFactorySetsIsSeparator) {
    auto sep = newui::MenuItem::Separator();
    EXPECT_TRUE(sep->isSeparator);
}

TEST(MenuItem, DefaultCommandIdIsZeroBeforeAnyBuild) {
    newui::MenuItem item("Unbuilt");
    EXPECT_EQ(item.commandId(), 0u);
}

// ---------------------------------------------------------------------------
// ContextMenu - native tree building (via buildNativeMenu(), see
// TestableContextMenu above).
// ---------------------------------------------------------------------------

TEST(ContextMenu, BuildNativeMenuBuildsCorrectNestingAndText) {
    newui::MenuItem root;
    newui::MenuItem* file = root.addChild(std::make_unique<newui::MenuItem>("File"));
    file->addChild(std::make_unique<newui::MenuItem>("New"));
    file->addChild(newui::MenuItem::Separator());
    file->addChild(std::make_unique<newui::MenuItem>("Exit"));

    newui::MenuItem* edit = root.addChild(std::make_unique<newui::MenuItem>("Edit"));
    edit->addChild(std::make_unique<newui::MenuItem>("Copy"));

    TestableContextMenu fileMenu;
    fileMenu.buildNativeMenu(*file);

    HMENU hmenu = fileMenu.handle();
    ASSERT_NE(hmenu, nullptr);
    EXPECT_EQ(::GetMenuItemCount(hmenu), 3);

    char buf[64] = {};
    ::GetMenuStringA(hmenu, 0, buf, sizeof(buf), MF_BYPOSITION);
    EXPECT_STREQ(buf, "New");

    // Separator: no command id assigned.
    EXPECT_EQ(::GetMenuItemID(hmenu, 1), 0u);

    ::GetMenuStringA(hmenu, 2, buf, sizeof(buf), MF_BYPOSITION);
    EXPECT_STREQ(buf, "Exit");

    TestableContextMenu editMenu;
    editMenu.buildNativeMenu(*edit);
    EXPECT_EQ(::GetMenuItemCount(editMenu.handle()), 1);
}

TEST(ContextMenu, LeafItemTextIncludesTabSeparatedShortcut) {
    newui::MenuItem root;
    newui::MenuItem* save = root.addChild(std::make_unique<newui::MenuItem>("Save"));
    save->shortcutText = "Ctrl+S";

    TestableContextMenu menu;
    menu.buildNativeMenu(root);

    char buf[64] = {};
    ::GetMenuStringA(menu.handle(), 0, buf, sizeof(buf), MF_BYPOSITION);
    EXPECT_STREQ(buf, "Save\tCtrl+S");
}

TEST(ContextMenu, ParentItemsWithChildrenGetNoCommandId) {
    newui::MenuItem root;
    newui::MenuItem* file = root.addChild(std::make_unique<newui::MenuItem>("File"));
    file->addChild(std::make_unique<newui::MenuItem>("New"));

    TestableContextMenu menu;
    menu.buildNativeMenu(root);

    EXPECT_EQ(file->commandId(), 0u);
}

// ---------------------------------------------------------------------------
// ContextMenu::dispatchCommand() - onClick routing.
// ---------------------------------------------------------------------------

TEST(ContextMenu, DispatchCommandFiresOnlyTheMatchingItemsOnClick) {
    g_clickLog.clear();

    newui::MenuItem root;
    newui::MenuItem* a = root.addChild(std::make_unique<newui::MenuItem>("A"));
    a->onClick.add(&RecordClick);
    newui::MenuItem* b = root.addChild(std::make_unique<newui::MenuItem>("B"));
    b->onClick.add(&RecordClick);

    TestableContextMenu menu;
    menu.buildNativeMenu(root);

    EXPECT_TRUE(menu.dispatchCommand(a->commandId()));
    ASSERT_EQ(g_clickLog.size(), 1u);
    EXPECT_EQ(g_clickLog[0], a->commandId());

    EXPECT_TRUE(menu.dispatchCommand(b->commandId()));
    ASSERT_EQ(g_clickLog.size(), 2u);
    EXPECT_EQ(g_clickLog[1], b->commandId());
}

TEST(ContextMenu, DispatchCommandReturnsFalseForUnknownId) {
    TestableContextMenu menu;
    EXPECT_FALSE(menu.dispatchCommand(999999));
}

// ---------------------------------------------------------------------------
// Radio groups / setChecked() / setEnabled().
// ---------------------------------------------------------------------------

TEST(ContextMenu, RadioGroupMembersGetRadioCheckMenuFlag) {
    newui::MenuItem root;
    newui::MenuItem* smallItem = root.addChild(std::make_unique<newui::MenuItem>("Small"));
    smallItem->radioGroup = 0;

    TestableContextMenu menu;
    menu.buildNativeMenu(root);

    MENUITEMINFOA mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_FTYPE;
    ASSERT_TRUE(::GetMenuItemInfoA(menu.handle(), smallItem->commandId(), FALSE, &mii));
    EXPECT_NE(mii.fType & MFT_RADIOCHECK, 0u);
}

TEST(ContextMenu, DispatchCommandOnRadioGroupMemberUnchecksSiblingsInModelAndNativeMenu) {
    newui::MenuItem root;

    newui::MenuItem* smallItem = root.addChild(std::make_unique<newui::MenuItem>("Small"));
    smallItem->radioGroup = 0;
    smallItem->checked = true;

    newui::MenuItem* mediumItem = root.addChild(std::make_unique<newui::MenuItem>("Medium"));
    mediumItem->radioGroup = 0;

    newui::MenuItem* largeItem = root.addChild(std::make_unique<newui::MenuItem>("Large"));
    largeItem->radioGroup = 0;

    TestableContextMenu menu;
    menu.buildNativeMenu(root);

    // Build-time native state matches the model (smallItem started checked).
    EXPECT_NE(::GetMenuState(menu.handle(), smallItem->commandId(), MF_BYCOMMAND) & MF_CHECKED, 0u);

    menu.dispatchCommand(mediumItem->commandId());

    EXPECT_FALSE(smallItem->checked);
    EXPECT_TRUE(mediumItem->checked);
    EXPECT_FALSE(largeItem->checked);

    EXPECT_EQ(::GetMenuState(menu.handle(), smallItem->commandId(), MF_BYCOMMAND) & MF_CHECKED, 0u);
    EXPECT_NE(::GetMenuState(menu.handle(), mediumItem->commandId(), MF_BYCOMMAND) & MF_CHECKED, 0u);
    EXPECT_EQ(::GetMenuState(menu.handle(), largeItem->commandId(), MF_BYCOMMAND) & MF_CHECKED, 0u);
}

TEST(ContextMenu, SetCheckedAndSetEnabledUpdateModelAndNativeMenu) {
    newui::MenuItem root;
    newui::MenuItem* wrap = root.addChild(std::make_unique<newui::MenuItem>("Word Wrap"));

    TestableContextMenu menu;
    menu.buildNativeMenu(root);

    menu.setChecked(*wrap, true);
    EXPECT_TRUE(wrap->checked);
    EXPECT_NE(::GetMenuState(menu.handle(), wrap->commandId(), MF_BYCOMMAND) & MF_CHECKED, 0u);

    menu.setEnabled(*wrap, false);
    EXPECT_FALSE(wrap->state.isEnabled());
    EXPECT_NE(::GetMenuState(menu.handle(), wrap->commandId(), MF_BYCOMMAND) & MF_GRAYED, 0u);
}

// ---------------------------------------------------------------------------
// Owner-draw (MFT_OWNERDRAW) - DispatchMenuMeasureItem()/DispatchMenuDrawItem()
// free functions (menus.h) - not tied to any ContextMenu instance.
// ---------------------------------------------------------------------------

TEST(ContextMenu, OwnerDrawnItemGetsOwnerDrawFlagAndItemDataPointer) {
    newui::MenuItem root;
    newui::MenuItem* custom = root.addChild(std::make_unique<newui::MenuItem>("Custom"));
    custom->ownerDrawn = true;

    TestableContextMenu menu;
    menu.buildNativeMenu(root);

    MENUITEMINFOA mii = {};
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_FTYPE | MIIM_DATA;
    ASSERT_TRUE(::GetMenuItemInfoA(menu.handle(), custom->commandId(), FALSE, &mii));
    EXPECT_NE(mii.fType & MFT_OWNERDRAW, 0u);
    EXPECT_EQ(reinterpret_cast<newui::MenuItem*>(mii.dwItemData), custom);
}

TEST(MenuFreeFunctions, DispatchMeasureItemAndDrawItemRouteToTheRightMenuItem) {
    g_measureCallCount = 0;
    g_drawCallCount = 0;

    newui::MenuItem custom("Custom");
    custom.onMeasure.add(&RecordMeasure);
    custom.onDraw.add(&RecordDraw);

    MEASUREITEMSTRUCT mis = {};
    mis.CtlType = ODT_MENU;
    mis.itemData = reinterpret_cast<ULONG_PTR>(&custom);
    EXPECT_TRUE(newui::DispatchMenuMeasureItem(mis));
    EXPECT_EQ(g_measureCallCount, 1);
    EXPECT_EQ(mis.itemWidth, 123u);
    EXPECT_EQ(mis.itemHeight, 45u);
    // The default measured fallback (from text against the real menu
    // font) was non-empty before RecordMeasure() overwrote it.
    EXPECT_GT(g_lastMeasuredDefault.width, 0.0f);

    DRAWITEMSTRUCT dis = {};
    dis.CtlType = ODT_MENU;
    dis.itemData = reinterpret_cast<ULONG_PTR>(&custom);
    dis.rcItem = RECT{ 1, 2, 101, 22 };
    dis.itemAction = ODA_DRAWENTIRE;
    dis.itemState = ODS_SELECTED;
    EXPECT_TRUE(newui::DispatchMenuDrawItem(dis));
    EXPECT_EQ(g_drawCallCount, 1);
    
    // (0,0)-(width,height), local to DispatchMenuDrawItem()'s own private
    // per-item Image - not dis.rcItem's original DC-relative (1,2)
    // origin (see MenuItem::onDraw's doc comment, menus.h).
    EXPECT_FLOAT_EQ(g_lastDrawRect.left(), 0.0f);
    EXPECT_FLOAT_EQ(g_lastDrawRect.top(), 0.0f);
    EXPECT_FLOAT_EQ(g_lastDrawRect.size().width, 100.0f);
    EXPECT_FLOAT_EQ(g_lastDrawRect.size().height, 20.0f);
}

TEST(MenuFreeFunctions, DispatchMeasureItemReturnsFalseForNonMenuCtlType) {
    MEASUREITEMSTRUCT mis = {};
    mis.CtlType = ODT_BUTTON;
    EXPECT_FALSE(newui::DispatchMenuMeasureItem(mis));
}

TEST(MenuFreeFunctions, DispatchDrawItemReturnsFalseForNonMenuCtlType) {
    DRAWITEMSTRUCT dis = {};
    dis.CtlType = ODT_BUTTON;
    EXPECT_FALSE(newui::DispatchMenuDrawItem(dis));
}

// ---------------------------------------------------------------------------
// MenuBar - a SubView tree (no native menu of its own), one button per
// top-level MenuItem. Real click -> ContextMenu interaction needs a live
// window (a click's onMouseDown handler calls rootView()/windowHandle()),
// so it isn't covered here - same live-only gap as everywhere else in
// this file/project. Heap-allocated per the SubView convention (see
// View's class comment) - destroy()+delete at the end of each test that
// builds buttons (has children); plain delete is enough for a bare,
// childless MenuBar, same convention test_view.cpp already uses for a
// standalone SubView.
// ---------------------------------------------------------------------------

TEST(MenuBar, ConstructedVisibleWithThemedBackgroundStyleAndHorizontalLayout) {
    auto* bar = new newui::MenuBar();

    EXPECT_TRUE(bar->isVisible());
    EXPECT_NE(dynamic_cast<const newui::ThemedMenuBarBackgroundStyle*>(&bar->style()), nullptr);
    EXPECT_NE(dynamic_cast<const newui::FlexLayout*>(bar->layout()), nullptr);
    EXPECT_GT(bar->desiredSize().height, 0.0f);
    EXPECT_TRUE(bar->childViews().empty());

    delete bar;
}

TEST(MenuBar, SetMenuItemsBuildsOneThemedButtonPerTopLevelItem) {
    std::vector<std::unique_ptr<newui::MenuItem>> items;
    items.push_back(std::make_unique<newui::MenuItem>("File"));
    items.push_back(std::make_unique<newui::MenuItem>("Edit"));
    items.push_back(std::make_unique<newui::MenuItem>("View"));

    auto* bar = new newui::MenuBar();
    bar->setMenuItems(std::move(items));

    ASSERT_EQ(bar->root().children().size(), 3u);
    ASSERT_EQ(bar->childViews().size(), 3u);

    for (std::size_t i = 0; i < bar->childViews().size(); ++i) {
        const newui::SubView* button = bar->childViews()[i];
        EXPECT_TRUE(button->isVisible());
        EXPECT_NE(dynamic_cast<const newui::ThemedMenuBarItemStyle*>(&button->style()), nullptr);
        EXPECT_GT(button->desiredSize().width, 0.0f);
        EXPECT_EQ(button->name(), bar->root().children()[i]->text);
    }

    bar->destroy();
    delete bar;
}

TEST(MenuBar, SetMenuItemsCalledAgainReplacesButtons) {
    std::vector<std::unique_ptr<newui::MenuItem>> firstItems;
    firstItems.push_back(std::make_unique<newui::MenuItem>("File"));
    firstItems.push_back(std::make_unique<newui::MenuItem>("Edit"));

    auto* bar = new newui::MenuBar();
    bar->setMenuItems(std::move(firstItems));
    ASSERT_EQ(bar->childViews().size(), 2u);

    std::vector<std::unique_ptr<newui::MenuItem>> secondItems;
    secondItems.push_back(std::make_unique<newui::MenuItem>("Help"));

    bar->setMenuItems(std::move(secondItems));
    ASSERT_EQ(bar->root().children().size(), 1u);
    ASSERT_EQ(bar->childViews().size(), 1u);
    EXPECT_EQ(bar->childViews()[0]->name(), "Help");

    bar->destroy();
    delete bar;
}

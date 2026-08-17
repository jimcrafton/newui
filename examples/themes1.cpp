// A tour of newui's uxtheme-based ViewStyle classes (see viewstyle.h) -
// native Win32 chrome (buttons, checkboxes/radio buttons, edit fields,
// tooltips, group boxes, spin buttons, list/header/tree items, tabs,
// trackbars, progress bars, scrollbars, toolbar parts, a MenuBar) drawn via
// OpenThemeData()/DrawThemeBackground() instead of this toolkit's own
// hand-drawn chrome. Layout classes (see layout.h) are used throughout
// purely as scaffolding to arrange all of this into something legible -
// for a tour of Layout itself, with plain colored panels and no theme
// dependency, see layout1.cpp instead.
//   - The root view's own layout is a vertical FlexLayout with three rows:
//     a MenuBar (newui/menus.h - a custom-drawn, uxtheme-themed menu bar,
//     File/Edit/View/Help, not a native Win32 HMENU bar - see
//     AddDemoMenuBar()), then a toolbar strip (viewstyle.h's push/
//     drop-down/split-button/separator/chevron theme parts - see
//     AddToolbarDemo()), then mainRow filling everything left over.
//   - mainRow's own horizontal FlexLayout - a fixed-width sidebar plus
//     four flexible content panels sharing the leftover width by weight,
//     the last (content4) a real ScrollView (controls.h).
//   - AnchorLayout, nested inside one of those content panels, pins a
//     small badge to its top-right corner - showing that a Layout
//     arranges whatever View it's attached to, not just the root, and
//     composes naturally with whatever layout the parent itself uses.
//   - GridLayout, nested inside a different content panel, lays out a
//     small 2-column "form" (an Auto-sized label column next to a
//     Star-sized input column) - see AddGridDemo() below.
//   - The sidebar hosts a native-themed button and checkbox
//     (ThemedButtonStyle/ThemedCheckBoxStyle - see viewstyle.h), stacked
//     via its own nested vertical FlexLayout, to show a themed style
//     composing with Layout the same way the hand-drawn styles do.
//   - The themed button shows a hand cursor and content2 shows a
//     crosshair while hovered - see View::setCursor()/View::cursor()/
//     CursorKind (cursor.h). content1 shows a small custom red-dot cursor
//     loaded from a real PNG file at runtime - see cursor().setPath()
//     and WriteDotCursorPNG() below. Move the mouse between them and the
//     rest of the window (default arrow) to see it change live.
// Every panel is otherwise empty (just a background/border color) so
// the arrangement is the only thing on screen - resize the window to
// see FlexLayout re-flow it live (SubView::setBounds()/
// RootView::setBounds() call View::updateLayout() automatically; see
// view.h).



#include "newui/newui.h"
#include "newui/application.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/layout.h"
#include "newui/color.h"
#include "newui/cursor.h"
#include "newui/menus.h"
#include "newui/tabcontrol.h"
#include "newui/uicolormanager.h"
#include "newui/keyboard_constants.h"
#include "newui/font.h"
#include "newui/fontmanager.h"
#include <blend2d/blend2d.h>

#include <iostream>
#include <memory>
#include <string>

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());

    return newui::SyncReturn::Handled;
}

// --- Application-level theme/session events (application.h) -------------
// Console output shows these firing (and with what data) directly -
// useful on its own, since Frame's WM_SETTINGCHANGE handling only acts on
// "ImmersiveColorSet" specifically (see Frame::handleMessage()), so this
// is the easiest way to confirm what Windows is actually sending. But
// this file's own controls mostly won't visibly change even so: none of
// the classic Win32 common-control theme parts demonstrated here (BUTTON,
// EDIT, TRACKBAR, PROGRESS, SCROLLBAR, TAB, HEADER, TOOLBAR, REBAR,
// TOOLTIP) have a distinct dark-mode visual DrawThemeBackground() picks
// up automatically - Windows' light/dark toggle mainly affects window
// chrome and a few shell surfaces via undocumented APIs
// (SetWindowTheme(hwnd, L"DarkMode_Explorer", ...) + AllowDarkModeForWindow)
// that no app gets without explicitly opting in, which this one doesn't.
// RootView::refreshThemes() still runs on WM_THEMECHANGED/
// WM_DWMCOLORIZATIONCOLORCHANGED/"ImmersiveColorSet" (dropping cached
// HTHEMEs and redrawing), and ThemedViewStyle::paint() applies a "fake"
// dark-mode approximation (an HSL lightness invert - see its own doc
// comment in viewstyle.cpp) to whatever those controls draw, so their
// look does shift, just not to a real dark asset.
//
// root's own background, below, is different: it's this app's own color,
// not native theme output, so ApplyThemeColors() can make it follow Dark
// mode for real via UIColorManager (uicolormanager.h) - no approximation
// needed for anything this toolkit actually controls itself.

newui::RootView* g_demoRootView = nullptr;

// AddToolbarDemo()'s own toolbarRow - a plain SubView with a hand-set
// backgroundFill (not a ThemedViewStyle), so - same as g_demoRootView -
// it needs an explicit UIColorManager-driven update here rather than
// picking up ThemedViewStyle::paint()'s pixel-invert fake automatically.
newui::SubView* g_demoToolbarRow = nullptr;

void ApplyThemeColors() {
    if (g_demoRootView != nullptr) {
        newui::Color background = newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground);
        g_demoRootView->style().setBackgroundColor( background);
        g_demoRootView->markDirty();
    }

    if (g_demoToolbarRow != nullptr) {
        newui::Color background = newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground);
        g_demoToolbarRow->style().setBackgroundColor( background );
        g_demoToolbarRow->style().markDirty();
    }
}

newui::SyncReturn ThemeChanged(newui::Application&) {
    printf("Application::onThemeChanged fired\n");
    fflush(stdout);
    ApplyThemeColors();
    return newui::SyncReturn::Handled;
}

newui::SyncReturn ColorizationColorChanged(newui::Application&, newui::Color color, bool isOpaqueBlend) {
    printf("Application::onColorizationColorChanged fired: color=%s isOpaqueBlend=%s\n",
        color.toString().c_str(), isOpaqueBlend ? "true" : "false");
    fflush(stdout);
    ApplyThemeColors();
    return newui::SyncReturn::Handled;
}

newui::SyncReturn SettingChanged(newui::Application&, std::uint32_t action, std::string settingName) {
    printf("Application::onSettingChange fired: action=%u settingName=\"%s\"\n",
        action, settingName.c_str());
    fflush(stdout);
    if (settingName == "ImmersiveColorSet") {
        ApplyThemeColors();
    }
    return newui::SyncReturn::Handled;
}

// --- MenuBar demo (newui/menus.h) ---------------------------------------
// Delegate callbacks are plain function pointers (no capturing lambdas -
// see delegate.h), so the frame this needs to reach back into (Exit) is
// stashed at file scope, set once in main() - same convention
// FrameClosed() above already follows for frame. MenuBar/ContextMenu need
// no such state for click routing - see menus.h.

newui::Frame* g_demoFrame = nullptr;

newui::SyncReturn MenuItemClicked(newui::MenuItem& item) {
    printf("Menu item clicked: \"%s\" (command id %u)\n", item.text.c_str(), item.commandId());
    fflush(stdout);  // printf alone can sit in a fully-buffered console until exit
    return newui::SyncReturn::Handled;
}

newui::SyncReturn ExitClicked(newui::MenuItem& item) {
    printf("Menu item clicked: \"%s\" - closing the window\n", item.text.c_str());
    fflush(stdout);
    if (g_demoFrame != nullptr && g_demoFrame->frameHandle() != nullptr) {
        ::SendMessage(g_demoFrame->frameHandle(), WM_CLOSE, 0, 0);
    }
    return newui::SyncReturn::Handled;
}

// Ctrl+Shift+F12 hotkey -> Frame::renderAllViewsToFile() (frame.h) - wired
// on root's own onKeyDown (View::onKeyDown, keyboard_constants.h's
// VirtualKeyCode/KeyboardMasks) rather than any per-menu-item
// shortcutText (those are display-only labels next to a MenuItem, e.g.
// Part 16's "Ctrl+S" on File > Save - real key delivery, nowhere in this
// example yet). A 2-modifier combo (not bare F12) specifically to
// exercise keyMask's kmCtrl/kmShift bits - real-world regression-tested
// here: rootview.cpp's WM_KEYDOWN handling used to run
// translateKeyEventInfo()'s already-correct keyMask back through
// translateKeyMask() a second time (that function expects a raw Win32
// MK_CONTROL/MK_SHIFT mouse-message mask, not newui's own kmCtrl/kmShift
// encoding), which silently reported Ctrl as Shift and dropped Ctrl
// entirely - fixed alongside this demo. Fires here because nothing in
// this demo ever calls setFocusedSubView() / clicks a focusable SubView,
// so RootView::keyEvent() keeps routing to root's own onKeyDown instead
// of some focused child's - see its own focusedSubView_ branch in
// rootview.cpp.
newui::SyncReturn ScreenshotHotkey(newui::View& /*sender*/, std::uint32_t keyMask, int /*keyCharVal*/, int /*repeatCount*/, std::uint32_t VKeyCode) {
    constexpr std::uint32_t kRequiredMods = newui::kmCtrl | newui::kmShift;

    printf("keyMask: %d,  VKeyCode: %d\n", keyMask, VKeyCode);
    

    if (VKeyCode != newui::vkF12 || (keyMask & kRequiredMods) != kRequiredMods || g_demoFrame == nullptr) {
        return newui::SyncReturn::Ignored;
    }

    const char* path = "themes1_screenshot.png";
    bool ok = g_demoFrame->renderAllViewsToFile(path);
    printf("Ctrl+Shift+F12: renderAllViewsToFile(\"%s\") -> %s\n", path, ok ? "ok" : "failed");
    fflush(stdout);
    return newui::SyncReturn::Handled;
}

// A plain (non-radio) checkable item toggles itself explicitly - a
// ContextMenu::setChecked(item, !item.checked) call would need the
// ContextMenu that showed it, which is already gone (destroyed) by the
// time onClick fires from inside its own show() - so a checkable item's
// own handler just flips the model bool directly; the *next* time this
// item's dropdown is shown, a fresh ContextMenu build reads item.checked
// and renders the checkmark correctly either way.
newui::SyncReturn WordWrapToggled(newui::MenuItem& item) {
    item.checked = !item.checked;
    printf("Word Wrap: %s\n", item.checked ? "on" : "off");
    fflush(stdout);
    return newui::SyncReturn::Handled;
}

// A colored-icon demo item - originally MFT_OWNERDRAW (a hand-drawn
// swatch + label via BLContext), dropped after confirming live that any
// owner-drawn item forces the *whole* popup back to legacy, non-dark
// chrome (see MenuItem::ownerDrawn's own doc comment, menus.h) - not
// worth it just for a demo swatch. This is the native alternative: a
// plain MFT_STRING item with a MIIM_BITMAP icon (MenuItem::bitmap) next
// to it, so the popup stays on the real dark-themed native path
// end-to-end. Built once (small, solid-color 16x16 DIB) and kept alive
// for the process's lifetime - a menu's own HBITMAP is borrowed, not
// owned (see MenuItem::bitmap's doc comment), so this has to outlive
// every ContextMenu::show() that might display it.
HBITMAP CreateSwatchBitmap(int size, COLORREF color) {
    HDC screenDC = ::GetDC(nullptr);
    HDC memDC = ::CreateCompatibleDC(screenDC);
    HBITMAP bitmap = ::CreateCompatibleBitmap(screenDC, size, size);
    HBITMAP oldBitmap = static_cast<HBITMAP>(::SelectObject(memDC, bitmap));

    RECT rc = { 0, 0, size, size };
    HBRUSH brush = ::CreateSolidBrush(color);
    ::FillRect(memDC, &rc, brush);
    ::DeleteObject(brush);

    ::SelectObject(memDC, oldBitmap);
    ::DeleteDC(memDC);
    ::ReleaseDC(nullptr, screenDC);
    return bitmap;
}

// Builds the demo File/Edit/View/Help menu tree and attaches a MenuBar to
// root - ordinary SubView tree from here on, so this needs no live HWND
// or Frame::initialize()/app.run() sequencing at all (unlike the old
// native-HMENU version) - it can run any time before or after other
// addChild() calls, exactly like every other panel in this demo.
void AddDemoMenuBar(newui::RootView& root, HBITMAP fancyItemBitmap) {
    std::vector<std::unique_ptr<newui::MenuItem>> menuItems;

    auto fileMenu = std::make_unique<newui::MenuItem>("File");
    fileMenu->addChild(std::make_unique<newui::MenuItem>("New"))->onClick.add(&MenuItemClicked);
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Open"))->onClick.add(&MenuItemClicked);
    newui::MenuItem* saveItem = fileMenu->addChild(std::make_unique<newui::MenuItem>("Save"));
    saveItem->shortcutText = "Ctrl+S";
    saveItem->onClick.add(&MenuItemClicked);
    fileMenu->addChild(newui::MenuItem::Separator());
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Exit"))->onClick.add(&ExitClicked);
    menuItems.push_back(std::move(fileMenu));

    auto editMenu = std::make_unique<newui::MenuItem>("Edit");
    editMenu->addChild(std::make_unique<newui::MenuItem>("Word Wrap"))->onClick.add(&WordWrapToggled);
    menuItems.push_back(std::move(editMenu));

    // A 3-item radio group - ContextMenu::dispatchCommand() handles the
    // mutual-exclusion (unchecking siblings) automatically, see menus.h.
    auto viewMenu = std::make_unique<newui::MenuItem>("View");
    for (const char* label : { "Small", "Medium", "Large" }) {
        auto sizeItem = std::make_unique<newui::MenuItem>(label);
        sizeItem->radioGroup = 0;
        sizeItem->checked = (std::string(label) == "Medium");
        sizeItem->onClick.add(&MenuItemClicked);
        viewMenu->addChild(std::move(sizeItem));
    }
    menuItems.push_back(std::move(viewMenu));

    auto helpMenu = std::make_unique<newui::MenuItem>("Help");
    auto fancyItem = std::make_unique<newui::MenuItem>("Fancy (Native Icon)");
    fancyItem->bitmap = fancyItemBitmap;
    fancyItem->onClick.add(&MenuItemClicked);
    helpMenu->addChild(std::move(fancyItem));
    menuItems.push_back(std::move(helpMenu));

    auto* menuBar = new newui::MenuBar();
    menuBar->setMenuItems(std::move(menuItems));
    menuBar->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    root.addChild(menuBar);
}

// Toolbar batch demo (viewstyle.h) - a horizontal strip of native-themed
// toolbar chrome: a plain push button, a checked (toggled-on) push
// button, a separator, a drop-down button (its chrome and arrow-glyph
// parts are separate theme parts, shown here as two adjacent Views - see
// ThemedToolbarDropDownButtonStyle's class comment), a split button (same
// two-part shape - main clickable face + its own dropdown-arrow part),
// another separator, and an overflow chevron (REBAR/RP_CHEVRON, not
// actually a TOOLBAR part - see ThemedRebarChevronStyle). Same "each part
// is its own SubView, no real click/dropdown behavior wired up" scope as
// everywhere else in this file - a visual tour of the theme parts, not a
// functioning toolbar.
void AddToolbarDemo(newui::RootView& root) {
    auto* toolbarRow = new newui::SubView();
    toolbarRow->setName("toolbarRow");
    toolbarRow->setVisible(true);
    toolbarRow->style().setBackgroundColor(
        newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground) );
    g_demoToolbarRow = toolbarRow;
    toolbarRow->setDesiredSize(newui::Size(0.0f, 32.0f));
    toolbarRow->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    auto toolbarLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    toolbarLayout->setSpacing(2.0f);
    toolbarLayout->setPadding(4.0f);
    toolbarLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Center);
    toolbarRow->setLayout(std::move(toolbarLayout));
    root.addChild(toolbarRow);

    auto* pushButton = new newui::SubView();
    pushButton->setName("toolbarPushButton");
    pushButton->setVisible(true);
    pushButton->setStyle(std::make_unique<newui::ThemedToolbarButtonStyle>());
    pushButton->setDesiredSize(newui::Size(28.0f, 24.0f));
    toolbarRow->addChild(pushButton);

    auto* checkedButton = new newui::SubView();
    checkedButton->setName("toolbarCheckedButton");
    checkedButton->setVisible(true);
    auto checkedButtonStyle = std::make_unique<newui::ThemedToolbarButtonStyle>();
    checkedButtonStyle->checked = true;
    checkedButton->setStyle(std::move(checkedButtonStyle));
    checkedButton->setDesiredSize(newui::Size(28.0f, 24.0f));
    toolbarRow->addChild(checkedButton);

    auto MakeSeparator = [&](const char* name) {
        auto* separator = new newui::SubView();
        separator->setName(name);
        separator->setVisible(true);
        separator->setStyle(std::make_unique<newui::ThemedToolbarSeparatorStyle>());
        separator->setDesiredSize(newui::Size(6.0f, 24.0f));
        toolbarRow->addChild(separator);
    };
    MakeSeparator("toolbarSeparator1");

    auto* dropDownButton = new newui::SubView();
    dropDownButton->setName("toolbarDropDownButton");
    dropDownButton->setVisible(true);
    dropDownButton->setStyle(std::make_unique<newui::ThemedToolbarDropDownButtonStyle>());
    dropDownButton->setDesiredSize(newui::Size(28.0f, 24.0f));
    toolbarRow->addChild(dropDownButton);

    auto* dropDownGlyph = new newui::SubView();
    dropDownGlyph->setName("toolbarDropDownGlyph");
    dropDownGlyph->setVisible(true);
    dropDownGlyph->setStyle(std::make_unique<newui::ThemedToolbarDropDownButtonGlyphStyle>());
    dropDownGlyph->setDesiredSize(newui::Size(12.0f, 24.0f));
    toolbarRow->addChild(dropDownGlyph);

    MakeSeparator("toolbarSeparator2");

    auto* splitButton = new newui::SubView();
    splitButton->setName("toolbarSplitButton");
    splitButton->setVisible(true);
    splitButton->setStyle(std::make_unique<newui::ThemedToolbarSplitButtonStyle>());
    splitButton->setDesiredSize(newui::Size(28.0f, 24.0f));
    toolbarRow->addChild(splitButton);

    auto* splitButtonDropDown = new newui::SubView();
    splitButtonDropDown->setName("toolbarSplitButtonDropDown");
    splitButtonDropDown->setVisible(true);
    splitButtonDropDown->setStyle(std::make_unique<newui::ThemedToolbarSplitButtonDropDownStyle>());
    splitButtonDropDown->setDesiredSize(newui::Size(14.0f, 24.0f));
    toolbarRow->addChild(splitButtonDropDown);

    MakeSeparator("toolbarSeparator3");

    auto* chevron = new newui::SubView();
    chevron->setName("toolbarChevron");
    chevron->setVisible(true);
    chevron->setStyle(std::make_unique<newui::ThemedRebarChevronStyle>());
    chevron->setDesiredSize(newui::Size(16.0f, 24.0f));
    toolbarRow->addChild(chevron);
}

// Every panel in this demo is otherwise empty - a flat background fill
// plus a contrasting border is the only thing that makes the
// arrangement visible - so this is the one place colors are chosen,
// rather than repeating the same four lines at each call site.
newui::SubView* MakePanel(const std::string& name, const std::string& backgroundColorName,
        const std::string& borderColorName, float borderWidth = 3.0f) {
    auto* panel = new newui::SubView();
    panel->setName(name);
    panel->setVisible(true);
    panel->style().setBackgroundColor( newui::Color::fromName(backgroundColorName) );
    panel->style().borderFill = newui::Color::fromName(borderColorName).toBLRgba32();
    panel->style().borderWidth = borderWidth;
    return panel;
}

// Draws a small solid red dot (alpha falls off to fully transparent past
// its radius) into a real 32x32 PNG on disk, at path - demo data for
// View::cursor().setPath() (view.h/cursor.h), which needs an actual
// image file to load. Generated at runtime rather than shipped as a
// checked-in asset, so this example stays self-contained; a real app
// would just ship a hand-authored cursor PNG under Resources/Cursors/
// instead and reference it by name alone (see Bundle, bundle.h -
// loadCursorFromFile() already falls back to that location).
void WriteDotCursorPNG(const std::string& path) {
    const int size = 32;
    BLImage image(size, size, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_comp_op(BL_COMP_OP_SRC_COPY);  // fully-transparent background, not black
    ctx.fill_all(BLRgba32(0, 0, 0, 0));
    ctx.set_comp_op(BL_COMP_OP_SRC_OVER);
    ctx.set_fill_style(BLRgba32(220, 20, 20, 255));
    ctx.fill_circle(size / 2.0, size / 2.0, size / 2.0 - 2.0);
    ctx.end();
    image.write_to_file(path.c_str());
}

// Builds a small 2-column "form" grid as parent's own Layout - column 0
// is Auto-sized to whichever label cell's desiredSize() is widest (see
// View::setDesiredSize()), column 1 is a single Star track filling
// whatever's left. Both cells default to CrossAxisAlignment::Stretch, so
// each label fills the *resolved* Auto column width, not just its own
// desiredSize() - label2 (below) is what actually drives that width.
void AddGridDemo(newui::SubView* parent) {
    auto grid = std::make_unique<newui::GridLayout>();
    grid->addAutoColumn();
    grid->addStarColumn();
    grid->addFixedRow(28.0f);
    grid->addFixedRow(28.0f);
    grid->setColumnSpacing(6.0f);
    grid->setRowSpacing(6.0f);
    parent->setLayout(std::move(grid));

    auto* label1 = MakePanel("label1", "gainsboro", "gray", 1.0f);
    label1->setDesiredSize(newui::Size(60.0f, 20.0f));
    label1->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    parent->addChild(label1);

    auto* input1 = MakePanel("input1", "white", "gray", 1.0f);
    input1->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 1));
    parent->addChild(input1);

    // Wider than label1 - grows the Auto column to fit this one instead.
    auto* label2 = MakePanel("label2", "gainsboro", "gray", 1.0f);
    label2->setDesiredSize(newui::Size(90.0f, 20.0f));
    label2->setLayoutParams(std::make_unique<newui::GridLayoutParams>(1, 0));
    parent->addChild(label2);

    auto* input2 = MakePanel("input2", "white", "gray", 1.0f);
    input2->setLayoutParams(std::make_unique<newui::GridLayoutParams>(1, 1));
    parent->addChild(input2);
}

// TabControl demo (tabcontrol.h) - a real, clickable tabbed control (not
// just the loose individual ThemedTabItemStyle/ThemedTabPaneStyle parts
// already shown in content2's own themed-controls demo above) - 3 tabs,
// each a plain colored page, switched by clicking. parent's own layout
// must already be (or about to become) an AnchorLayout, since this fills
// parent via AnchorLayoutParams(Left|Top|Right|Bottom) - same "dock to
// fill" shape the badge demo below already uses for content1.
void AddTabControlDemo(newui::SubView* parent) {
    auto* tabs = new newui::TabControl();
    auto tabsParams = std::make_unique<newui::AnchorLayoutParams>(
        newui::Anchor::Left | newui::Anchor::Top | newui::Anchor::Right | newui::Anchor::Bottom);
    tabsParams->leftMargin = 8.0f;
    tabsParams->topMargin = 8.0f;
    tabsParams->rightMargin = 8.0f;
    tabsParams->bottomMargin = 8.0f;
    tabs->setLayoutParams(std::move(tabsParams));
    parent->addChild(tabs);

    tabs->addTab("Red", MakePanel("tabPageRed", "indianred", "darkred"));
    tabs->addTab("Green", MakePanel("tabPageGreen", "mediumseagreen", "darkgreen"));
    tabs->addTab("Blue", MakePanel("tabPageBlue", "cornflowerblue", "navy"));
}

int main() {

    std::cout << "newui " << newui::version() << " - themes example\n";
    std::cout << "A tour of newui's uxtheme-based ViewStyle classes: a MenuBar, a toolbar strip,\n";
    std::cout << "a themed button/checkbox/radio button/edit/tooltip/group box/spin buttons in the\n";
    std::cout << "sidebar, list/header/tree items, tabs, a trackbar, a progress bar (normal +\n";
    std::cout << "paused state), and a scrollbar in content2,\n";
    std::cout << "and a real clickable TabControl in content1. Layout classes just arrange all of\n";
    std::cout << "this - see layout1.cpp for a tour of Layout itself.\n";

    newui::Frame frame;
    g_demoFrame = &frame;  // ExitClicked()/ScreenshotHotkey() both reach the frame through this

    newui::Application& app = newui::Application::instance();
    app.setName("themes1");
    app.setFrame(&frame);

    frame.setTitle("Themes Example");
    frame.setBounds(newui::Rect(10, 10, 900, 500));
    frame.onClosed += FrameClosed;

    // See ThemeChanged()/ColorizationColorChanged()/SettingChanged()'s
    // own comment above.
    app.onThemeChanged += &ThemeChanged;
    app.onColorizationColorChanged += &ColorizationColorChanged;
    app.onSettingChange += &SettingChanged;

    newui::RootView& root = frame.getView();
    g_demoRootView = &root;
    root.onKeyDown += &ScreenshotHotkey;
    // Sets root's background from whatever Light/Dark mode is active
    // right now (see ApplyThemeColors() above) - so even a fresh launch
    // while already in Dark mode starts out correct, not just a live
    // toggle while running.
    ApplyThemeColors();

    // Outer vertical split: the MenuBar row on top (its own fixed
    // desiredSize height - see menus.cpp), then mainRow (weight 1.0)
    // filling everything left over - what used to be root's own direct
    // horizontal row (sidebar + content1/2/3), now nested one level so it
    // can share root's vertical space with the menu bar above it.
    root.setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

    // Outlives every ContextMenu::show() that might display the Help
    // menu's Fancy item - see MenuItem::bitmap's own doc comment
    // (menus.h) on why this can't be a transient, per-show() resource.
    HBITMAP fancyItemBitmap = CreateSwatchBitmap(16, RGB(220, 80, 40));

    AddDemoMenuBar(root, fancyItemBitmap);
    AddToolbarDemo(root);

    auto* mainRow = new newui::SubView();
    mainRow->setName("mainRow");
    mainRow->setVisible(true);
    mainRow->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    // Spacing between panels, padding from the window's own edges - same
    // values the outer root layout used to have directly.
    auto mainRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    mainRowLayout->setSpacing(12.0f);
    mainRowLayout->setPadding(16.0f);
    mainRow->setLayout(std::move(mainRowLayout));
    root.addChild(mainRow);

    // Sidebar: fixed width (its "natural" size - see FlexLayoutParams'
    // weight comment), full height via the default
    // CrossAxisAlignment::Stretch. Bounds have to be set before
    // addChild() - that's what triggers the first arrange() pass, and
    // FlexLayout reads a weight-0 child's starting desiredSize() (which
    // falls back to bounds size by default - see View::desiredSize()) as
    // its natural main-axis size.
    auto* sidebar = MakePanel("sidebar", "steelblue", "navy");
    sidebar->setBounds(newui::Rect(0, 0, 160, 0));
    mainRow->addChild(sidebar);

    // A couple of real Win32 controls (drawn via uxtheme, not this
    // toolkit's own hand-drawn chrome) stacked in the sidebar via its own
    // nested vertical FlexLayout. Left-aligned rather than the default
    // Stretch - DrawThemeBackground() stretches a native part's artwork
    // to fill whatever rect it's given, and a BP_CHECKBOX/BP_PUSHBUTTON
    // part stretched to the sidebar's full width would look distorted -
    // so each child gets a fixed desiredSize() and stays natural-width.
    auto sidebarLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    sidebarLayout->setSpacing(8.0f);
    sidebarLayout->setPadding(12.0f);
    sidebar->setLayout(std::move(sidebarLayout));

    auto* themedButton = new newui::SubView();
    themedButton->setName("themedButton");
    themedButton->setVisible(true);
    themedButton->setStyle(std::make_unique<newui::ThemedButtonStyle>());
    themedButton->setDesiredSize(newui::Size(120.0f, 28.0f));
    themedButton->setCursor(newui::Cursor(newui::CursorKind::Hand));
    auto themedButtonParams = std::make_unique<newui::FlexLayoutParams>();
    themedButtonParams->crossAxisAlignment = newui::CrossAxisAlignment::Start;
    themedButton->setLayoutParams(std::move(themedButtonParams));
    sidebar->addChild(themedButton);

    auto* themedCheckBox = new newui::SubView();
    themedCheckBox->setName("themedCheckBox");
    themedCheckBox->setVisible(true);
    auto themedCheckBoxStyle = std::make_unique<newui::ThemedCheckBoxStyle>();
    themedCheckBoxStyle->checked = true;
    themedCheckBox->setStyle(std::move(themedCheckBoxStyle));
    themedCheckBox->setDesiredSize(newui::Size(20.0f, 20.0f));
    auto themedCheckBoxParams = std::make_unique<newui::FlexLayoutParams>();
    themedCheckBoxParams->crossAxisAlignment = newui::CrossAxisAlignment::Start;
    themedCheckBox->setLayoutParams(std::move(themedCheckBoxParams));
    sidebar->addChild(themedCheckBox);

    // The 8 new "batch 1" themed styles below (uxtheme parts that fit
    // ThemedViewStyle's simple partId()/stateId() shape directly - see
    // HANDOFF.md) - stacked in the sidebar purely so all of them get a
    // real live-window paint() pass to eyeball, same as the button/
    // checkbox above; not otherwise functionally wired up (no click
    // toggling, no real up/down spinner logic, ...).
    auto* themedRadioButton = new newui::SubView();
    themedRadioButton->setName("themedRadioButton");
    themedRadioButton->setVisible(true);
    auto themedRadioButtonStyle = std::make_unique<newui::ThemedRadioButtonStyle>();
    themedRadioButtonStyle->checked = true;
    themedRadioButton->setStyle(std::move(themedRadioButtonStyle));
    themedRadioButton->setDesiredSize(newui::Size(20.0f, 20.0f));
    auto themedRadioButtonParams = std::make_unique<newui::FlexLayoutParams>();
    themedRadioButtonParams->crossAxisAlignment = newui::CrossAxisAlignment::Start;
    themedRadioButton->setLayoutParams(std::move(themedRadioButtonParams));
    sidebar->addChild(themedRadioButton);

    auto* themedToolbarButton = new newui::SubView();
    themedToolbarButton->setName("themedToolbarButton");
    themedToolbarButton->setVisible(true);
    auto themedToolbarButtonStyle = std::make_unique<newui::ThemedToolbarButtonStyle>();
    themedToolbarButton->setStyle(std::move(themedToolbarButtonStyle));
    themedToolbarButton->setDesiredSize(newui::Size(32.0f, 28.0f));
    auto themedToolbarButtonParams = std::make_unique<newui::FlexLayoutParams>();
    themedToolbarButtonParams->crossAxisAlignment = newui::CrossAxisAlignment::Start;
    themedToolbarButton->setLayoutParams(std::move(themedToolbarButtonParams));
    sidebar->addChild(themedToolbarButton);

    auto* themedEdit = new newui::SubView();
    themedEdit->setName("themedEdit");
    themedEdit->setVisible(true);
    themedEdit->setStyle(std::make_unique<newui::ThemedEditStyle>());
    themedEdit->setDesiredSize(newui::Size(136.0f, 22.0f));
    sidebar->addChild(themedEdit);

    auto* themedTooltip = new newui::SubView();
    themedTooltip->setName("themedTooltip");
    themedTooltip->setVisible(true);
    themedTooltip->setStyle(std::make_unique<newui::ThemedTooltipStyle>());
    themedTooltip->setDesiredSize(newui::Size(136.0f, 24.0f));
    sidebar->addChild(themedTooltip);

    auto* themedStatusPane = new newui::SubView();
    themedStatusPane->setName("themedStatusPane");
    themedStatusPane->setVisible(true);
    themedStatusPane->setStyle(std::make_unique<newui::ThemedStatusPaneStyle>());
    themedStatusPane->setDesiredSize(newui::Size(136.0f, 22.0f));
    // Renders as just a one-sided divider line, not a full box border -
    // that's real, correct uxtheme behavior for a standalone SP_PANE, not
    // a bug - see ThemedStatusPaneStyle's comment in viewstyle.h.
    sidebar->addChild(themedStatusPane);

    auto* themedGroupBox = new newui::SubView();
    themedGroupBox->setName("themedGroupBox");
    themedGroupBox->setVisible(true);
    themedGroupBox->setStyle(std::make_unique<newui::ThemedGroupBoxStyle>());
    themedGroupBox->setDesiredSize(newui::Size(136.0f, 40.0f));
    sidebar->addChild(themedGroupBox);

    // Up/down spin arrows side by side via their own tiny nested
    // horizontal FlexLayout, rather than a container of their own -
    // reuses sidebar's own vertical stacking for the row itself.
    auto* themedSpinRow = new newui::SubView();
    themedSpinRow->setName("themedSpinRow");
    themedSpinRow->setVisible(true);
    themedSpinRow->setDesiredSize(newui::Size(136.0f, 20.0f));
    auto spinRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    spinRowLayout->setSpacing(4.0f);
    themedSpinRow->setLayout(std::move(spinRowLayout));
    sidebar->addChild(themedSpinRow);

    auto* themedSpinUp = new newui::SubView();
    themedSpinUp->setName("themedSpinUp");
    themedSpinUp->setVisible(true);
    themedSpinUp->setStyle(std::make_unique<newui::ThemedSpinButtonStyle>());
    themedSpinUp->setDesiredSize(newui::Size(16.0f, 20.0f));
    themedSpinRow->addChild(themedSpinUp);

    auto* themedSpinDown = new newui::SubView();
    themedSpinDown->setName("themedSpinDown");
    themedSpinDown->setVisible(true);
    auto themedSpinDownStyle = std::make_unique<newui::ThemedSpinButtonStyle>();
    themedSpinDownStyle->isUpButton = false;
    themedSpinDown->setStyle(std::move(themedSpinDownStyle));
    themedSpinDown->setDesiredSize(newui::Size(16.0f, 20.0f));
    themedSpinRow->addChild(themedSpinDown);

    // Three flexible panels sharing the row's leftover width by weight
    // (CSS flex-grow) - content2 is twice as wide as content1/content3
    // since its weight is double theirs.
    auto* content1 = MakePanel("content1", "lightcoral", "darkred");
    content1->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    // View::cursor().setPath() demo - a real 32x32 PNG with alpha,
    // generated at runtime by WriteDotCursorPNG() above (see its comment).
    WriteDotCursorPNG("themes1_dot_cursor.png");
    if (!content1->cursor().setPath("themes1_dot_cursor.png")) {
        std::cerr << "cursor().setPath() failed for content1's cursor\n";
    }
    mainRow->addChild(content1);

    auto* content2 = MakePanel("content2", "khaki", "darkgoldenrod");
    content2->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(2.0f));
    content2->setCursor(newui::Cursor(newui::CursorKind::Cross));
    mainRow->addChild(content2);

    // Batch-2 themed uxtheme styles demo - stacked in content2 via its
    // own nested vertical FlexLayout, same "needs a real live-window
    // paint() pass to eyeball" reasoning as batch 1's sidebar demo.
    auto content2Layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    content2Layout->setSpacing(6.0f);
    content2Layout->setPadding(12.0f);
    content2->setLayout(std::move(content2Layout));

    auto* themedListItem = new newui::SubView();
    themedListItem->setName("themedListItem");
    themedListItem->setVisible(true);
    auto themedListItemStyle = std::make_unique<newui::ThemedListItemStyle>();
    themedListItemStyle->selected = true;
    themedListItem->setStyle(std::move(themedListItemStyle));
    themedListItem->setDesiredSize(newui::Size(180.0f, 20.0f));
    content2->addChild(themedListItem);

    // Header item + sort arrow side by side.
    auto* headerRow = new newui::SubView();
    headerRow->setName("headerRow");
    headerRow->setVisible(true);
    headerRow->setDesiredSize(newui::Size(180.0f, 20.0f));
    auto headerRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    headerRowLayout->setSpacing(2.0f);
    headerRow->setLayout(std::move(headerRowLayout));
    content2->addChild(headerRow);

    auto* themedHeaderItem = new newui::SubView();
    themedHeaderItem->setName("themedHeaderItem");
    themedHeaderItem->setVisible(true);
    auto themedHeaderItemStyle = std::make_unique<newui::ThemedHeaderItemStyle>();
    themedHeaderItemStyle->sorted = true;
    themedHeaderItem->setStyle(std::move(themedHeaderItemStyle));
    themedHeaderItem->setDesiredSize(newui::Size(150.0f, 20.0f));
    headerRow->addChild(themedHeaderItem);

    auto* themedHeaderSortArrow = new newui::SubView();
    themedHeaderSortArrow->setName("themedHeaderSortArrow");
    themedHeaderSortArrow->setVisible(true);
    themedHeaderSortArrow->setStyle(std::make_unique<newui::ThemedHeaderSortArrowStyle>());
    themedHeaderSortArrow->setDesiredSize(newui::Size(20.0f, 20.0f));
    headerRow->addChild(themedHeaderSortArrow);

    // Tree item + expand glyph side by side.
    auto* treeRow = new newui::SubView();
    treeRow->setName("treeRow");
    treeRow->setVisible(true);
    treeRow->setDesiredSize(newui::Size(180.0f, 18.0f));
    auto treeRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    treeRowLayout->setSpacing(2.0f);
    treeRow->setLayout(std::move(treeRowLayout));
    content2->addChild(treeRow);

    auto* themedTreeGlyph = new newui::SubView();
    themedTreeGlyph->setName("themedTreeGlyph");
    themedTreeGlyph->setVisible(true);
    auto themedTreeGlyphStyle = std::make_unique<newui::ThemedTreeGlyphStyle>();
    themedTreeGlyphStyle->expanded = true;
    themedTreeGlyph->setStyle(std::move(themedTreeGlyphStyle));
    themedTreeGlyph->setDesiredSize(newui::Size(16.0f, 16.0f));
    treeRow->addChild(themedTreeGlyph);

    auto* themedTreeItem = new newui::SubView();
    themedTreeItem->setName("themedTreeItem");
    themedTreeItem->setVisible(true);
    themedTreeItem->setStyle(std::make_unique<newui::ThemedTreeItemStyle>());
    themedTreeItem->setDesiredSize(newui::Size(160.0f, 18.0f));
    treeRow->addChild(themedTreeItem);

    // A tiny 3-tab strip (left/middle/right edge parts) + pane below it.
    auto* tabRow = new newui::SubView();
    tabRow->setName("tabRow");
    tabRow->setVisible(true);
    tabRow->setDesiredSize(newui::Size(180.0f, 24.0f));
    tabRow->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));
    content2->addChild(tabRow);

    auto* tabLeft = new newui::SubView();
    tabLeft->setName("tabLeft");
    tabLeft->setVisible(true);
    auto tabLeftStyle = std::make_unique<newui::ThemedTabItemStyle>();
    tabLeftStyle->position = newui::ThemedTabItemStyle::Position::Left;
    tabLeftStyle->selected = true;
    tabLeft->setStyle(std::move(tabLeftStyle));
    tabLeft->setDesiredSize(newui::Size(60.0f, 24.0f));
    tabRow->addChild(tabLeft);

    auto* tabMiddle = new newui::SubView();
    tabMiddle->setName("tabMiddle");
    tabMiddle->setVisible(true);
    tabMiddle->setStyle(std::make_unique<newui::ThemedTabItemStyle>());
    tabMiddle->setDesiredSize(newui::Size(60.0f, 24.0f));
    tabRow->addChild(tabMiddle);

    auto* tabRight = new newui::SubView();
    tabRight->setName("tabRight");
    tabRight->setVisible(true);
    auto tabRightStyle = std::make_unique<newui::ThemedTabItemStyle>();
    tabRightStyle->position = newui::ThemedTabItemStyle::Position::Right;
    tabRight->setStyle(std::move(tabRightStyle));
    tabRight->setDesiredSize(newui::Size(60.0f, 24.0f));
    tabRow->addChild(tabRight);

    auto* themedTabPane = new newui::SubView();
    themedTabPane->setName("themedTabPane");
    themedTabPane->setVisible(true);
    themedTabPane->setStyle(std::make_unique<newui::ThemedTabPaneStyle>());
    themedTabPane->setDesiredSize(newui::Size(180.0f, 40.0f));
    content2->addChild(themedTabPane);

    // Trackbar track + thumb, overlaid via a nested AnchorLayout so the
    // thumb sits on top of the track instead of stacked below it.
    auto* trackbarRow = new newui::SubView();
    trackbarRow->setName("trackbarRow");
    trackbarRow->setVisible(true);
    trackbarRow->setDesiredSize(newui::Size(180.0f, 20.0f));
    trackbarRow->setLayout(std::make_unique<newui::AnchorLayout>());
    content2->addChild(trackbarRow);

    auto* themedTrackbarTrack = new newui::SubView();
    themedTrackbarTrack->setName("themedTrackbarTrack");
    themedTrackbarTrack->setVisible(true);
    themedTrackbarTrack->setStyle(std::make_unique<newui::ThemedTrackbarTrackStyle>());
    auto trackParams = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Left | newui::Anchor::Right | newui::Anchor::Top);
    trackParams->topMargin = 8.0f;
    trackParams->height = 6.0f;
    themedTrackbarTrack->setLayoutParams(std::move(trackParams));
    trackbarRow->addChild(themedTrackbarTrack);

    auto* themedTrackbarThumb = new newui::SubView();
    themedTrackbarThumb->setName("themedTrackbarThumb");
    themedTrackbarThumb->setVisible(true);
    themedTrackbarThumb->setStyle(std::make_unique<newui::ThemedTrackbarThumbStyle>());
    auto thumbParams = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Left | newui::Anchor::Top);
    thumbParams->leftMargin = 70.0f;
    thumbParams->width = 12.0f;
    thumbParams->height = 20.0f;
    themedTrackbarThumb->setLayoutParams(std::move(thumbParams));
    trackbarRow->addChild(themedTrackbarThumb);

    // Progress bar: track + fill overlaid via their own nested
    // AnchorLayout each, same "narrower View sits on top of the track"
    // shape as the trackbar thumb above - how much is "filled" is just
    // how wide the fill SubView is, not a field on the style itself (see
    // ThemedProgressBarFillStyle's class comment). Two rows show two of
    // the fill's states: a normal ~60% progress, and a paused ~35%
    // progress - the same PBST_NORMAL/PBST_PAUSED green/yellow look
    // PBM_SETSTATE would give a real Win32 progress bar control.
    auto* progressRow1 = new newui::SubView();
    progressRow1->setName("progressRow1");
    progressRow1->setVisible(true);
    progressRow1->setDesiredSize(newui::Size(180.0f, 16.0f));
    progressRow1->setLayout(std::make_unique<newui::AnchorLayout>());
    content2->addChild(progressRow1);

    auto* themedProgressTrack1 = new newui::SubView();
    themedProgressTrack1->setName("themedProgressTrack1");
    themedProgressTrack1->setVisible(true);
    themedProgressTrack1->setStyle(std::make_unique<newui::ThemedProgressBarTrackStyle>());
    themedProgressTrack1->setLayoutParams(std::make_unique<newui::AnchorLayoutParams>(
        newui::Anchor::Left | newui::Anchor::Right | newui::Anchor::Top | newui::Anchor::Bottom));
    progressRow1->addChild(themedProgressTrack1);

    auto* themedProgressFill1 = new newui::SubView();
    themedProgressFill1->setName("themedProgressFill1");
    themedProgressFill1->setVisible(true);
    themedProgressFill1->setStyle(std::make_unique<newui::ThemedProgressBarFillStyle>());
    auto fill1Params = std::make_unique<newui::AnchorLayoutParams>(
        newui::Anchor::Left | newui::Anchor::Top | newui::Anchor::Bottom);
    fill1Params->width = 108.0f;  // ~60% of the 180-wide track
    themedProgressFill1->setLayoutParams(std::move(fill1Params));
    progressRow1->addChild(themedProgressFill1);

    auto* progressRow2 = new newui::SubView();
    progressRow2->setName("progressRow2");
    progressRow2->setVisible(true);
    progressRow2->setDesiredSize(newui::Size(180.0f, 16.0f));
    progressRow2->setLayout(std::make_unique<newui::AnchorLayout>());
    content2->addChild(progressRow2);

    auto* themedProgressTrack2 = new newui::SubView();
    themedProgressTrack2->setName("themedProgressTrack2");
    themedProgressTrack2->setVisible(true);
    themedProgressTrack2->setStyle(std::make_unique<newui::ThemedProgressBarTrackStyle>());
    themedProgressTrack2->setLayoutParams(std::make_unique<newui::AnchorLayoutParams>(
        newui::Anchor::Left | newui::Anchor::Right | newui::Anchor::Top | newui::Anchor::Bottom));
    progressRow2->addChild(themedProgressTrack2);

    auto* themedProgressFill2 = new newui::SubView();
    themedProgressFill2->setName("themedProgressFill2");
    themedProgressFill2->setVisible(true);
    auto progressFill2Style = std::make_unique<newui::ThemedProgressBarFillStyle>();
    progressFill2Style->state = newui::ThemedProgressBarFillStyle::FillState::Paused;
    themedProgressFill2->setStyle(std::move(progressFill2Style));
    auto fill2Params = std::make_unique<newui::AnchorLayoutParams>(
        newui::Anchor::Left | newui::Anchor::Top | newui::Anchor::Bottom);
    fill2Params->width = 63.0f;  // ~35% of the 180-wide track
    themedProgressFill2->setLayoutParams(std::move(fill2Params));
    progressRow2->addChild(themedProgressFill2);


    // A real, interactive ScrollBar (controls.h) - up/down arrow click
    // (with auto-repeat), track-click paging, and thumb drag are all live
    // here, unlike the loose hand-wired Themed*Style SubViews this used to
    // be. content2Layout's own crossAxisAlignment is Stretch (its default,
    // relied on by every other row above to fill content2's full width) -
    // without an explicit override here, this ScrollBar would get
    // stretched to that same full width instead of staying a narrow
    // column.
    auto* demoScrollBar = new newui::ScrollBar();
    demoScrollBar->setName("demoScrollBar");
    demoScrollBar->setDesiredSize(newui::Size(20.0f, 100.0f));
    demoScrollBar->setRange(0.0f, 100.0f);
    demoScrollBar->setPageSize(20.0f);
    demoScrollBar->setLineStep(5.0f);
    auto scrollBarParams = std::make_unique<newui::FlexLayoutParams>();
    scrollBarParams->crossAxisAlignment = newui::CrossAxisAlignment::Start;
    demoScrollBar->setLayoutParams(std::move(scrollBarParams));
    content2->addChild(demoScrollBar);

    auto* content3 = MakePanel("content3", "mediumseagreen", "darkgreen");
    content3->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    // ViewStyle::setBackgroundImage() demo - tiles the same dot-cursor
    // PNG (WriteDotCursorPNG(), already generated above) as a repeating
    // pattern fill (BLPattern's own default extend mode) in place of
    // content3's plain "mediumseagreen" solid color from MakePanel().
    if (!content3->style().setBackgroundImage("themes1_dot_cursor.png")) {
        std::cerr << "setBackgroundImage() failed for content3's background\n";
    }
    mainRow->addChild(content3);

    // ScrollView demo (controls.h) - content taller and wider than the
    // viewport, only reachable via the vertical/horizontal ScrollBars
    // ScrollView wires up automatically, real mouse-wheel support
    // (RootView::mouseWheel()'s bubbling - rootview.cpp - lets ScrollView
    // catch a wheel event over any of its nested content, not just
    // itself), and View::origin() (view.h) actually shifting where that
    // content paints/hit-tests. Real content (scrollRows below) is added
    // via ScrollView::addChild() exactly like any other container -
    // ScrollView routes it into its own internal viewport SubView
    // automatically.
    auto* content4 = new newui::ScrollView();
    content4->setName("content4");
    content4->setVisible(true);
    content4->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    mainRow->addChild(content4);

    const newui::Size scrollContentSize(300.0f, 900.0f);
    content4->setContentSize(scrollContentSize);

    const char* scrollRowColors[] = {
        "lightpink", "lightyellow", "lightgreen", "lightblue", "plum",
        "khaki", "lightsalmon", "lightcyan", "wheat", "thistle",
        "peachpuff", "palegreen", "lightsteelblue", "mistyrose", "honeydew",
    };
    for (int i = 0; i < 15; ++i) {
        auto* scrollRow = MakePanel("scrollRow" + std::to_string(i), scrollRowColors[i], "dimgray", 1.0f);
        scrollRow->setBounds(newui::Rect(8.0f, float(i) * 60.0f + 8.0f, scrollContentSize.width - 16.0f, 52.0f));
        content4->addChild(scrollRow);
    }

    // A small badge nested inside content1, pinned to its top-right
    // corner via a second, independent Layout - content1's own, not
    // the root's. AnchorLayoutParams' margins are relative to whichever
    // View the AnchorLayout is attached to, so this doesn't need to
    // know anything about content1's position within the row.
    content1->setLayout(std::make_unique<newui::AnchorLayout>());

    // TabControl demo - fills content1 (see AddTabControlDemo() above);
    // badge below is added after, so it draws on top of it.
    AddTabControlDemo(content1);

    auto* badge = MakePanel("badge", "white", "darkred", 2.0f);
    auto badgeParams = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Right | newui::Anchor::Top);
    badgeParams->rightMargin = 8.0f;
    badgeParams->topMargin = 8.0f;
    badgeParams->width = 24.0f;
    badgeParams->height = 24.0f;
    badge->setLayoutParams(std::move(badgeParams));
    content1->addChild(badge);

    // A small form grid nested inside content3 - see AddGridDemo() above.
    AddGridDemo(content3);

    app.run();

    ::DeleteObject(fancyItemBitmap);

    return 0;
}

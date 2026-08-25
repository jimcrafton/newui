#include "newui/menus.h"
#include "newui/application.h"
#include "newui/rootview.h"
#include "newui/frame.h"
#include "newui/viewstyle.h"
#include "newui/layout.h"
#include "newui/fontmanager.h"
#include "newui/font.h"
#include "newui/uicolormanager.h"
#include "newui/graphics.h"

#include <blend2d/blend2d.h>

#include <utility>

// Measurement fallback for an ownerDrawn MenuItem (inside a ContextMenu
// dropdown) that never wired an onMeasure handler - so ownerDrawn="true,
// onDraw=X" alone still gets a clickable/visible item instead of
// collapsing to nothing. Measured against the real menu font
// (NONCLIENTMETRICS's lfMenuFont) rather than whatever's currently
// selected into a stock DC, so the fallback size is at least in the right
// ballpark for a native-looking menu.
namespace {

newui::Size defaultMenuItemMeasure(const newui::MenuItem& item) {
    std::string label = item.shortcutText.empty()
        ? item.text
        : item.text + "    " + item.shortcutText;
    if (label.empty()) {
        label = " ";
    }

    HDC hdc = ::GetDC(nullptr);

    NONCLIENTMETRICSA ncm = {};
    ncm.cbSize = sizeof(ncm);
    HFONT font = nullptr;
    if (::SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        font = ::CreateFontIndirectA(&ncm.lfMenuFont);
    }
    HGDIOBJ oldFont = (font != nullptr) ? ::SelectObject(hdc, font) : nullptr;

    SIZE extent = {};
    ::GetTextExtentPoint32A(hdc, label.c_str(), static_cast<int>(label.size()), &extent);

    if (font != nullptr) {
        ::SelectObject(hdc, oldFont);
        ::DeleteObject(font);
    }
    ::ReleaseDC(nullptr, hdc);

    return newui::Size(float(extent.cx) + 24.0f, float(extent.cy) + 8.0f);
}

// A MenuBar bar-button's label, shaped once against the real system menu
// font - shared by MenuBarButtonView::paint() (drawing) and
// MakeMenuBarButton() (sizing), so the two never disagree about how wide
// a label actually is. font is owned by FontManager's cache (see
// newui::Font::blFont()'s doc comment) and stays valid for the rest of
// the process's lifetime, independent of the local newui::Font value
// used to resolve it here.
struct ShapedMenuBarLabel {
    BLFont* font = nullptr;
    float width = 0.0f;
    float ascent = 0.0f;
    float descent = 0.0f;
    bool valid = false;
};

ShapedMenuBarLabel ShapeMenuBarLabel(const std::string& text) {
    ShapedMenuBarLabel result;

    newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Menu);
    result.font = font.blFont();
    if (result.font == nullptr) {
        return result;
    }

    newui::TextMetrics tm = font.measureText(text);
    result.width = tm.width;
    result.ascent = tm.ascent;
    result.descent = tm.descent;
    result.valid = true;
    return result;
}

// Internal to MenuBar - not declared in menus.h, same "keep implementation
// machinery out of the public header" convention cursor.cpp already uses
// for its own anonymous-namespace helpers.
class MenuBarButtonView : public newui::SubView {
public:
    // Non-owning - points into the owning MenuBar's own root_ tree, which
    // outlives this button (rebuilt/destroyed together in
    // MenuBar::setMenuItems()).
    newui::MenuItem* menuItem = nullptr;

    // Without this, RootView::mouseDown() unconditionally calls
    // setFocusedSubView(this) the instant a menu bar button is hit -
    // before MenuBarButtonClicked() (below) even opens the dropdown -
    // stealing focus away from whatever document/control the user was
    // actually working in. A command reached through that dropdown
    // (Copy, say) that dispatches against "whatever's currently
    // focused" (RootView::performCommand()) would then silently find
    // nothing to act on. Real menu bars never take focus away from the
    // document this way - matches that.
    bool canBecomeFocused() const override {
        return false;
    }

    void paint(BLContext& ctx) override {
        if (menuItem == nullptr) {
            return;
        }

        ShapedMenuBarLabel shaped = ShapeMenuBarLabel(menuItem->text);
        if (!shaped.valid) {
            return;
        }

        newui::Rect bounds = getClientBounds();
        float x = bounds.left() + (bounds.size().width - shaped.width) * 0.5f;
        float textHeight = shaped.ascent + shaped.descent;
        float baselineY = bounds.top() + (bounds.size().height - textHeight) * 0.5f + shaped.ascent;

        // GetSysColor(COLOR_MENUTEXT) doesn't track Light/Dark mode at
        // all (see UIColorManager's own class comment in
        // uicolormanager.h for why) - the item's background (drawn via
        // ThemedMenuBarItemStyle, i.e. paintStyle(), which runs before
        // this paint() override) already follows it through
        // ThemedViewStyle::paint()'s dark-mode fake, so the label needs
        // its own theme-aware color to match instead of staying fixed.
        newui::Color textColor = newui::UIColorManager::colorFor(newui::UIColorRole::ControlText);
        ctx.set_fill_style(textColor.toBLRgba32());
        ctx.fill_utf8_text(BLPoint(x, baselineY), *shaped.font, menuItem->text.c_str());
    }
};

// Builds a ContextMenu on the fly ("dynamically" - a single, short-lived
// instance per click, never cached) from the clicked button's own
// menuItem, right under it. Needs no MenuBar-instance state at all - the
// clicked button (sender) already carries everything (which MenuItem,
// and via rootView() the live HWND/screen position to show at).
newui::SyncReturn MenuBarButtonClicked(newui::View& sender, const newui::Point&, std::uint32_t, std::uint32_t) {
    auto& button = static_cast<MenuBarButtonView&>(sender);
    if (button.menuItem == nullptr || !button.menuItem->hasChildren()) {
        return newui::SyncReturn::Ignored;
    }

    newui::RootView* root = button.rootView();
    if (root == nullptr || root->windowHandle() == nullptr) {
        return newui::SyncReturn::Ignored;
    }

    // TrackPopupMenu's owner has to be the real top-level window, not
    // RootView's own (WS_CHILD) HWND - WM_MEASUREITEM/WM_DRAWITEM for an
    // ownerDrawn dropdown item are handled in Frame::handleMessage(),
    // which only ever sees messages sent to Frame's own HWND. Passing
    // RootView's HWND here left those unanswered, so the item never got a
    // real size or a paint - it just collapsed to a blank cell.
    newui::Frame* frame = root->getFrame();
    if (frame == nullptr || frame->frameHandle() == nullptr) {
        return newui::SyncReturn::Ignored;
    }

    newui::Point screenPt = root->localToScreen(
        root->accumulatedOffset(&button) + newui::Point(0.0f, button.bounds().size().height));

    newui::ContextMenu popup;
    popup.show(frame->frameHandle(), *button.menuItem, int(screenPt.x), int(screenPt.y));
    return newui::SyncReturn::Handled;
}

}  // namespace

namespace newui {

MenuItem* MenuItem::addChild(std::unique_ptr<MenuItem> child) {
    child->parent_ = this;
    MenuItem* raw = child.get();
    children_.push_back(std::move(child));
    return raw;
}

void MenuItem::setAction(Action* action) {
    action_ = action;
    if (action != nullptr) {
        Application::instance().runLoop().registerAction(action);
    }
}

// --- ContextMenu -----------------------------------------------------

ContextMenu::~ContextMenu() {
    if (hmenu_ != nullptr) {
        ::DestroyMenu(hmenu_);
    }
}

void ContextMenu::buildMenuLevel(HMENU hmenu, MenuItem& parentItem) {
    int position = 0;
    for (auto& childPtr : parentItem.children_) {
        MenuItem& item = *childPtr;
        item.ownerMenu_ = hmenu;

        // An item wired to an Action (setAction()) re-derives its
        // enabled state fresh every time the menu opens - this is
        // exactly what Action::update()/onActionUpdated is for ("so a
        // listener can reconsider whether this Action currently makes
        // sense", action.h) - so a caller never has to remember to
        // call state.setEnabled() itself on every focus/content
        // change; this one call, right before the native item is
        // built below, is the only place that needs to.
        if (item.action() != nullptr) {
            item.action()->update();
            item.state.setEnabled(item.action()->enabled());
        }

        MENUITEMINFOA mii = {};
        mii.cbSize = sizeof(mii);
        mii.fMask = MIIM_FTYPE | MIIM_STATE;
        mii.fState = (item.state.isEnabled() ? MFS_ENABLED : MFS_DISABLED)
            | (item.checked ? MFS_CHECKED : MFS_UNCHECKED);

        std::string label;  // must outlive the InsertMenuItemA() call below

        if (item.isSeparator) {
            mii.fType = MFT_SEPARATOR;
        } else {
            mii.fType = (item.ownerDrawn ? MFT_OWNERDRAW : MFT_STRING)
                | (item.radioGroup >= 0 ? MFT_RADIOCHECK : 0);

            if (item.ownerDrawn) {
                mii.fMask |= MIIM_DATA;
                mii.dwItemData = reinterpret_cast<ULONG_PTR>(&item);
            } else {
                label = item.shortcutText.empty() ? item.text : item.text + "\t" + item.shortcutText;
                mii.fMask |= MIIM_STRING;
                mii.dwTypeData = const_cast<char*>(label.c_str());
                mii.cch = static_cast<UINT>(label.size());

                if (item.bitmap != nullptr) {
                    mii.fMask |= MIIM_BITMAP;
                    mii.hbmpItem = item.bitmap;
                }
            }

            if (item.hasChildren()) {
                mii.fMask |= MIIM_SUBMENU;
                mii.hSubMenu = ::CreatePopupMenu();
            } else {
                item.commandId_ = nextCommandId_++;
                commandMap_[item.commandId_] = &item;
                mii.fMask |= MIIM_ID;
                mii.wID = item.commandId_;
            }
        }

        ::InsertMenuItemA(hmenu, static_cast<UINT>(position), TRUE, &mii);

        if (!item.isSeparator && item.hasChildren()) {
            buildMenuLevel(mii.hSubMenu, item);
        }

        ++position;
    }
}

void ContextMenu::buildNativeMenu(MenuItem& parentItem) {
    if (hmenu_ != nullptr) {
        ::DestroyMenu(hmenu_);
        hmenu_ = nullptr;
    }
    commandMap_.clear();
    nextCommandId_ = 1000;

    hmenu_ = ::CreatePopupMenu();
    buildMenuLevel(hmenu_, parentItem);
}

bool ContextMenu::show(HWND owner, MenuItem& parentItem, int screenX, int screenY) {
    buildNativeMenu(parentItem);

    // Opts owner into real native dark chrome (uicolormanager.h) right
    // before showing the popup - TrackPopupMenuEx() below reads dark-mode
    // eligibility off its owner window, not off hmenu_ itself, and this
    // is cheap/idempotent enough to just redo on every show() rather than
    // caching whether some particular owner has already been opted in.
    enableDarkModeForWindow(owner);

    ::SetForegroundWindow(owner);
    UINT id = UINT(::TrackPopupMenuEx(hmenu_, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN,
        screenX, screenY, owner, nullptr));
    // Documented MSDN workaround: without this, the popup can reappear on
    // the user's very next click into owner.
    ::PostMessage(owner, WM_NULL, 0, 0);

    bool clicked = (id != 0) && dispatchCommand(id);

    ::DestroyMenu(hmenu_);
    hmenu_ = nullptr;

    return clicked;
}

bool ContextMenu::dispatchCommand(UINT id) {
    auto it = commandMap_.find(id);
    if (it == commandMap_.end()) {
        return false;
    }
    MenuItem* item = it->second;

    if (item->radioGroup >= 0 && item->parent_ != nullptr) {
        for (auto& siblingPtr : item->parent_->children_) {
            MenuItem* sibling = siblingPtr.get();
            if (sibling->radioGroup == item->radioGroup) {
                setChecked(*sibling, sibling == item);
            }
        }
    }

    item->onClick.syncCall(*item);
    return true;
}

void ContextMenu::setChecked(MenuItem& item, bool checked) {
    item.checked = checked;
    // A parent/separator item never got a real commandId_ (stays 0) -
    // MF_BYCOMMAND against id 0 simply finds nothing in ownerMenu_ and
    // no-ops, so this is harmless to call on one, it just has no visible
    // effect (there is no native command id to look it up by afterward).
    if (item.ownerMenu_ != nullptr) {
        ::CheckMenuItem(item.ownerMenu_, item.commandId_,
            MF_BYCOMMAND | (checked ? MF_CHECKED : MF_UNCHECKED));
    }
}

void ContextMenu::setEnabled(MenuItem& item, bool enabled) {
    item.state.setEnabled(enabled);
    if (item.ownerMenu_ != nullptr) {
        ::EnableMenuItem(item.ownerMenu_, item.commandId_,
            MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    }
}

bool DispatchMenuMeasureItem(MEASUREITEMSTRUCT& mis) {
    if (mis.CtlType != ODT_MENU) {
        return false;
    }
    MenuItem* item = reinterpret_cast<MenuItem*>(mis.itemData);
    if (item == nullptr) {
        return false;
    }

    Size size = defaultMenuItemMeasure(*item);
    item->onMeasure.syncCall(*item, size);

    mis.itemWidth = static_cast<UINT>(size.width);
    mis.itemHeight = static_cast<UINT>(size.height);
    return true;
}

bool DispatchMenuDrawItem(const DRAWITEMSTRUCT& dis) {
    if (dis.CtlType != ODT_MENU) {
        return false;
    }
    MenuItem* item = reinterpret_cast<MenuItem*>(dis.itemData);
    if (item == nullptr) {
        return false;
    }

    int width = int(dis.rcItem.right - dis.rcItem.left);
    int height = int(dis.rcItem.bottom - dis.rcItem.top);
    if (width <= 0 || height <= 0) {
        return true;
    }

    // The "trap" itself: build a private, blank Image (graphics.h) sized
    // to the real item rect, hand the handler a BLContext over it (so it
    // draws with Blend2D, never touching an HDC), then blit the result
    // onto dis.hDC via the Image's own memDC() - the exact
    // Image+BLContext+BitBlt pattern this class's own doc comment already
    // described as the "Blend2D-quality" option, now done once here
    // instead of duplicated by every onDraw handler.
    gfx::Image itemImage(width, height);
    if (!itemImage.isValid()) {
        return true;  // couldn't allocate a DIB-backed buffer - draw nothing rather than crash
    }


    item->state.setDisabled((dis.itemState & ODS_DISABLED) == ODS_DISABLED);
    item->state.setInactive((dis.itemState & ODS_INACTIVE) == ODS_INACTIVE);
    item->state.setGreyedOut((dis.itemState & ODS_GRAYED) == ODS_GRAYED);
    item->state.setFocused((dis.itemState & ODS_FOCUS) == ODS_FOCUS);
    item->state.setHighlighted((dis.itemState & ODS_HOTLIGHT) == ODS_HOTLIGHT);
    //item->state.setSelected((dis.itemState & ODS_SELECTED) == ODS_SELECTED);


    BLContext ctx(itemImage.blImage());
    item->onDraw.syncCall(*item, ctx, Rect(0.0f, 0.0f, float(width), float(height)));
    ctx.end();

    ::BitBlt(dis.hDC, dis.rcItem.left, dis.rcItem.top, width, height, itemImage.memDC(), 0, 0, SRCCOPY);
    return true;
}

// --- MenuBar -----------------------------------------------------------

MenuBar::MenuBar() {
    setName("MenuBar");
    setVisible(true);
    setStyle(std::make_unique<ThemedMenuBarBackgroundStyle>());
    setLayout(std::make_unique<FlexLayout>(Orientation::Horizontal));
    setDesiredSize(Size(0.0f, 32.0f));  // caller can override via setDesiredSize() afterward
}

void MenuBar::setMenuItems(std::vector<std::unique_ptr<MenuItem>> items) {
    // Detach + delete the previous button Views - removeChild() doesn't
    // delete (caller-managed, same convention as everywhere else raw
    // View ownership terminates - see View::destroy()'s own while-loop),
    // and the previous root_ tree they pointed into is about to be
    // replaced anyway.
    while (!childViews().empty()) {
        SubView* child = childViews().front();
        removeChild(child);
        child->destroy();
        delete child;
    }

    root_ = MenuItem();

    for (auto& item : items) {
        MenuItem* topLevel = root_.addChild(std::move(item));

        ShapedMenuBarLabel shaped = ShapeMenuBarLabel(topLevel->text);
        Size buttonSize = shaped.valid
            ? Size(shaped.width + 24.0f, shaped.ascent + shaped.descent + 12.0f)
            : Size(60.0f, 28.0f);

        auto* button = new MenuBarButtonView();
        button->setName(topLevel->text);
        button->setVisible(true);
        button->menuItem = topLevel;
        button->setStyle(std::make_unique<ThemedMenuBarItemStyle>());
        button->setDesiredSize(buttonSize);
        button->onMouseDown.add(&MenuBarButtonClicked);

        addChild(button);
    }
}

}  // namespace newui

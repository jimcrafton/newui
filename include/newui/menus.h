#pragma once

#include "newui/newui.h"
#include "newui/delegate.h"
#include "newui/geometry.h"
#include "newui/subview.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace newui {

    // A single menu item - either a leaf (fires onClick when clicked), a
    // separator, or a parent (has children_, opens a submenu and never
    // fires onClick itself - Windows never sends WM_COMMAND for a
    // submenu-opening item, it just opens the submenu). Owned by
    // std::unique_ptr in its parent's children_ (plain RAII tree, no
    // destroy() needed - no MenuItem owns a live per-item Win32 handle;
    // ContextMenu builds a transient, single-use HMENU from an existing
    // MenuItem's children on each show() - see ContextMenu::show()).
    // Shared, unmodified data model for both MenuBar (a SubView-tree menu
    // bar - see below) and ContextMenu (a native popup) - either can show
    // any MenuItem's children as a dropdown.
    class MenuItem {
    public:
        MenuItem() = default;
        explicit MenuItem(std::string text) : text(std::move(text)) {}

        typedef Delegate<MenuItem> ClickDelegate;
        ClickDelegate onClick;

        std::string text;
        bool enabled = true;
        bool isSeparator = false;
        bool checked = false;

        // >= 0: this item is mutually exclusive with every sibling that
        // shares the same non-negative value - see
        // ContextMenu::dispatchCommand(). -1 (default): not part of a
        // radio group, checked (if set) toggles independently.
        int radioGroup = -1;

        // Display-only, e.g. "Ctrl+S" - shown right-aligned next to text
        // (appended as "text\tshortcutText" when the native item is
        // built). Ignored (see onDraw) when ownerDrawn is set - Windows
        // never renders dwTypeData/text itself for an MFT_OWNERDRAW item,
        // only onMeasure/onDraw's handler decides what appears. No real
        // accelerator-table/keyboard handling is wired up for it either
        // way - just the visual.
        std::string shortcutText;

        // Opt in to drawing this item yourself (MFT_OWNERDRAW) instead of
        // Windows' own native rendering - see onMeasure/onDraw below.
        // Ignored on a separator (isSeparator always wins - a native
        // separator is cheap and rarely worth owner-drawing).
        bool ownerDrawn = false;

        // Fired from Frame's WM_MEASUREITEM (via DispatchMenuMeasureItem())
        // only when ownerDrawn is set. outSize arrives pre-filled with a
        // sane default (measured from text/shortcutText against the real
        // menu font) - a handler can leave it alone or overwrite it
        // entirely.
        typedef Delegate<MenuItem, Size&> MeasureDelegate;
        MeasureDelegate onMeasure;

        // Fired from Frame's WM_DRAWITEM (via DispatchMenuDrawItem()) only
        // when ownerDrawn is set. hdc/itemRect are exactly what Windows
        // handed over (itemRect already translated to newui's own Rect);
        // odAction/odState are DRAWITEMSTRUCT::itemAction/itemState passed
        // through raw (ODA_DRAWENTIRE/ODA_SELECT/ODA_FOCUS,
        // ODS_SELECTED/ODS_CHECKED/ODS_DISABLED/ODS_GRAYED/...) rather than
        // wrapped - draw with plain GDI against hdc, or (for a
        // Blend2D-drawn look) create a newui::Image (graphics.h) sized to
        // itemRect, paint into its blImage() via BLContext, and BitBlt
        // image.memDC() onto hdc.
        typedef Delegate<MenuItem, HDC, const Rect&, UINT, UINT> DrawDelegate;
        DrawDelegate onDraw;

        static std::unique_ptr<MenuItem> Separator() {
            auto item = std::make_unique<MenuItem>();
            item->isSeparator = true;
            return item;
        }

        // Takes ownership of child, sets child->parent_ to this, and
        // returns the raw pointer just added (for chaining/wiring up
        // onClick without a separate lookup).
        MenuItem* addChild(std::unique_ptr<MenuItem> child);

        const std::vector<std::unique_ptr<MenuItem>>& children() const {
            return children_;
        }

        bool hasChildren() const {
            return !children_.empty();
        }

        MenuItem* parent() const {
            return parent_;
        }

        // 0 until a ContextMenu::show() over this item's parent last
        // assigned one - only leaf items (no children, not a separator)
        // ever get one; Windows never sends WM_COMMAND for a
        // submenu-opening item, so parent items have no command id.
        // Transient - only meaningful while that show() call is still on
        // the stack (see ContextMenu::show()'s doc comment).
        UINT commandId() const {
            return commandId_;
        }

    private:
        // ContextMenu assigns/reads commandId_/ownerMenu_ while building
        // its transient native popup (show()) and needs them again for
        // setChecked()/setEnabled()/dispatchCommand() during that same
        // call.
        friend class ContextMenu;

        std::vector<std::unique_ptr<MenuItem>> children_;
        MenuItem* parent_ = nullptr;
        UINT commandId_ = 0;

        // The exact HMENU this item was inserted into during the most
        // recent ContextMenu::show() over its parent - needed so
        // ContextMenu::setChecked()/setEnabled() can call
        // ::CheckMenuItem()/::EnableMenuItem() against the right menu
        // directly. Only valid while that show() call is on the stack;
        // dangles once the popup is torn down (harmless - nothing reads
        // it afterward).
        HMENU ownerMenu_ = nullptr;
    };

    // Builds a real, transient native Win32 popup menu (HMENU) from an
    // existing MenuItem's own children - in place, borrowed (parentItem
    // must outlive the call; its tree is never moved/copied) - and shows
    // it via ::TrackPopupMenu(). Single-use by design ("on the fly"): a
    // fresh ContextMenu per show() call is the intended usage (see
    // MenuBar's own button click handling, menus.cpp) - it holds no
    // meaningful state between calls, and its native HMENU is destroyed
    // again before show() returns.
    class ContextMenu {
    public:
        ContextMenu() = default;
        ~ContextMenu();

        ContextMenu(const ContextMenu&) = delete;
        ContextMenu& operator=(const ContextMenu&) = delete;

        // Builds a popup from parentItem.children() (CreatePopupMenu()/
        // InsertMenuItemA() - needs no live window, same as Cursor's
        // HCURSOR-building constructors), then blocks - via
        // ::TrackPopupMenuEx()'s own internal modal loop, the same
        // blocking model every native Windows app's own menu already has -
        // until the user picks an item or dismisses the popup (Esc, click
        // elsewhere). owner is the HWND TrackPopupMenu tracks against
        // (also where any ownerDrawn item's WM_MEASUREITEM/WM_DRAWITEM
        // will arrive - see DispatchMenuMeasureItem()/DispatchMenuDrawItem()
        // below). TPM_RETURNCMD means the clicked command id comes back
        // directly with no WM_COMMAND ever sent, so dispatchCommand() is
        // called internally, right here - no owner-side click routing
        // needed at all. Destroys its native popup before returning
        // regardless of outcome. Returns true iff an item was actually
        // clicked (its onClick already fired by the time this returns).
        bool show(HWND owner, MenuItem& parentItem, int screenX, int screenY);

        // Looks up the leaf MenuItem registered for id (assigned during
        // the show() currently on the stack) and fires its onClick. If
        // item->radioGroup >= 0, first unchecks every sibling sharing the
        // same radioGroup and checks this one (model +, harmlessly, the
        // about-to-be-destroyed native menu - the model update is what
        // matters, so the next show() over the same parent reflects it).
        // Returns false if id isn't currently known.
        bool dispatchCommand(UINT id);

        // Explicit model+native update - so a click handler that wants
        // checkbox-style toggle behavior writes
        // contextMenu.setChecked(item, !item.checked) instead of relying
        // on implicit auto-toggle-on-click. No-op on the native menu (but
        // still updates the model) if item has no ownerMenu_ (not part of
        // the popup currently being shown, or called outside show()).
        void setChecked(MenuItem& item, bool checked);
        void setEnabled(MenuItem& item, bool enabled);

        // The native popup built by the most recent show()/buildNativeMenu()
        // call, or nullptr before either has run. Exposed for inspection
        // (e.g. tests verifying tree structure via GetMenuItemCount()/
        // GetMenuStringA()/etc. against a real HMENU).
        HMENU handle() const {
            return hmenu_;
        }

    protected:
        // The "build" half of show() - (re)creates hmenu_ via
        // ::CreatePopupMenu() and recursively inserts parentItem's own
        // children into it, without ever calling ::TrackPopupMenuEx().
        // Factored out (show() calls this too) specifically so a
        // test-local subclass can exercise the native tree-building logic
        // directly - TrackPopupMenuEx() blocks on real user input, so it
        // isn't headlessly testable, same "protected seam for testability"
        // convention RootView/ThemedButtonStyle already use elsewhere in
        // this codebase (see TestableRootView/TestableThemedButtonStyle).
        void buildNativeMenu(MenuItem& parentItem);

    private:
        HMENU hmenu_ = nullptr;
        std::unordered_map<UINT, MenuItem*> commandMap_;
        UINT nextCommandId_ = 1000;  // stays clear of low ids - same
                                      // defensive-headroom habit as
                                      // custom_message_constants.h's
                                      // WM_APP + 1

        // Recursively inserts parentItem's children into hmenu (its
        // already-created native container - CreatePopupMenu() for the
        // top level, or a nested one for a submenu), assigning command
        // ids/ownerMenu_ as it goes - see menus.cpp.
        void buildMenuLevel(HMENU hmenu, MenuItem& parentItem);
    };

    // Handle Frame's WM_MEASUREITEM/WM_DRAWITEM for an ownerDrawn
    // MenuItem - both identify which MenuItem via
    // MEASUREITEMSTRUCT::itemData/DRAWITEMSTRUCT::itemData directly (set
    // to the MenuItem* itself at build time - MSDN's documented way to
    // identify a menu item in these messages, since itemID isn't reliable
    // for WM_MEASUREITEM), no ContextMenu instance needed - only ever set
    // (MIIM_DATA) on items built with ownerDrawn, so any ODT_MENU message
    // a Frame receives is guaranteed to carry a real MenuItem*, whether it
    // came from a ContextMenu it's hosting via TrackPopupMenu or (if ever
    // reintroduced) its own native bar. Return false (Frame should fall
    // back to DefWindowProcA) if CtlType isn't ODT_MENU. Free functions,
    // not tied to a specific ContextMenu, since neither ever touches an
    // instance's own state - just the MenuItem* itemData already carries.
    bool DispatchMenuMeasureItem(MEASUREITEMSTRUCT& mis);
    bool DispatchMenuDrawItem(const DRAWITEMSTRUCT& dis);

    // A custom-drawn (uxtheme-themed) menu bar - a SubView itself (the bar
    // row), not a native Win32 HMENU bar. A window's own menu bar row
    // height is fixed by system metrics and ignores WM_MEASUREITEM
    // (confirmed live - a native bar can't be made to look different), so
    // MenuBar IS an ordinary horizontal row of SubViews instead - one
    // child per top-level MenuItem, styled via ThemedMenuBarItemStyle
    // (viewstyle.h, real uxtheme MENU/MENU_BARITEM chrome). Clicking one
    // builds a ContextMenu on the fly (see above) from that item's own
    // children and shows it right under the button - the only place
    // native menu-building logic still exists.
    //
    // Heap-only, like every other SubView (see View's class comment) -
    // construct with new MenuBar(...), add into your own tree via
    // addChild() exactly like any other SubView (e.g.
    // rootView.addChild(menuBar)) - ownership from then on is the normal
    // View/SubView tree ownership, nothing MenuBar-specific to manage.
    class MenuBar : public SubView {
    public:
        MenuBar();

        // Replaces root()'s children (the top-level items) and rebuilds
        // this MenuBar's own child buttons (childViews()) to match, one
        // per top-level MenuItem.
        void setMenuItems(std::vector<std::unique_ptr<MenuItem>> items);

        MenuItem& root() {
            return root_;
        }

        const MenuItem& root() const {
            return root_;
        }

    private:
        MenuItem root_;
    };

}

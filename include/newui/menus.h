#pragma once

#include "newui/newui.h"
#include "newui/action.h"
#include "newui/delegate.h"
#include "newui/geometry.h"
#include "newui/subview.h"

#include <blend2d/blend2d.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace newui {
    enum MenuStateFlags {
        Disabled = 0x01,
        Focused = 0x02,
        GreyedOut = 0x04,
        Highlighted = 0x08,
        Inactive = 0x10,
        Selected = 0x20
    };

    class MenuState {
    public:
        MenuState() {}

        MenuState(const std::uint32_t& v):state_(v){ }

        bool isEnabled() const { return !isDisabled(); }
        bool isDisabled() const { return (state_ & Disabled) == Disabled; }
        bool isFocused() const { return (state_ & Focused) == Focused; }
        bool isGreyedOut() const { return (state_ & GreyedOut) == GreyedOut; }
        bool isHighlighted() const { return (state_ & Highlighted) == Highlighted; }
        bool isInactive() const { return (state_ & Inactive) == Inactive; }
        bool isSelected() const { return (state_ & Selected) == Selected; }

        void setEnabled(bool v) {
            setDisabled(!v);
        }

        void setDisabled(bool v) {
            if (v) { state_ |= Disabled; }
            else { state_ &= ~Disabled; }
        }

        void setFocused(bool v) {
            if (v) { state_ |= Focused; }
            else { state_ &= ~Focused; }
        }

        void setGreyedOut(bool v) {
            if (v) { state_ |= GreyedOut; }
            else { state_ &= ~GreyedOut; }
        }

        void setHighlighted(bool v) {
            if (v) { state_ |= Highlighted; }
            else { state_ &= ~Highlighted; }
        }

        void setInactive(bool v) {
            if (v) { state_ |= Inactive; }
            else { state_ &= ~Inactive; }
        }

        void setSelected(bool v) {
            if (v) { state_ |= Selected; }
            else { state_ &= ~Selected; }
        }

        operator std::uint32_t () const {
            return state_;
        }

        MenuState& operator=(const std::uint32_t& rhs) {
            state_ = rhs;
            return *this;
        }
    private:
        std::uint32_t state_ = 0;
    };

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
    class MenuItem : public Component {
    public:
        MenuItem() = default;
        explicit MenuItem(std::string text) : text_(std::move(text)) {}
        // Deletes every child still attached (see addChild()).
        ~MenuItem() override;

        // Owns raw child pointers, like View - a copy would double-delete them.
        MenuItem(const MenuItem&) = delete;
        MenuItem& operator=(const MenuItem&) = delete;

        typedef Delegate<MenuItem> ClickDelegate;
        ClickDelegate onClick;


        
        MenuState& state() { return state_; }
        const MenuState& state() const { return state_; }

        const std::string& text() const { return text_; }
        void setText(const std::string& text) { text_ = text; }

        bool isSeparator() const { return separator_; }
        void setSeparator(bool separator) { separator_ = separator; }

        bool isChecked() const { return checked_; }
        void setChecked(bool checked) { checked_ = checked; }

        // >= 0: this item is mutually exclusive with every sibling that
        // shares the same non-negative value - see
        // ContextMenu::dispatchCommand(). -1 (default): not part of a
        // radio group, checked (if set) toggles independently.
        int radioGroup() const { return radioGroup_; }
        void setRadioGroup(int radioGroup) { radioGroup_ = radioGroup; }

        // Display-only, e.g. "Ctrl+S" - shown right-aligned next to text
        // (appended as "text	shortcutText" when the native item is
        // built). Ignored (see onDraw) when ownerDrawn is set - Windows
        // never renders dwTypeData/text itself for an MFT_OWNERDRAW item,
        // only onMeasure/onDraw's handler decides what appears. No real
        // accelerator-table/keyboard handling is wired up for it either
        // way - just the visual.
        const std::string& shortcutText() const { return shortcutText_; }
        void setShortcutText(const std::string& shortcutText) { shortcutText_ = shortcutText; }

        // Opt in to drawing this item yourself (MFT_OWNERDRAW) instead of
        // Windows' own native rendering - see onMeasure/onDraw below.
        // Ignored on a separator (isSeparator() always wins - a native
        // separator is cheap and rarely worth owner-drawing).
        //
        // Known limitation, confirmed live: Windows falls back to legacy,
        // non-dark-themed popup compositing for the *whole* menu the
        // moment any one of its items is MFT_OWNERDRAW - a plain
        // (non-owner-drawn) sibling item still renders fine, but the
        // popup's own frame/background around an owner-drawn item stays
        // stuck in light chrome even when enableDarkModeForWindow()
        // (uicolormanager.h) has been applied to its owner. No known
        // workaround short of not owner-drawing at all - see bitmap()
        // below for a native alternative (an icon next to plain text)
        // that doesn't hit this.
        bool isOwnerDrawn() const { return ownerDrawn_; }
        void setOwnerDrawn(bool ownerDrawn) { ownerDrawn_ = ownerDrawn; }

        // Native MIIM_BITMAP icon shown to the left of a plain
        // (non-owner-drawn) item's text - caller-owned, borrowed only
        // (this MenuItem never creates/destroys it, same as every other
        // Win32 handle this class avoids owning - see the class
        // comment). nullptr (default) = no icon, plain MFT_STRING as
        // before. The way to get a "colored glyph next to a label" look
        // without MFT_OWNERDRAW's dark-mode caveat above. Not reflected:
        // a runtime handle can't be saved.
        //@reflect ignore=true
        HBITMAP bitmap() const { return bitmap_; }
        //@reflect ignore=true
        void setBitmap(HBITMAP bitmap) { bitmap_ = bitmap; }

        // Fired from Frame's WM_MEASUREITEM (via DispatchMenuMeasureItem())
        // only when ownerDrawn is set. outSize arrives pre-filled with a
        // sane default (measured from text/shortcutText against the real
        // menu font) - a handler can leave it alone or overwrite it
        // entirely.
        typedef Delegate<MenuItem, Size&> MeasureDelegate;
        MeasureDelegate onMeasure;

        // Fired from Frame's WM_DRAWITEM (via DispatchMenuDrawItem()) only
        // when ownerDrawn is set - draw with Blend2D against ctx, not raw
        // GDI. DispatchMenuDrawItem() builds ctx itself: a private
        // newui::Image (graphics.h) sized to the real WM_DRAWITEM rect,
        // wrapped in a BLContext, BitBlt (via the Image's own memDC()) onto
        // the real HDC after this delegate returns - so itemRect is
        // (0,0)-(width,height) local to that private image, not a
        // DC-relative rect, and there's no HDC for a handler to touch at
        // all. odAction/odState are DRAWITEMSTRUCT::itemAction/itemState
        // passed through raw (ODA_DRAWENTIRE/ODA_SELECT/ODA_FOCUS,
        // ODS_SELECTED/ODS_CHECKED/ODS_DISABLED/ODS_GRAYED/...).
        typedef Delegate<MenuItem, BLContext&, const Rect&> DrawDelegate;
        DrawDelegate onDraw;

        // Non-owning - see Action's own class comment. Unlike
        // Control::setAction(), setting this also registers action with
        // the current Application's RunLoop (Application::instance().
        // runLoop().registerAction() - see RunLoop::registerAction())
        // so action's hotkey(), if any, is matched against real
        // keystrokes - shortcutText above stays purely cosmetic, it's
        // still on the caller to keep it in sync with action's hotkey()
        // if both are set.
        void setAction(Action* action);

        Action* action() { return action_; }
        const Action* action() const { return action_; }

        static std::unique_ptr<MenuItem> Separator() {
            auto item = std::make_unique<MenuItem>();
            item->setSeparator(true);
            return item;
        }

        // Child ownership mirrors View's: addChild() takes ownership (detaching child from any
        // previous parent first), removeChild() detaches without deleting (the caller owns it
        // again), and whatever is still attached is deleted with this item.
        //
        // @reflect collection add=addChild remove=removeChild - saved and loaded as a real tree,
        // the same way View::childViews() is.
        const std::vector<MenuItem*>& children() const {
            return children_;
        }

        void addChild(MenuItem* child);
        void removeChild(MenuItem* child);

        // Convenience for building menus in code: takes ownership and returns the item just added
        // (for wiring up onClick without a separate lookup).
        //@reflect ignore=true
        MenuItem* addChild(std::unique_ptr<MenuItem> child);

        // Moves an attached child to newIndex (clamped); a no-op if child isn't a direct child.
        void reorderChild(MenuItem* child, std::size_t newIndex);

        // Deletes every child.
        void deleteChildren();

        bool hasChildren() const {
            return !children_.empty();
        }

        // Non-owning upward back-reference to the owning MenuItem -
        // reachable downward already via that item's own children() - same
        // "would recurse straight back into the tree ObjectReader/
        // ObjectWriter are already walking" reasoning View::rootView()
        // (view.h) is ignore-annotated for.
        //@reflect ignore=true
        MenuItem* parent() const {
            return parent_;
        }

        // Same contract as View::setParent(): detaches from the current parent (if any), then
        // appends to newParent (nullptr just detaches - the caller then owns this item). Refuses
        // (false, no-op) if newParent is this item or one of its descendants.
        bool setParent(MenuItem* newParent);

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

        std::vector<MenuItem*> children_;
        MenuItem* parent_ = nullptr;
        UINT commandId_ = 0;
        Action* action_ = nullptr;

        // The exact HMENU this item was inserted into during the most
        // recent ContextMenu::show() over its parent - needed so
        // ContextMenu::setChecked()/setEnabled() can call
        // ::CheckMenuItem()/::EnableMenuItem() against the right menu
        // directly. Only valid while that show() call is on the stack;
        // dangles once the popup is torn down (harmless - nothing reads
        // it afterward).
        HMENU ownerMenu_ = nullptr;

        MenuState state_;
        std::string text_;
        bool separator_ = false;
        bool checked_ = false;
        int radioGroup_ = -1;
        std::string shortcutText_;
        bool ownerDrawn_ = false;
        HBITMAP bitmap_ = nullptr;
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
    // @reflect category=menutoolbar
    class MenuBar : public SubView {
    public:
        MenuBar();

        // Replaces root()'s children (the top-level items) and rebuilds
        // this MenuBar's own child buttons (childViews()) to match, one
        // per top-level MenuItem.
        void setMenuItems(std::vector<std::unique_ptr<MenuItem>> items);

        // The top-level menus (root()'s children) - what a saved MenuBar holds.
        // addMenu()/removeMenu() follow MenuItem::addChild()/removeChild()'s ownership and rebuild
        // the buttons.
        //
        // @reflect collection add=addMenu remove=removeMenu
        const std::vector<MenuItem*>& menus() const {
            return root_.children();
        }
        void addMenu(MenuItem* item);
        void removeMenu(MenuItem* item);

        // Recreates one button per top-level menu - call after changing root()'s children or a
        // top-level item's text directly.
        void rebuildButtons();

        // Not reflected: menus() already covers its children.
        //@reflect ignore=true
        MenuItem& root() {
            return root_;
        }

        //@reflect ignore=true
        const MenuItem& root() const {
            return root_;
        }

    private:
        MenuItem root_;
    };

}

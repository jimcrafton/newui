#pragma once

// Windows-only: owner-draw menu message dispatch used by Frame's message handler.

#include "newui/menus.h"
#include "newui/win32/winheaders.h"

namespace newui {

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

} // namespace newui

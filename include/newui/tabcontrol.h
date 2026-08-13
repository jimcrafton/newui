#pragma once

#include "newui/newui.h"
#include "newui/delegate.h"
#include "newui/subview.h"
#include "newui/viewstyle.h"

#include <cstddef>
#include <string>

namespace newui {

    // A themed tab strip (ThemedTabItemStyle buttons) plus a
    // CardLayout-driven content area (one page per tab, all but the
    // selected one hidden) combined into a single composite control -
    // same "SubView owning a themed strip of button-like children" shape
    // MenuBar (menus.h) already established. Unlike MenuBar's buttons
    // (which have to open a native ContextMenu popup), a tab click just
    // switches which already-built page is visible - no live window
    // needed at all for that, so (unlike MenuBar/ContextMenu) TabControl
    // is fully headlessly testable.
    //
    // Heap-only, like every other SubView (see View's class comment) -
    // construct with new TabControl(...), add into your own tree via
    // addChild() exactly like any other SubView.
    class TabControl : public SubView {
    public:
        explicit TabControl(ThemedTabItemStyle::TabAlignment alignment = ThemedTabItemStyle::TabAlignment::Top);

        // Appends one tab: a themed ThemedTabItemStyle button labeled
        // text in the strip, and page (already heap-allocated - same
        // caller-builds/TabControl-takes-ownership-via-addChild()
        // convention as every other SubView tree in this codebase, not a
        // unique_ptr) as its matching content - both index-aligned. The
        // first tab added auto-selects (a tab control should never show
        // nothing). Returns page.
        SubView* addTab(const std::string& text, SubView* page);

        std::size_t tabCount() const;

        // The Nth tab's own button/page SubView (addTab() order),
        // nullptr if index is out of range - e.g. to wire up further
        // customization (a tooltip, an icon) after the fact, or (tests)
        // to drive the button's already-wired onMouseDown directly
        // without a real HWND/message pump.
        SubView* tabButton(std::size_t index) const;
        SubView* page(std::size_t index) const;

        std::size_t selectedIndex() const {
            return selectedIndex_;
        }

        // No-op if index is out of range.
        void selectTab(std::size_t index);

        ThemedTabItemStyle::TabAlignment alignment() const {
            return alignment_;
        }

        typedef Delegate<TabControl, std::size_t> TabChangedDelegate;
        TabChangedDelegate onTabChanged;  // fires from selectTab() (including from a tab click)

    private:
        ThemedTabItemStyle::TabAlignment alignment_;
        std::size_t selectedIndex_ = 0;
        SubView* stripRow_;   // the row/column of tab-item buttons
        SubView* pagesArea_;  // CardLayout container - one page per addTab() call

        // Recomputes every tab button's ThemedTabItemStyle::position
        // (Left/Right/Middle/Only) from scratch - has to re-run on every
        // addTab() call, since which button is first/last changes.
        void updateTabPositions();
    };

}

#pragma once

namespace newui {

    class RootView;
    class SubView;
    class View;

    // Which way Tab/Shift+Tab should move focus - see UIInputManager::moveFocus().
    enum class FocusNavigationDirection {
        Next,
        Previous
    };

    // Singleton home for the mouse/keyboard *policy* decisions this
    // toolkit's input handling needs but that don't belong on any single
    // View: which View a click should actually focus (not necessarily the
    // exact SubView the mouse hit - some controls, and any plain SubView,
    // shouldn't steal focus at all - see resolveClickFocusTarget()), and
    // which View Tab/Shift+Tab should move keyboard focus to next (see
    // moveFocus()).
    //
    // Deliberately holds no per-RootView state of its own - no cached
    // "the" focused View, no cached tab order. RootView already owns that
    // (focusedSubView()/setFocusedSubView(), rootview.h) and this process
    // can have any number of independent RootViews alive at once (a main
    // window, a modal Dialog, a DropDownList's popup, ...), each with its
    // own focus chain - so every method here takes the RootView it should
    // act on directly rather than remembering one. What actually makes
    // this a singleton isn't shared state, then, but the same reasoning
    // Bundle/UIColorManager already are (bundle.h/uicolormanager.h): one
    // process-wide policy, one place to look for it, regardless of how
    // many RootViews exist.
    //
    // See RootView::mouseDown()/mouseDblClick() and RootView::keyEvent()
    // (rootview.cpp) for the real call sites.
    class UIInputManager {
    public:
        static UIInputManager& instance();

        // Starting at hitView (whatever View::hitTestChildren() found
        // under the mouse - nullptr for a click on empty space, or on a
        // View whose whole subtree is design-time - see
        // RootView::resolveInteractiveHit()), walks up parent() looking
        // for the nearest View that actually wants keyboard focus
        // (canBecomeFocused(), which is gated on acceptsFocus() - view.h)
        // - the same "clicking a Button's own drawn label still focuses
        // the Button" reasoning a real toolkit's hit-testing needs,
        // since the exact pixel-perfect leaf hit isn't necessarily the
        // right thing to focus. Returns nullptr if nothing in the chain
        // qualifies - clicking empty space, or a plain non-focusable
        // SubView with no focusable ancestor either, clears focus rather
        // than leaving it wherever it happened to be.
        SubView* resolveClickFocusTarget(SubView* hitView) const;

        // Moves root's focusedSubView() to the next (direction Next) or
        // previous (direction Previous) focusable SubView in reading
        // order - top-to-bottom, then left-to-right within a 5px-
        // tolerance row (see the sort in uiinputmanager.cpp). Wraps around
        // at either end; a no-op if there's no focusable SubView to move
        // to. If root's own focusedSubView() isn't itself a focusable
        // candidate right now (nothing focused yet, or a focus veto has
        // otherwise left something odd in place), starts from the first
        // (direction Next) or last (direction Previous) candidate rather
        // than treating that as an error.
        //
        // Scoped, not always root's whole tree: walks up from
        // focusedSubView() looking for the nearest ancestor with
        // isFocusScope() true (View::, view.h) and, if one exists, cycles
        // only among candidates inside *that* subtree - the "Scoped
        // Geometric Hierarchy" a properties panel/inspector/timeline needs
        // so Tab can't leak out of it into unrelated parts of the same
        // window. A nested scope found while gathering candidates becomes
        // a single opaque stop in its parent scope's own cycle (its own
        // descendants aren't mixed in) rather than being recursed into -
        // but only if it can itself become focused (canBecomeFocused()):
        // an isFocusScope() View that never opted into acceptsFocus() too
        // has no way to actually receive that stop's focus (setFocusedSubView()
        // just no-ops against a non-focusable target), so treating it as a
        // reachable stop would make Tab appear to do nothing - its
        // descendants stay reachable by a mouse click instead, same as
        // any other unscoped container.
        void moveFocus(RootView& root, FocusNavigationDirection direction) const;

    private:
        UIInputManager() = default;
        UIInputManager(const UIInputManager&) = delete;
        UIInputManager& operator=(const UIInputManager&) = delete;
    };

}

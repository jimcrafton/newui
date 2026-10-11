#pragma once

#include <newui/subview.h>

namespace newui {

    class RootView;
    class View;

    // Which way Tab/Shift+Tab should move focus - see UIInputManager::moveFocus().
    enum class FocusNavigationDirection {
        Next,
        Previous
    };

    // Which way an arrow key should spatially jump focus - see
    // UIInputManager::moveFocusSpatially(). Deliberately distinct from
    // FocusNavigationDirection above: that one is about *reading order*
    // (Tab's Next/Previous through a 1-D sequence), this one is about
    // real on-screen geometry in all four directions.
    enum class SpatialDirection {
        Up,
        Down,
        Left,
        Right
    };

    // An invisible SubView that redirects Tab/Shift+Tab through to a
    // different real View - this toolkit's answer to "put this specific
    // View next in Tab order without renumbering everything else",
    // modeled on UIKit's own UIFocusGuide rather than a numeric priority
    // field (the web/Win32 tabindex idea considered and rejected here -
    // a plain int silently drifts out of sync as a layout changes, the
    // exact failure mode a position-anchored placeholder avoids: this
    // guide's *position* in the tree/layout is itself the only thing
    // that determines where the redirect fires from, so moving real
    // content around it can't desync anything the way renumbering a
    // dozen tabOrder values by hand could).
    //
    // Usage: construct one, addChild() it wherever in the tree the
    // desired jump point falls in reading order (its parent's own
    // geometric position is what places it in the Tab sequence - see
    // setBounds() below), then setRedirectTarget() to the real View that
    // should actually receive focus when UIInputManager::moveFocus()
    // (uiinputmanager.h) reaches it. Never itself becomes RootView::
    // focusedSubView() - moveFocus()/UIInputManager::resolveClickFocusTarget()
    // both resolve straight through it (and, in a chain, through however
    // many further guides it points at) to the first real SubView they
    // find; a redirectTarget() left null, or a cyclic chain of guides,
    // makes reaching this guide a no-op rather than getting stuck on it
    // or crashing.
    //
    // setBounds() only ever keeps the position you pass it, forcing the
    // size to (0,0) regardless of what's given - a real footprint would
    // make this guide hit-testable (View::hitTestChildren()) and able to
    // steal real mouse clicks meant for whatever it's sitting near, which
    // UIKit's own UIFocusGuide never does either (it participates in the
    // focus engine only, never in touch hit-testing). Only position
    // matters for sorting into the reading-order candidate list
    // (UIInputManager::moveFocus() only ever reads accumulatedOffset(),
    // never size, for that).
    //
    // Purely a code-time construct today, not .newui-persistable -
    // redirectTarget() is a plain non-owning View* into elsewhere in the
    // same tree (a forward reference, not a back-reference like View::
    // parent()/rootView()), and this codebase has no established pattern
    // yet for persisting a cross-reference like that (see RootView::
    // hoveredSubView()'s own doc comment, rootview.h, for the reflectgen
    // double-serialization trap a raw structural pointer property would
    // hit here too). Wire it up programmatically instead - e.g. right
    // after building the two real Views it connects.
    //@reflect ignore=true
    class FocusGuide : public SubView {
    public:
        // setVisible(true) - not "this is drawn", "this participates in
        // the tree at all": SubView (like every View) defaults to
        // isVisible() == false, and gatherFocusableInScope()
        // (uiinputmanager.cpp) prunes invisible nodes before ever
        // checking canBecomeFocused() - so a guide the caller forgot to
        // setVisible(true) on would silently never be gathered as a
        // candidate in the first place, well before redirectTarget() ever
        // got a chance to matter. Same "the caller shouldn't have to
        // remember this" reasoning every concrete Control already applies
        // to itself in its own constructor (Button::Button(), e.g.).
        FocusGuide() {
            setVisible(true);
            setAcceptsFocus(true);
        }

        void setBounds(const Rect& bounds) override {
            SubView::setBounds(Rect(bounds.pos(), Size(0.0f, 0.0f)));
        }

        View* redirectTarget() const {
            return redirectTarget_;
        }

        // Non-owning - same convention as Control::action() (controls.h):
        // whoever built the tree keeps owning the real target, this is
        // just a pointer to it.
        void setRedirectTarget(View* target) {
            redirectTarget_ = target;
        }

    private:
        View* redirectTarget_ = nullptr;
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
        // than leaving it wherever it happened to be. If the walk lands on
        // a FocusGuide (above - vanishingly unlikely given its forced
        // (0,0) size makes it unhittable on its own, but not impossible if
        // something real were nested inside one), resolves straight
        // through its redirectTarget() chain the same way moveFocus()
        // does, rather than ever returning the guide itself.
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
        //
        // Whatever candidate this lands on - the computed "next"/
        // "previous" entry, or the fallback first/last one - is resolved
        // through FocusGuide::redirectTarget() (above) before actually
        // being focused, exactly like resolveClickFocusTarget() does. A
        // guide with no redirectTarget() set, or a cyclic chain of them,
        // makes landing on it a no-op (focus stays exactly where it was)
        // rather than ever focusing the guide itself or crashing.
        void moveFocus(RootView& root, FocusNavigationDirection direction) const;

        // Arrow-key equivalent of moveFocus() above, called directly by
        // RootView::keyEvent() for VKeyCode in {vkUpArrow, vkDownArrow,
        // vkLeftArrow, vkRightArrow} on keKeyDown - same shape as Tab's
        // own handling there, just dispatch-then-fallback instead of
        // intercept-first: unlike Tab (always swallowed before it ever
        // reaches a View's own onKeyDown), an arrow key must always be
        // offered to root's focusedSubView() first - a ListView moving
        // its selection, a Slider changing its value, DropDownList's
        // popup navigation, ... - and only falls back to a spatial jump
        // if that View's onKeyDown doesn't handle it. This keeps
        // RootView::keyEvent itself a thin "detect the key, hand off to
        // UIInputManager" dispatcher for both, rather than splitting
        // "who owns focusedSubView_ dispatch" one way for Tab and another
        // for arrows - focusedSubView_ itself stays owned by RootView
        // throughout (read here only via its own public focusedSubView()
        // accessor, same as every other method in this class), so this
        // doesn't need RootView::keyEvent to also separately dispatch
        // onKeyDown to it beforehand the way the plain (non-arrow) key
        // path still does for every other key.
        //
        // A no-op if root's focusedSubView() is nullptr. Returns true if
        // the key was consumed locally (the focused View's own onKeyDown
        // handled it) or by a resulting spatial jump; false if neither
        // did anything - RootView::keyEvent doesn't currently need this,
        // but it's cheap to report and mirrors moveFocusSpatially()'s own
        // "did anything actually happen" return below.
        bool routeArrowKeyDown(RootView& root, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) const;

        // The actual spatial-jump policy routeArrowKeyDown() above falls
        // back to once it finds root's focusedSubView() didn't handle the
        // arrow key itself - a ListView/TreeView reporting that because
        // Up/Down already clamped at its own first/last row (nothing left
        // for it to do internally), but equally any other focused View
        // that simply never hooks arrow keys at all (a Button, Toggle,
        // Label, ...): a control that already consumes arrow keys for its
        // own purpose (Slider, DropDownList, ...) always reports Handled
        // and this never runs for it, so behavior for those is unchanged
        // either way - see routeArrowKeyDown()'s own doc comment for why
        // that generalization (not just ListView/TreeView) is the right
        // shape here, not scope creep. Adapted from the docx/plan.md's
        // own ArrowResult::BoundaryReached design (see
        // uiinputmanager-plan.md's HandleArrowKeyPressed) - real
        // accumulatedOffset()-based geometry here instead of a
        // GetGlobalBounds() RECT, and FocusGuide-aware/scope-respecting
        // candidate gathering reused straight from moveFocus() instead of
        // a flat GatherAllFocusableViews(). Exposed as its own public
        // method (rather than folded entirely into routeArrowKeyDown())
        // since the geometric jump itself is a reusable piece of policy
        // in its own right - e.g. a future explicit "jump focus" command
        // wired to something other than a raw arrow keystroke.
        //
        // Searches every focusable candidate in root's currently active
        // focus scope (same scope-trapping moveFocus() itself respects -
        // an arrow jump escaping a properties-panel/inspector container
        // would be exactly as surprising as Tab doing it), filters out
        // anything that isn't actually positioned in direction relative
        // to the current View's own edge (not just "happens to be
        // closer" - a candidate to the upper-left is never a valid
        // Down-jump target no matter how close), and moves focus to
        // whichever of those remaining candidates has the nearest
        // center-to-center distance. A no-op (returns false) if nothing
        // qualifies - root's focusedSubView() is nullptr, no other
        // candidate exists, or nothing lies in the requested direction at
        // all. Returns true if focus actually moved.
        bool moveFocusSpatially(RootView& root, SpatialDirection direction) const;

    private:
        UIInputManager() = default;
        UIInputManager(const UIInputManager&) = delete;
        UIInputManager& operator=(const UIInputManager&) = delete;
    };

}

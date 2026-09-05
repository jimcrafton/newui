#pragma once

#include <memory>
#include <optional>

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/command.h>
#include <newui/component.h>
#include <newui/cursor.h>
#include <newui/delegate.h>
#include <newui/dragndrop.h>
#include <newui/geometry.h>
#include <newui/layout.h>
#include <newui/viewstyle.h>

namespace newui {
    class RootView;
    class SubView;

    // Common state shared by View and SubView: bounds/visible/name storage
    // and the trivial accessors that read it identically in both. Anything
    // whose behavior differs between the two (setBounds, setVisible,
    // addChild/removeChild, initialize, destroy) stays in the derived class.
    //
    // Heap-only by convention, never stack/member-embedded: construct
    // SubView/RootView with new, same as everything else this codebase
    // owns via a raw pointer (a parent's childViews_, PropertyManager's
    // properties_, Frame's rootView_, ...) and frees explicitly - see
    // View::destroy().
    class View : public Component {
    public:

        View();

        virtual ~View() = default;

        typedef Delegate<View, const Size&> SizeChangedDelegate;
        typedef Delegate<View> VisibilityChangedDelegate;
        typedef Delegate<View> CreatedDelegate;
        typedef Delegate<View> DestroyedDelegate;
        typedef Delegate<View, Size&> QueryContentSizeDelegate;
        typedef Delegate<View, const Point&> ScrollOffsetChangedDelegate;
        typedef Delegate<View> ContentSizeChangedDelegate;
        typedef Delegate<View, const Rect&> RequestScrollIntoViewDelegate;

        typedef Delegate<View, const Point&, std::uint32_t, std::uint32_t> MouseEventDelegate;        
        typedef Delegate<View, const Point&, float> MouseWheelDelegate;

        typedef Delegate<View, std::uint32_t, int, int, std::uint32_t> KeyEventDelegate;

		typedef Delegate<View> FocusDelegate;


        const Rect& bounds() const {
            return bounds_;
        }

        // Virtual (not pure - reflectgen's collect_class() skips pure
        // virtual methods outright, is_pure_virtual_method(), so a `= 0`
        // here would stay invisible to it exactly like having no
        // declaration at all) so reflectgen sees a real getter+setter pair
        // on View itself and registers "bounds" as a genuinely read/write
        // property - before this, View::bounds() had no matching setter
        // reflectgen could see in the same class scan (SubView::
        // setBounds()/RootView::setBounds() are declared on the *derived*
        // classes, a separate scan pass), so reflectgen registered
        // "bounds" read-only, and Bundle::loadFrame()/loadView() silently
        // left every reconstructed View's bounds() at Rect{}'s default -
        // real, reproduced bug (Bundle test
        // LoadFrameAppliesBoundsAndVisibleOntoRebuiltChildren). This base
        // body is never actually reached in practice (View itself is
        // never directly instantiated - SubView::setBounds()/
        // RootView::setBounds() both override it with the real onSizeChanged()/
        // updateLayout()-or-HWND-resize behavior the class comment above
        // describes) - it exists purely so View stays concrete (matching
        // its own already-registered reflectgen `.constructor<>()`) and a
        // bare View, if one were ever constructed, still behaves sanely
        // rather than silently ignoring setBounds() altogether.
        virtual void setBounds(const Rect& bounds) {
            bounds_ = bounds;
        }

        bool isVisible() const {
            return visible_;
        }

        // Same reasoning as setBounds() above, and the same real bug it
        // fixed - "visible" had exactly the same getter-only-on-View shape.
        virtual void setVisible(bool visible) {
            visible_ = visible;
        }

        // If this View is already attached to a RootView (rootView() !=
        // nullptr) and name is non-empty, reserves it in that RootView's
        // NameManager (rootView()->nameManager()) so a later auto-
        // generated default name (see RootView::generateDefaultName())
        // never collides with a hand-assigned one - see view.cpp; needs
        // RootView's full definition, so this can't stay inline here.
        // Overrides Component::setName() to also reserve the name in
        // this View's RootView (if attached) - defined in view.cpp,
        // needs RootView's full definition. Redeclares name() too so
        // reflectgen pairs a real getter+setter on View itself, same
        // reason setBounds()/setVisible() above are redeclared here.
        void setName(const std::string& name);
        std::string name() const { return Component::name(); }

        virtual void addChild(SubView* child);
        virtual void removeChild(SubView* child);

        // Read-only view of this View's direct children, in the order
        // addChild() attached them - what a Layout arranges (see
        // Layout::arrange()).
        //
        // @reflect collection add=addChild remove=removeChild - registers
        // as a real newui::reflection propertyCollection (reflection.md),
        // so ObjectReader::read() drives the live addChild()/removeChild()
        // for each element instead of leaving "childViews" a get-only,
        // never-reconstructed property - needed for Bundle::loadView()
        // (bundle.h) to actually rebuild a saved child tree.
        const std::vector<SubView*>& childViews() const {
            return childViews_;
        }

        // Depth-first search of this View and every descendant (self
        // first, then each child's own findView() in childViews() order)
        // for the first View whose name() == name. Returns nullptr if
        // nothing matches. name() has no uniqueness guarantee enforced
        // anywhere below the RootView-assigned defaults (see
        // RootView::generateDefaultName()) - a caller that hand-assigns
        // duplicate names via setName() just gets whichever match this
        // walk reaches first.
        View* findView(const std::string& name);
        const View* findView(const std::string& name) const;

        Layout* layout() const {
            return layout_.get();
        }

        // Swaps in a different Layout (e.g. std::make_unique<FlexLayout>())
        // to arrange childViews() automatically - see Layout and
        // updateLayout(). Pass nullptr to go back to manual positioning
        // (childViews() bounds are left exactly as they are until
        // something else sets them, same as before a Layout was ever
        // attached).
        void setLayout(std::unique_ptr<Layout> layout);

        // Re-runs layout()->arrange(*this), if a Layout is attached; a
        // no-op otherwise. Called automatically whenever this View's own
        // size changes (SubView::setBounds()/RootView::setBounds()) or
        // its child list changes (addChild()/removeChild()) - normal use
        // never needs to call this directly; it's exposed for the rare
        // case of forcing a re-arrange without either of those (e.g.
        // after mutating a child's LayoutParams in place).
        void updateLayout();

        ViewStyle& style() {
            return *style_;
        }

        const ViewStyle& style() const {
            return *style_;
        }

        // Swaps in a different ViewStyle (e.g. std::make_unique<ButtonStyle>())
        // to draw widget-specific chrome; see ViewStyle::paint().
        void setStyle(std::unique_ptr<ViewStyle> style) {
            if (nullptr != style_) {
                style_->setView(nullptr);
            }
            style_ = std::move(style);
            style_->setView(this);
        }

        void setHighlighted(bool highlighted) {
            highlighted_ = highlighted;
        }

        bool isHighlighted() const {
            return highlighted_;
        }

        // Draws background/border/highlight from style() - see ViewStyle.
        // Called automatically before paint() by whatever's orchestrating
        // the draw (paintChildren() for children, RootView::repaint() for
        // itself), so it always runs first without subclasses needing to
        // remember to call it.
        void paintStyle(BLContext& ctx);

        // The rect (local to this view, same coordinates paint() draws in)
        // left over for content/children after style()'s chrome (border,
        // 3D edge, checkbox glyph, ...) - see ViewStyle::computeClientBounds().
        // Computed live from the current style()/getBounds() every call, so
        // it's always correct with no dependency on paintStyle() ever
        // having run - safe to call from Layout::arrange() (see Layout's
        // class comment) or anywhere else that needs it before the first
        // paint.
        Rect getClientBounds() const {
            return style_ ? style_->computeClientBounds(bounds_.size())
                           : Rect(0.0f, 0.0f, bounds_.size().width, bounds_.size().height);
        }

        // "What size would this view like to be" - independent of its
        // current getBounds() size, so a Layout can consult it without
        // that being circular (Layout is what sets bounds in the first
        // place). Returns the explicit override set via setDesiredSize(),
        // if any; otherwise falls back to computeDesiredSize() below.
        Size desiredSize() const {
            return desiredSizeOverride_.has_value() ? *desiredSizeOverride_ : computeDesiredSize();
        }

        // Sets an explicit override, taking precedence over
        // computeDesiredSize() until clearDesiredSize() is called. Doesn't
        // itself move/resize this view - it's just a value a Layout (or
        // anything else) can read; nothing happens until something
        // consults it (e.g. FlexLayout's/GridLayout's arrange()).
        void setDesiredSize(const Size& size) {
            desiredSizeOverride_ = size;
        }

        // Reverts to computeDesiredSize()'s computed fallback.
        void clearDesiredSize() {
            desiredSizeOverride_.reset();
        }

        bool hasDesiredSizeOverride() const {
            return desiredSizeOverride_.has_value();
        }

        // Computed fallback used by desiredSize() when no explicit
        // override is set. Default just returns the current bounds size -
        // the same "natural size" proxy FlexLayout's non-wrap path (née
        // StackLayout) already relied on, so a view that never calls
        // setDesiredSize() or overrides this behaves exactly as it always
        // has. A SubView subclass (or a future ViewStyle-driven widget -
        // e.g. a label computing from font metrics, none exist yet) can
        // override this to compute a real answer instead of requiring
        // every caller to set one by hand.
        virtual Size computeDesiredSize() const {
            return bounds_.size();
        }

        // "How much content is there, total" - a different question from
        // desiredSize() above, which answers "what size would I like my
        // own bounds() to be" for a Layout. contentSize() is for a view
        // whose bounds() can legitimately stay smaller than everything it
        // actually holds (a long document in a scrolled text area, e.g.),
        // answering "how big would I have to be to show all of it" for
        // whoever owns the scrollbar - a generic container (ScrollView,
        // via this method - see its updateLayout()) or a control that
        // hand-rolls its own scrollbar and wants to ask a child the same
        // question without needing to know its concrete type.
        //
        // Delegate-based (onQueryContentSize, syncCallFirst - first
        // listener to claim SyncReturn::Handled wins), not a virtual
        // override like computeDesiredSize(), matching this toolkit's own
        // "prefer composition over a growing virtual surface" convention
        // (see Controller's class comment, controllers.h) - most views
        // never have an answer other than bounds_.size() (their content
        // IS their bounds), so this stays a no-op for them; a view that
        // does have a real answer (TextController, controls.h, is the
        // real consumer - see its own class comment) hooks
        // onQueryContentSize itself instead of every such view needing
        // its own dedicated accessor. A view that answers this is also
        // what ScrollView (controls.h) treats as *virtualized* content -
        // see onScrollOffsetChanged below, its counterpart.
        Size contentSize() {
            Size result = bounds_.size();
            onQueryContentSize.syncCallFirst(*this, result);
            return result;
        }

        //repaint this views clientBounds
        //typically 0,0 to bounds_.size().width, bounds_.size().height
        //but it could be less than this
        virtual void redraw();

        // How far this view's own children are shifted when painted/hit-
        // tested - a scroll offset, not this view's own position (that's
        // still bounds()/setBounds(), untouched). (0,0) (the default) is a
        // no-op: every child paints/hit-tests exactly where its own
        // bounds() already say. paintChildren() translates by -origin()
        // before walking children; hitTestChildren() tests against
        // localPt + origin() instead of raw localPt, so the two stay
        // consistent with each other - see their own doc comments. A
        // ScrollView-style container sets this on whichever child actually
        // hosts scrollable content (see controls.h's ScrollView), not on
        // itself, so its own always-visible chrome (scrollbars) stays put
        // regardless of scroll position.
        const Point& origin() const {
            return origin_;
        }

        void setOrigin(const Point& origin) {
            origin_ = origin;
        }

        // Draws this view's own content. Default is a no-op; SubView
        // subclasses override it to draw themselves. ctx is already
        // translated so (0,0) is this view's top-left corner and clipped to
        // its bounds.
        virtual void paint(BLContext& ctx) {}

        // Walks childViews_, translating/clipping ctx to each visible
        // child's bounds before calling its paint() and recursing into its
        // own children, so children draw on top of whatever's already in
        // the buffer in bounds-relative local coordinates.
        //
        // Deliberately no dirty-rect pruning (skipping a child whose
        // bounds don't intersect the region being repainted) - tried and
        // reverted, see HANDOFF.md: the geometry math checked out but it
        // produced real visual corruption live (wrong colors/stale
        // content on siblings that should have been left alone),
        // confirmed via a controlled test - removing it, and nothing
        // else, fixed the corruption immediately. Every visible child is
        // always walked unconditionally.
        void paintChildren(BLContext& ctx);

        // Finds the deepest visible descendant SubView whose bounds
        // contain localPt (a point in this View's own local coordinate
        // space - the same space getBounds()/paintChildren() use for
        // direct children), searching topmost-drawn-first (reverse child
        // order, matching paintChildren()'s draw order - a later-added
        // child paints over an earlier one, so it should also hit-test
        // first for overlapping bounds) and recursing into whichever
        // child it hits, so a click on a deeply nested SubView returns
        // that SubView directly, not just its top-level ancestor. Returns
        // nullptr (outLocalPt left untouched) if localPt isn't over any
        // visible child - the caller's own point is then still valid, in
        // this View's own local space, i.e. this View itself is the
        // target. Used by RootView to route mouse events to the right
        // SubView - see RootView::mouseDown()/mouseMove() etc.
        SubView* hitTestChildren(const Point& localPt, Point& outLocalPt) const;

        // Direct access to this View's Cursor (cursor.h) - see
        // RootView::handleMessage()'s WM_SETCURSOR case, which is what
        // actually calls ::SetCursor(resolvedCursor()) once per hovered/
        // captured View. Mutate cursor() in place for anything Cursor
        // supports (view->cursor().setCursorKind(CursorKind::Hand),
        // view->cursor().setPath("hand.png"), view->cursor().setImage(img)),
        // or replace it wholesale via setCursor(Cursor) below.
        Cursor& cursor() {
            return cursor_;
        }

        const Cursor& cursor() const {
            return cursor_;
        }

        // Replaces cursor() wholesale, e.g.
        // view->setCursor(newui::Cursor(newui::CursorKind::Hand)); or
        // view->setCursor(newui::Cursor("hand.png")). For a load that
        // might fail and needs checking, mutate in place instead:
        // if (!view->cursor().setPath("hand.png")) { ... }
        void setCursor(Cursor cursor) {
            cursor_ = std::move(cursor);
        }

        // Convenience for cursor().kind().
        CursorKind cursorKind() const {
            return cursor_.kind();
        }

        // Convenience for cursor().handle().
        HCURSOR resolvedCursor() const {
            return cursor_.handle();
        }

        SizeChangedDelegate onSizeChanged;
        VisibilityChangedDelegate onVisibilityChanged;
        CreatedDelegate onCreated;
        DestroyedDelegate onDestroyed;
        // See contentSize()'s own doc comment above.
        QueryContentSizeDelegate onQueryContentSize;

        // Fired by a scrolling container (ScrollView, controls.h) on a
        // *virtualized* content child - one that answered onQueryContentSize
        // above - instead of the ordinary "shift this view's whole
        // bounds/position via origin()" scrolling every other child gets.
        // A virtualized child's own bounds() stay pinned to whatever the
        // container gives it (typically its own viewport size) rather
        // than growing to match contentSize(); this is how the container
        // tells it "you're now scrolled to here" (in this view's own
        // content coordinate space - the same units contentSize()
        // reports in) so it can adjust whatever it draws internally.
        // Purely a notification, like onSizeChanged - most views never
        // subscribe (the same ones that never answer onQueryContentSize
        // either); TextController (controls.h) is the real consumer.
        // Deliberately not origin() - origin() already has a real,
        // established meaning (shift *this view's own children*), and a
        // virtualized view that also owns real children of its own (see
        // TextController's class comment, controls.h, for a live example
        // of exactly this collision) would have origin() reused for two
        // unrelated purposes at once, one of which was tried and reverted
        // this same session after breaking live (see HANDOFF.md).
        ScrollOffsetChangedDelegate onScrollOffsetChanged;

        // Fired by a view *on itself* whenever whatever contentSize()
        // would now report may have changed - not a bounds/size change
        // (see onSizeChanged for that; this view's own bounds() haven't
        // necessarily moved at all), but the logical content extent
        // behind it has (more text typed, a different font, ...). A
        // container hosting this view as a virtualized content child
        // (ScrollView, controls.h - see onScrollOffsetChanged above)
        // subscribes to this once, when the child's added, to know when
        // to re-run its own layout (bar visibility/range, re-pinning
        // this child's bounds) - see ScrollView::addChild(). Most views
        // never fire this at all - the same ones that never answer
        // onQueryContentSize either, since there'd be nothing for a
        // listener to usefully re-query.
        ContentSizeChangedDelegate onContentSizeChanged;

        // Fired by a view *on itself* to ask whoever's hosting it (if
        // anyone) to scroll just far enough that requestedRect - in this
        // view's own content coordinate space, the same units
        // contentSize() reports in - becomes fully visible. A no-op if
        // nothing's listening (standalone use) or if it's already
        // visible - the listener (ScrollView, controls.h, on a
        // virtualized content child - see onScrollOffsetChanged above)
        // decides that part, not the caller. TextController (controls.h)
        // fires this once per paint() with caret_'s own current on-screen
        // rect - see its own paint()'s comment for why one call site
        // there covers every caret-moving input path (typing, Backspace/
        // Delete, Enter, arrow keys, a click, ...) without each needing
        // its own call.
        RequestScrollIntoViewDelegate onRequestScrollIntoView;

		MouseEventDelegate onMouseDown;
        MouseEventDelegate onMouseUp;
        MouseEventDelegate onMouseMove;
        MouseWheelDelegate onMouseWheel;
        MouseEventDelegate onMouseEntered;
        MouseEventDelegate onMouseLeft;
        MouseEventDelegate onMouseDblClick;

		FocusDelegate onGotFocus;
        FocusDelegate onLostFocus;

		KeyEventDelegate onKeyPress;
        KeyEventDelegate onKeyDown;
        KeyEventDelegate onKeyUp;

        // Asked by RootView::setFocusedSubView() (rootview.h) before
        // taking focus away from this View / handing it to this View,
        // respectively - default true (no veto) so existing overrides
        // are unaffected. Returning false from either leaves
        // focusedSubView_ unchanged and fires no got/lostFocus event -
        // e.g. a control mid-validation with an invalid value can
        // refuse to give up focus until that's resolved.
        virtual bool canResignFocus() const { return true; }
        virtual bool canBecomeFocused() const { return true; }

        // Answers whether this View itself (not its children) can
        // currently carry out cmd - default false, so a View that
        // doesn't override either of these simply isn't part of the
        // chain RootView::canPerformCommand()/performCommand()
        // (rootview.h) walks. A View that wants to answer several
        // commands typically implements both by forwarding to a
        // CommandTable member (command.h) rather than writing its own
        // if/else chain over CommandId.
        virtual bool canPerformCommand(const CommandId& cmd) const { return false; }
        virtual void performCommand(const CommandId& cmd) {}

        // Per-View association for outgoing/incoming OLE drag-and-drop -
        // see newui::DropSource/newui::DropTarget's own class comments
        // (dragndrop.h). Null until explicitly opted into via
        // setDragSource()/setDropTarget() - most Views never call either,
        // so this costs nothing beyond one null pointer per View (same
        // "null until set" convention layout_ already uses below) rather
        // than every View paying for a DropSource/DropTarget's worth of
        // Delegates it will never use. COMDropTarget's own per-View
        // hit-testing walk and RootView's drag-gesture detection both
        // check these getters directly - neither one ever allocates.
        // @reflect ignore=true
        DropSource* dragSource() const { return dragSource_.get(); }
        // @reflect ignore=true
        void setDragSource(std::unique_ptr<DropSource> dragSource) { dragSource_ = std::move(dragSource); }

        // @reflect ignore=true
        DropTarget* dropTarget() const { return dropTarget_.get(); }
        // @reflect ignore=true
        void setDropTarget(std::unique_ptr<DropTarget> dropTarget) { dropTarget_ = std::move(dropTarget); }

        virtual bool initialize();
        virtual void destroy();

        // Non-owning upward back-reference (RootView::addChild()/
        // propagateRootView() set it, never this View) - reachable downward
        // from the real owner already (RootView/SubView's own childViews),
        // so recursively serializing it back out through here would walk
        // straight back into the same tree ObjectReader/ObjectWriter are
        // already walking to reach this View in the first place. For the
        // RootView subclass specifically this is also self-referential
        // (RootView's own constructor calls setRootView(this)), which made
        // this a real, reproduced infinite-recursion/stack-overflow bug the
        // moment reflectgen started registering an addressable, non-copy-
        // constructible getter like this as a real Property (see
        // reflectgen.py's collect_property_accessors()) - same "back-
        // reference, not a real owned sub-object" reasoning
        // RootView::getFrame() (rootview.h) is already ignore-annotated
        // for.
        //@reflect ignore=true
        RootView* rootView() {
            return rootView_;
        }

        // Same ignore reasoning as the non-const overload just above - a
        // reflectgen "@reflect ignore=true" comment only applies to the one
        // declaration it's directly attached to, not to sibling overloads,
        // so the const overload needs its own.
        //@reflect ignore=true
        const RootView* rootView() const {
            return rootView_;
        }

        void setRootView(RootView* val) {
            rootView_ = val;
        }

        // Same non-owning-upward-back-reference reasoning as rootView()
        // above - reachable downward already via the real parent's own
        // childViews, so letting reflectgen register this as a Property
        // would walk straight back into the same subtree ObjectReader/
        // ObjectWriter are already recursing through to reach this View.
        //@reflect ignore=true
        View* parent() const {
            return parent_;
        }

        void setParent(View* newParent) {
            parent_ = newParent;
        }

        // Sets rootView() on this View and recurses into every descendant
        // already in childViews_ - so attaching/detaching a SubView (sub)
        // tree that was built before (or after) it had a RootView still
        // gets every existing descendant's rootView() updated, not just
        // the immediate child being attached/detached. See
        // RootView::addChild()/removeChild() and
        // SubView::addChild()/removeChild(), which call this instead of
        // plain setRootView() for exactly that reason.
        void propagateRootView(RootView* root);

    protected:
        Rect bounds_;
        bool visible_ = false;

        std::optional<Size> desiredSizeOverride_;

        std::unique_ptr<ViewStyle> style_ = std::make_unique<ViewStyle>();
        bool highlighted_ = false;

        // Owns/frees any custom HCURSOR it loaded itself (RAII, see
        // cursor.h) - no explicit cleanup needed anywhere in View for
        // that.
        Cursor cursor_;

        std::unique_ptr<Layout> layout_;

        std::unique_ptr<DropSource> dragSource_;
        std::unique_ptr<DropTarget> dropTarget_;

        std::vector<SubView*> childViews_;

        Point origin_;

        View* parent_ = nullptr;

        RootView* rootView_ = nullptr;
    };

}

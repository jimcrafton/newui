#pragma once

#include <memory>
#include <optional>

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/layout.h>
#include <newui/uicomponent.h>
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
    class View : public UIComponent {
    public:
        virtual ~View() = default;

        typedef Delegate<View, const Size&> SizeChangedDelegate;
        typedef Delegate<View> VisibilityChangedDelegate;
        typedef Delegate<View> CreatedDelegate;
        typedef Delegate<View> DestroyedDelegate;

        typedef Delegate<View, const Point&, std::uint32_t, std::uint32_t> MouseEventDelegate;        
        typedef Delegate<View, const Point&, float> MouseWheelDelegate;

        typedef Delegate<View, std::uint32_t, int, int, std::uint32_t> KeyEventDelegate;

		typedef Delegate<View> FocusDelegate;


        const Rect& getBounds() const {
            return bounds_;
        }

        bool isVisible() const {
            return visible_;
        }

        void setName(const std::string& name) {
            name_ = name;
        }

        std::string getName() const {
            return name_;
        }

        virtual void addChild(SubView* child);
        virtual void removeChild(SubView* child);

        // Read-only view of this View's direct children, in the order
        // addChild() attached them - what a Layout arranges (see
        // Layout::arrange()).
        const std::vector<SubView*>& childViews() const {
            return childViews_;
        }

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

        // Draws this view's own content. Default is a no-op; SubView
        // subclasses override it to draw themselves. ctx is already
        // translated so (0,0) is this view's top-left corner and clipped to
        // its bounds.
        virtual void paint(BLContext& ctx) {}

        // Walks childViews_, translating/clipping ctx to each visible
        // child's bounds before calling its paint() and recursing into its
        // own children, so children draw on top of whatever's already in
        // the buffer in bounds-relative local coordinates.
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

        SizeChangedDelegate onSizeChanged;
        VisibilityChangedDelegate onVisibilityChanged;
        CreatedDelegate onCreated;
        DestroyedDelegate onDestroyed;

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

        virtual bool initialize();
        virtual void destroy();

        RootView* rootView() {
            return rootView_;
        }

        const RootView* rootView() const {
            return rootView_;
        }

        void setRootView(RootView* val) {
            rootView_ = val;
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

        // UIComponent: covers name_/bounds_/visible_ only - style_/layout_/
        // childViews_ are nested UIComponents of their own, composed by
        // the tree-walker in serialization.cpp, not written here. See
        // SubView/RootView for the typeName()-relevant subclass overrides.
        void writeFields(json5::builder& w) const override;
        void readFields(const json5::value& obj) override;

    protected:
        Rect bounds_;
        bool visible_ = false;
        std::string name_;

        std::optional<Size> desiredSizeOverride_;

        std::unique_ptr<ViewStyle> style_ = std::make_unique<ViewStyle>();
        bool highlighted_ = false;

        std::unique_ptr<Layout> layout_;

        std::vector<SubView*> childViews_;

        RootView* rootView_ = nullptr;
    };

}

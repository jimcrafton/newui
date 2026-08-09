#pragma once

#include <memory>

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/viewstyle.h>

namespace newui {

    class SubView;

    // Common state shared by View and SubView: bounds/visible/name storage
    // and the trivial accessors that read it identically in both. Anything
    // whose behavior differs between the two (setBounds, setVisible,
    // addChild/removeChild, initialize, destroy) stays in the derived class.
    class View {
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

        ViewStyle& style() {
            return *style_;
        }

        const ViewStyle& style() const {
            return *style_;
        }

        // Swaps in a different ViewStyle (e.g. std::make_unique<ButtonStyle>())
        // to draw widget-specific chrome; see ViewStyle::paint().
        void setStyle(std::unique_ptr<ViewStyle> style) {
            style_ = std::move(style);
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
        // remember to call it. Captures style()'s clientBounds output into
        // getClientBounds(), so paint() overrides know where to draw
        // without overlapping whatever chrome the style just painted.
        void paintStyle(BLContext& ctx);

        // The rect (local to this view, same coordinates paint() draws in)
        // left over after paintStyle()'s most recent run painted the
        // style's chrome - see ViewStyle::paint()'s clientBounds parameter.
        // Default-constructed (a zero rect) until the first paintStyle()
        // call sets it.
        const Rect& getClientBounds() const {
            return clientBounds_;
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
    protected:
        Rect bounds_;
        bool visible_ = false;
        std::string name_;

        Rect clientBounds_;

        std::unique_ptr<ViewStyle> style_ = std::make_unique<ViewStyle>();
        bool highlighted_ = false;

        std::vector<SubView*> childViews_;
    };

}

#pragma once

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/geometry.h>
#include <newui/color.h>
#include <newui/viewstyle.h>
#include <newui/shapes.h>

namespace newui {

    // Painted last by RootView::repaint(), on top of every child SubView -
    // see RootView::setOverlay()/overlay(). Owned by RootView, defaults to
    // null (nothing extra drawn at all).
    //
    // Not a SubView: it isn't part of childViews_, has no bounds()/hit-
    // testing/mouse routing of its own - just a final full-window pass
    // RootView drives directly after its own tree, for whatever needs to
    // sit above the entire view hierarchy regardless of z-order within it
    // (a modal dimming scrim, a drag-and-drop insertion indicator, a debug
    // overlay, ...).
    class Overlay {
    public:
        Overlay() = default;
        virtual ~Overlay() = default;

        // If false, RootView::repaint() skips this overlay entirely -
        // neither paint() nor the per-repaint work paint() would otherwise
        // do runs.
        bool visible() const { return visible_; }
        void setVisible(bool visible) { visible_ = visible; }

        // Flat fill drawn across the whole overlay rect before shapeLayer()
        // - transparent black (the default) means "no fill", leaving only
        // whatever shapeLayer() itself draws.
        const Color& fillColor() const { return fillColor_; }
        void setFillColor(const Color& color) { fillColor_ = color; }

        // Multiplies the fill's alpha - same convention as ViewStyle::opacity
        // (viewstyle.h). Doesn't affect shapeLayer(), which has its own
        // opacity/compositingOp (see ShapeLayer, shapes.h). Defaults to
        // 0.4 - light enough that a solid fillColor() still reads as a
        // tint over the content beneath rather than obscuring it.
        float opacity() const { return opacity_; }
        void setOpacity(float opacity) { opacity_ = opacity; }

        // How the fill blends with whatever every child SubView already
        // painted; see BLCompOp. Defaults to CompScreen (lightens what's
        // underneath rather than alpha-blending over it), a more typical
        // starting point for a tint/highlight overlay than plain
        // over-blending.
        CompositingFlag compositingOp() const { return compositingOp_; }
        void setCompositingOp(CompositingFlag op) { compositingOp_ = op; }

        // Shapes drawn on top of the flat fill above - see ShapeLayer
        // (shapes.h) for adding/removing shapes and its own opacity/
        // compositingOp.
        shapes::ShapeLayer& shapeLayer() { return shapeLayer_; }
        const shapes::ShapeLayer& shapeLayer() const { return shapeLayer_; }

        // Called by RootView::setBounds() right after the RootView's own
        // bounds_ is updated to newBounds - before that resize's repaint,
        // so any extra state this overlay keeps derived from the window
        // size (shape positions, a cached path, ...) can be brought up to
        // date first instead of paint() seeing it stale for one frame.
        // Base implementation is a no-op.
        virtual void viewSized(const Rect& newBounds) {}

        // Paints this overlay into ctx - already in the same root-local
        // coordinate space RootView's own paint() uses, (0,0) at the
        // window's top-left - with rect covering the RootView's current
        // bounds().size() at that origin. Only called when visible() is
        // true. Base implementation fills rect with fillColor()/opacity()/
        // compositingOp(), then renders shapeLayer() on top; a subclass
        // drawing something else instead can chain to Overlay::paint()
        // first to keep that base fill.
        virtual void paint(BLContext& ctx, const Rect& rect);

    private:
        bool visible_ = true;
        Color fillColor_ = Color(0.0f, 0.0f, 0.0f, 0.0f);
        float opacity_ = 0.4f;
        CompositingFlag compositingOp_ = CompScreen;
        shapes::ShapeLayer shapeLayer_;
    };

}

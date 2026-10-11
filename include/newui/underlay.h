#pragma once

#include <string>

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/geometry.h>
#include <newui/color.h>
#include <newui/graphics.h>
#include <newui/viewstyle.h>
#include <newui/shapes.h>

namespace newui {

    // Owned by PopupTool (popuptool.h) - conceptually Overlay's mirror
    // image (see overlay.h's own class comment): where Overlay is
    // painted last, on top of a RootView's own SubView tree, Underlay
    // *is* the whole visual surface a PopupTool window presents to the
    // screen. Its rendered pixels (image()) are exactly what
    // PopupTool::present() hands to UpdateLayeredWindow - see that
    // class's own comment for why a WS_EX_LAYERED popup can't just rely
    // on RootView's own ordinary WM_PAINT-driven repaint().
    //
    // Unlike Overlay (which paints into a BLContext someone else already
    // owns - RootView's own imageBuffer_), Underlay owns its own real
    // Win32-DIB-backed newui::gfx::Image - that's exactly what
    // UpdateLayeredWindow's hdcSrc parameter needs (gfx::Image::memDC()
    // already wraps the CreateDIBSection()/CreateCompatibleDC() dance -
    // see graphics.h), and it has to stay alive independently of - and
    // at a possibly different size/format than - whatever RootView's own
    // imageBuffer_ happens to be doing.
    class Underlay {
    public:
        Underlay() = default;
        virtual ~Underlay() = default;

        Underlay(const Underlay&) = delete;
        Underlay& operator=(const Underlay&) = delete;

        // (Re)allocates image() at newSize, discarding whatever was
        // rendered before - called by PopupTool whenever the popup
        // window's own bounds change, so image() always matches the
        // window instead of getting stretched/cropped by
        // UpdateLayeredWindow. A no-op for a non-positive width/height
        // (image() is left however it was - same "just don't crash"
        // convention RootView::resizeImageBuffer() already follows).
        virtual void viewSized(const Size& newSize);

        // If false, render() below clears image() to fully transparent
        // and skips paint() entirely - same "nothing extra drawn, at
        // all" meaning Overlay::visible() has (overlay.h).
        bool visible() const { return visible_; }
        void setVisible(bool visible) { visible_ = visible; }

        // Flat fill drawn across the whole popup before sourceImage()/
        // shapeLayer() below - transparent black (the default) means "no
        // fill," leaving only whatever sourceImage()/shapeLayer()
        // themselves draw. Same convention as Overlay::fillColor().
        const Color& fillColor() const { return fillColor_; }
        void setFillColor(const Color& color) { fillColor_ = color; }

        // Multiplies the fill's alpha - same convention as Overlay::
        // opacity()/ViewStyle::opacity. Doesn't affect sourceImage() or
        // shapeLayer(), which have their own opacity.
        float opacity() const { return opacity_; }
        void setOpacity(float opacity) { opacity_ = opacity; }

        // How the flat fill blends with whatever's already in image() -
        // see BLCompOp. Defaults to CompSrcOver (plain alpha-over),
        // unlike Overlay's own CompScreen default - Overlay tints
        // existing SubView content, but render() below always starts
        // this Underlay's buffer from a fully transparent clear_all(),
        // so there's nothing underneath yet for a "lighten" blend to
        // act on.
        CompositingFlag compositingOp() const { return compositingOp_; }
        void setCompositingOp(CompositingFlag op) { compositingOp_ = op; }

        // Shapes drawn on top of the flat fill/sourceImage() above - see
        // ShapeLayer (shapes.h) for adding/removing shapes and its own
        // opacity/compositingOp. This is the normal way to give the
        // popup its actual custom silhouette (a RoundRect, Path, ...) -
        // same vocabulary Overlay::shapeLayer() already uses.
        shapes::ShapeLayer& shapeLayer() { return shapeLayer_; }
        const shapes::ShapeLayer& shapeLayer() const { return shapeLayer_; }

        // Paints this Underlay's content into ctx - already cleared to
        // fully transparent (see render() below) and covering exactly
        // rect, (0,0) at image()'s own top-left. Base implementation
        // fills rect with fillColor()/opacity()/compositingOp(), blits
        // sourceImage() if loadImageFile() ever loaded one successfully,
        // then renders shapeLayer() on top - a subclass drawing
        // something else instead can chain to Underlay::paint() first to
        // keep that base content.
        virtual void paint(BLContext& ctx, const Rect& rect);

        // Runs one full paint pass: clears image()'s buffer to fully
        // transparent, then calls paint() above (skipped if visible() is
        // false - image() is left fully transparent). Not automatic/on a
        // timer - called by PopupTool::present() every time it needs a
        // fresh frame to push via UpdateLayeredWindow.
        void render();

        // Loads path (anything newui::gfx::Image's own (path, width,
        // height) constructor accepts - PNG/BMP/JPEG/QOI, or SVG,
        // rasterized directly at this Underlay's current size() -
        // graphics.h) as sourceImage(), the background paint() blits by
        // default. Returns false, leaving any previous sourceImage()
        // untouched, if path fails to load or size() isn't positive yet
        // (call viewSized() first) - see gfx::Image::isValid().
        bool loadImageFile(const std::string& path);

        gfx::Image& sourceImage() { return sourceImage_; }
        const gfx::Image& sourceImage() const { return sourceImage_; }

        // The rendered, DIB-backed buffer render() writes into - what
        // PopupTool::present() actually pushes to the screen. Only valid
        // after viewSized() has allocated it at least once.
        gfx::Image& image() { return image_; }
        const gfx::Image& image() const { return image_; }

        const Size& size() const { return size_; }

    private:
        gfx::Image image_;
        Size size_;
        gfx::Image sourceImage_;

        bool visible_ = true;
        Color fillColor_ = Color(0.0f, 0.0f, 0.0f, 0.0f);
        float opacity_ = 1.0f;
        CompositingFlag compositingOp_ = CompSrcOver;
        shapes::ShapeLayer shapeLayer_;
    };

}

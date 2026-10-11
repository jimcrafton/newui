#include "canvasview.h"

#include <newui/color.h>
#include <newui/fontmanager.h>
#include <newui/uicolormanager.h>

#include <cmath>
#include <stdexcept>
#include <string>

namespace sketch {

    using namespace newui;

    CanvasView::CanvasView() {
        setName("CanvasView");
        setVisible(true);
        setAcceptsFocus(true);
    }

    void CanvasView::setPageWidth(float width) {
        if (width <= 0.0f) {
            throw std::runtime_error("CanvasView::setPageWidth: width must be positive");
        }
        pageWidth_ = width;
        style().markDirty();
    }

    void CanvasView::setPageHeight(float height) {
        if (height <= 0.0f) {
            throw std::runtime_error("CanvasView::setPageHeight: height must be positive");
        }
        pageHeight_ = height;
        style().markDirty();
    }

    void CanvasView::setZoom(float zoom) {
        if (zoom <= 0.0f) {
            throw std::runtime_error("CanvasView::setZoom: zoom must be positive");
        }
        zoom_ = zoom;
        style().markDirty();
    }

    void CanvasView::setShowGrid(bool value) {
        showGrid_ = value;
        style().markDirty();
    }

    void CanvasView::setShowRulers(bool value) {
        showRulers_ = value;
        style().markDirty();
    }

    void CanvasView::setGridSpacing(float spacing) {
        if (spacing <= 0.0f) {
            throw std::runtime_error("CanvasView::setGridSpacing: spacing must be positive");
        }
        gridSpacing_ = spacing;
        style().markDirty();
    }

    void CanvasView::paint(BLContext& ctx) {
        Rect client = getClientBounds();
        if (client.width() <= 0.0f || client.height() <= 0.0f) {
            return;
        }

        float rulerSize = showRulers_ ? kRulerThickness : 0.0f;

        ctx.save();
        ctx.clip_to_rect(BLRect(client.left() + rulerSize, client.top() + rulerSize,
            client.width() - rulerSize, client.height() - rulerSize));

        // Pasteboard.
        ctx.set_fill_style(BLRgba32(0xFF7A7A7Au));
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), client.height()));

        // Page, with its top-left a fixed margin in from the viewport (pan arrives with S1).
        double pageLeft = client.left() + rulerSize + 20.0;
        double pageTop = client.top() + rulerSize + 20.0;
        double pageW = double(pageWidth_) * double(zoom_);
        double pageH = double(pageHeight_) * double(zoom_);
        ctx.set_fill_style(BLRgba32(0xFFFFFFFFu));
        ctx.fill_rect(BLRect(pageLeft, pageTop, pageW, pageH));

        if (showGrid_) {
            double step = double(gridSpacing_) * double(zoom_);
            // Below ~4px a grid is just noise.
            if (step >= 4.0) {
                BLPath grid;
                for (double x = step; x < pageW; x += step) {
                    double px = std::floor(pageLeft + x) + 0.5;
                    grid.move_to(px, pageTop);
                    grid.line_to(px, pageTop + pageH);
                }
                for (double y = step; y < pageH; y += step) {
                    double py = std::floor(pageTop + y) + 0.5;
                    grid.move_to(pageLeft, py);
                    grid.line_to(pageLeft + pageW, py);
                }
                ctx.set_stroke_style(BLRgba32(0xFFE7E7E7u));
                ctx.set_stroke_width(1.0);
                ctx.stroke_path(grid);
            }
        }

        ctx.set_stroke_style(BLRgba32(0x59000000u));
        ctx.set_stroke_width(1.0);
        ctx.stroke_rect(BLRect(pageLeft - 0.5, pageTop - 0.5, pageW + 1.0, pageH + 1.0));
        ctx.restore();

        if (showRulers_) {
            paintRulers(ctx, client);
        }
    }

    // Tick labels every 100 document units at the current zoom - enough for the scaffold; the
    // real ruler (adaptive tick spacing, cursor marker, guide dragging) comes with S7.
    void CanvasView::paintRulers(BLContext& ctx, const Rect& client) const {
        BLRgba32 face = UIColorManager::colorFor(UIColorRole::ControlBackground).toBLRgba32();
        BLRgba32 text = UIColorManager::colorFor(UIColorRole::ControlText).toBLRgba32();
        BLRgba32 edge = UIColorManager::colorFor(UIColorRole::ControlBorder).toBLRgba32();

        ctx.save();
        ctx.set_fill_style(face);
        ctx.fill_rect(BLRect(client.left(), client.top(), client.width(), kRulerThickness));
        ctx.fill_rect(BLRect(client.left(), client.top(), kRulerThickness, client.height()));
        ctx.set_stroke_style(edge);
        ctx.set_stroke_width(1.0);
        BLPath edges;
        edges.move_to(client.left(), client.top() + kRulerThickness - 0.5);
        edges.line_to(client.left() + client.width(), client.top() + kRulerThickness - 0.5);
        edges.move_to(client.left() + kRulerThickness - 0.5, client.top());
        edges.line_to(client.left() + kRulerThickness - 0.5, client.top() + client.height());
        ctx.stroke_path(edges);

        Font font = FontManager::getSystemFont(SystemUIFont::Status);
        BLFont* blFont = font.blFont();
        double originX = client.left() + kRulerThickness + 20.0;
        double originY = client.top() + kRulerThickness + 20.0;
        double step = 100.0 * double(zoom_);
        if (blFont != nullptr && blFont->is_valid() && step >= 20.0) {
            ctx.set_fill_style(text);
            BLPath ticks;
            for (int i = 0; ; ++i) {
                double x = originX + double(i) * step;
                if (x > client.left() + client.width()) {
                    break;
                }
                std::string label = std::to_string(i * 100);
                ticks.move_to(std::floor(x) + 0.5, client.top() + 12.0);
                ticks.line_to(std::floor(x) + 0.5, client.top() + kRulerThickness);
                ctx.fill_utf8_text(BLPoint(x + 3.0, client.top() + 11.0), *blFont, label.c_str(), label.size());
            }
            for (int i = 0; ; ++i) {
                double y = originY + double(i) * step;
                if (y > client.top() + client.height()) {
                    break;
                }
                std::string label = std::to_string(i * 100);
                ticks.move_to(client.left() + 12.0, std::floor(y) + 0.5);
                ticks.line_to(client.left() + kRulerThickness, std::floor(y) + 0.5);
                ctx.fill_utf8_text(BLPoint(client.left() + 2.0, y + 11.0), *blFont, label.c_str(), label.size());
            }
            ctx.set_stroke_style(edge);
            ctx.stroke_path(ticks);
        }
        ctx.restore();
    }

}

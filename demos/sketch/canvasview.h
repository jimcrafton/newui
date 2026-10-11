#pragma once

#include <newui/subview.h>

namespace sketch {

    // The drawing surface: the gray pasteboard, the white page with its grid, and (later) the
    // document's objects, the selection overlay and the rulers - all painted by this one view,
    // since a ruler is meaningless without the canvas's zoom/pan. Sketch's own "CanvasView
    // (custom SubView)" from demos/sketch/mockup/sketch-mockup.html.
    //
    // S0 scaffold: paints the pasteboard, page and grid only; SketchDocument/tools (PLAN.md S1/S2)
    // hook in here. The properties below are what a .newui file may set (see sketch.newui).
    // @reflect category=basic
    class CanvasView : public newui::SubView {
    public:
        // Declared (not implicit) so reflectgen registers it - a .newui load builds one through it.
        CanvasView();
        virtual ~CanvasView() {}

        // Page size in document units (points). A US Letter portrait page by default.
        float pageWidth() const { return pageWidth_; }
        void setPageWidth(float width);
        float pageHeight() const { return pageHeight_; }
        void setPageHeight(float height);

        // 1.0 == 100%. Always positive.
        float zoom() const { return zoom_; }
        void setZoom(float zoom);

        bool showGrid() const { return showGrid_; }
        void setShowGrid(bool value);

        bool showRulers() const { return showRulers_; }
        void setShowRulers(bool value);

        // Grid spacing in document units.
        float gridSpacing() const { return gridSpacing_; }
        void setGridSpacing(float spacing);

        void paint(BLContext& ctx) override;

    private:
        static constexpr float kRulerThickness = 20.0f;

        void paintRulers(BLContext& ctx, const newui::Rect& client) const;

        float pageWidth_ = 612.0f;
        float pageHeight_ = 792.0f;
        float zoom_ = 1.0f;
        float gridSpacing_ = 20.0f;
        bool showGrid_ = true;
        bool showRulers_ = true;
    };

}

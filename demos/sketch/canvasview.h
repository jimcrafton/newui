#pragma once

#include "sketchdocument.h"

#include <newui/subview.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace sketch {

    // What a left-button drag on the canvas does.
    // DirectSelect, Pen, Pencil and Text are palette entries whose behavior lands in later phases (S5): they
    // are selectable so the palette stays a radio group, but a drag with them does nothing yet.
    enum class SketchTool { Select, DirectSelect, Rectangle, Ellipse, Line, Pen, Pencil, Text, Gradient, Hand, Zoom };

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

        // Draw and move drags jump to the grid when within a few pixels of a grid line (like Illustrator's
        // snap-to-point; the grid need not be visible).
        bool snapToGrid() const { return snapToGrid_; }
        void setSnapToGrid(bool value);

        // The document painted on the page; null paints an empty page. Not owned. The view
        // registers itself with the document (Model::addView) so edits repaint it.
        // @reflect ignore=true
        SketchDocument* document() const { return document_; }
        // @reflect ignore=true
        void setDocument(SketchDocument* document);

        // Where the page's top-left sits, in view pixels measured from the top-left of the
        // drawing area (just inside the rulers).
        float panX() const { return panX_; }
        float panY() const { return panY_; }
        void setPan(float x, float y);

        // Sets zoom so the document point under view point `anchor` stays put (wheel zoom).
        // @reflect ignore=true
        void zoomAt(const newui::Point& anchor, float newZoom);

        // View pixels <-> document units (points), for the current zoom and pan.
        // @reflect ignore=true
        BLPoint toDocument(const newui::Point& viewPoint) const;
        // @reflect ignore=true
        BLPoint toView(const BLPoint& documentPoint) const;

        // Zoom so the whole page fits the drawing area with a small margin, and re-center it.
        void zoomToFit();

        // The active tool. Select: click/shift-click/marquee, drag to move, handles to resize/rotate.
        // Rectangle/Ellipse/Line: drag out a shape (Shift = square/circle/45 degrees). Hand: drag pans.
        // Gradient: drag across the selected object(s) to lay a gradient axis (a solid fill becomes a linear
        // gradient; Shift = 45 degrees); drag the axis ends and stops of a selected gradient; click the axis to
        // add a stop, Alt-click a stop to remove it.
        // @reflect ignore=true
        SketchTool tool() const { return tool_; }
        // @reflect ignore=true
        void setTool(SketchTool tool);
        newui::Delegate<CanvasView> onToolChanged;

        // The pointer over the page, in document units (status bar coordinates); fired on every move.
        BLPoint pointerPosition() const { return pointerDoc_; }
        newui::Delegate<CanvasView> onPointerMoved;

        // Which paint the Gradient tool edits: the fill (default) or the stroke - follows the Color panel's target.
        bool gradientOnStroke() const { return gradientOnStroke_; }
        void setGradientOnStroke(bool stroke);
        // The gradient stop the tool has selected (highlighted on the axis; the Color panel edits the same one).
        // Setting it from outside does not fire onGradientStopSelected.
        std::size_t selectedGradientStop() const { return gradientSelectedStop_; }
        void setSelectedGradientStop(std::size_t index);
        newui::Delegate<CanvasView> onGradientStopSelected;

        // Fired whenever zoom() changes (wheel, setZoom, zoomToFit) - the zoom combo and status
        // bar follow it.
        typedef newui::Delegate<CanvasView> ViewChangedDelegate;
        ViewChangedDelegate onZoomChanged;

        // Ctrl+<key> shortcuts the canvas does not own (File menu: N/O/S); return true when handled.
        // @reflect ignore=true
        std::function<bool(std::uint32_t vkCode, bool shift)> onControlKey;

        void paint(BLContext& ctx) override;

    private:
        static constexpr float kRulerThickness = 20.0f;
        static constexpr float kMinZoom = 0.05f;
        static constexpr float kMaxZoom = 64.0f;

        void paintRulers(BLContext& ctx, const newui::Rect& client) const;
        float rulerSize() const { return showRulers_ ? kRulerThickness : 0.0f; }

        newui::SyncReturn handleMouseDown(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseMove(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleMouseUp(newui::View& sender, const newui::Point& pt, std::uint32_t buttons, std::uint32_t keys);
        newui::SyncReturn handleKeyDown(newui::View& sender, std::uint32_t keyMask, int keyChar, int repeatCount, std::uint32_t vkCode);
        newui::SyncReturn handleMouseWheel(newui::View& sender, const newui::Point& pt, float delta);

        float pageWidth_ = 612.0f;
        float pageHeight_ = 792.0f;
        float zoom_ = 1.0f;
        float gridSpacing_ = 20.0f;
        bool showGrid_ = true;
        bool showRulers_ = true;
        bool snapToGrid_ = true;
        BLPoint pointerDoc_;
        BLBox dragBounds_;  // the selection's bounds when a move began
        // `value` moved onto the nearest grid line when it is within snapThreshold px of one.
        double snapValue(double value) const;
        BLPoint snapPoint(const BLPoint& point) const { return BLPoint(snapValue(point.x), snapValue(point.y)); }
        bool centered_ = true;  // cleared by the first manual pan/zoom
        float panX_ = 20.0f;
        float panY_ = 20.0f;
        SketchDocument* document_ = nullptr;
        bool panning_ = false;
        newui::Point panAnchor_;
        float panStartX_ = 0.0f;
        float panStartY_ = 0.0f;

        // ---- Tool interaction ----
        enum class Drag { None, Move, Resize, Rotate, Shear, Warp, Origin, Marquee, Draw, GradientNew, GradientEnd, GradientStop };
        // Handles 0..7 clockwise from the top-left corner; 8 is the rotate knob above top-center.
        // 9 is the transform-origin marker.
        static constexpr int kRotateHandle = 8;
        static constexpr int kOriginHandle = 9;
        static constexpr double kHandleHit = 6.0;
        static constexpr double kRotateKnobOffset = 22.0;

        // Handle position in view pixels (the rotate knob sits kRotateKnobOffset px out along the frame's "up").
        BLPoint handlePosition(int handle, const SelectionFrame& frame) const;
        int hitHandle(const newui::Point& viewPoint) const;
        // Alt+Shift on a corner starts a perspective warp, on a middle handle a shear; on an already warped
        // (quad) frame every handle edits the quad. The origin marker moves the pivot.
        void beginResize(int handle, std::uint32_t keys);
        void updateDrag(const newui::Point& pt, std::uint32_t keys);
        void finishDrag(const newui::Point& pt, std::uint32_t keys);
        // Re-applies `matrix` to the pre-drag outlines; the frame angles become their old values + angleDelta.
        void applyToSelection(const BLMatrix2D& matrix, double angleDelta = 0.0);
        void applyMapToSelection(const std::function<BLPoint(const BLPoint&)>& map, const QuadFrame& newQuad);
        void paintOverlay(BLContext& ctx) const;
        // Select tool: the cursor over a handle (resize arrows that follow the frame's turn, rotate, pivot).
        void updateHoverCursor(const newui::Point& pt);

        // ---- Gradient tool ----
        // The tool edits the fill or the stroke paint of the selection.
        const SketchPaint& gradientPaint(const SketchObject& object) const { return gradientOnStroke_ ? object.stroke() : object.fill(); }
        const SketchPaint& gradientPaint(const StyleState& state) const { return gradientOnStroke_ ? state.stroke : state.fill; }
        void setGradientPaint(SketchObject& object, const SketchPaint& paint) const {
            if (gradientOnStroke_) { object.setStroke(paint); } else { object.setFill(paint); }
        }
        // The handle that reshapes a radial gradient (view pixels): where its across radius ends - out from the
        // center, across the axis, leaning by `skew`. Dragging it sets the aspect and the lean together.
        static BLPoint aspectHandle(const BLPoint& center, const BLPoint& end, double aspect, double skew);
        // The handle that tilts a linear gradient's lines of equal color: a fixed distance from the middle of the
        // axis along the direction those lines run.
        static BLPoint tiltHandle(const BLPoint& start, const BLPoint& end, double skew);
        // The one selected object whose fill is a gradient (the tool's handles belong to it), or null.
        SketchObject* gradientTarget() const;
        // The target's gradient axis in view pixels (linear: start..end; radial: center..a point on the circle).
        bool gradientAxisView(const SketchObject& object, BLPoint& start, BLPoint& end) const;
        // Mouse-down with the gradient tool: grabs an axis end or stop of the target, adds a stop on its axis,
        // or starts dragging out a new axis on the (selected / clicked) objects.
        void beginGradientDrag(const newui::Point& pt, std::uint32_t keys);
        void updateGradientDrag(std::uint32_t keys);
        void finishGradientDrag();
        void cancelGradientDrag();
        void paintGradientOverlay(BLContext& ctx) const;

        bool gradientOnStroke_ = false;
        std::size_t gradientSelectedStop_ = 0;
        std::vector<StyleState> gradientBefore_;  // fills when a gradient drag began
        int gradientEnd_ = 0;                     // GradientEnd: 0 = start / center, 1 = end / radius point
        std::size_t gradientStop_ = 0;            // GradientStop: index of the stop being moved
        std::string gradientWhat_;                // undo step name

        SketchTool tool_ = SketchTool::Select;
        Drag drag_ = Drag::None;
        BLPoint dragStartDoc_;
        BLPoint dragNowDoc_;
        newui::Point dragNowView_;
        std::vector<GeometryState> dragBefore_;
        FrameState dragFrameBefore_;  // the document group angle / quad frame when a drag began
        SelectionFrame dragFrame_;  // selection frame when a resize/rotate began
        int dragHandle_ = 0;
        double rotateStartAngle_ = 0.0;
        BLPoint dragOrigin_;     // transform origin (document units) when a rotate/shear began
        BLPoint warpQuad_[4];    // the distorted frame corners (TL, TR, BR, BL) while warping
        bool dragMoved_ = false;
        bool dragAdditive_ = false;
    };

}

#pragma once

#include "sketchdocument.h"

#include <newui/controls.h>
#include <newui/rootview.h>
#include <newui/segmentedcontrol.h>

#include "gradientbar.h"
#include "swatchgrid.h"

#include <functional>
#include <string>
#include <vector>

namespace sketch {

    // Wires the Color / Stroke / Appearance panels of sketch.newui to the document: the panels show the
    // style of the selection (or the defaults new shapes get, with nothing selected) and edits flow back
    // as undoable style steps - a slider drag is one step, recorded on mouse-up.
    //
    // Solid/gradient/no fill and stroke in RGB, CMYK, Gray or HSB (the four component sliders follow the
    // color's own space; the mode buttons convert it), stroke weight, whole-object opacity, swatches.
    // Cap/join/dash and the appearance stack follow in later phases.
    // @reflect ignore=true
    class StylePanel {
    public:
        enum class Target { Fill, Stroke };

        StylePanel(newui::RootView& root, SketchDocument& document);
        StylePanel(const StylePanel&) = delete;
        StylePanel& operator=(const StylePanel&) = delete;

        Target target() const { return target_; }
        void setTarget(Target target);

        // Re-reads the selection (or defaults) into every control.
        void refresh();

    private:
        SketchPaint targetPaint() const;
        // Applies `paint` to the target of the selection (or the defaults) and records an undo step unless a
        // drag is in progress.
        void applyPaint(const SketchPaint& paint, const char* description);
        // Applies the current slider values as a solid color.
        // Sets the color being edited: the solid color, or the selected stop of a gradient.
        void applyColor(const SketchColor& color, const char* description);
        // Replaces the target paint with a gradient of the given kind (Linear or Radial).
        void applyGradient(const SketchGradient& gradient, SketchPaint::Kind kind, const char* description);
        // The color the sliders and hex field show.
        SketchColor displayedColor() const;
        void applyGradientFields();
        void applySlidersAsColor();
        void applyHex();
        void applyStrokeWeight();
        void applyOpacity();

        void beginDrag();
        void endDrag();
        // Runs `edit` (which changes the selection live) and either records a step now or leaves it to endDrag().
        void edit(const char* description, const std::function<void()>& onSelection, const std::function<void()>& onDefaults);

        template <typename T>
        T* find(const char* name) const {
            return dynamic_cast<T*>(root_.findView(name));
        }
        void syncWells();
        void syncSlider(const char* sliderName, float value);

        newui::RootView& root_;
        SketchDocument& document_;
        Target target_ = Target::Fill;
        bool updating_ = false;
        bool dragging_ = false;
        std::string dragDescription_;
        std::vector<StyleState> dragBefore_;
        SketchColor lastSolid_ = SketchColor::fromHex("#e8452c");
        std::size_t selectedStop_ = 0;  // index into the target gradient's stops
    };

}

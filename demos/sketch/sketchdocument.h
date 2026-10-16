#pragma once

#include "pathops.h"
#include "sketchobject.h"

#include <newui/delegate.h>
#include <newui/models.h>
#include <newui/undostack.h>

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace sketch {

    // An object's outline and frame angle at one moment - what undo of a move/resize/rotate restores.
    // @reflect ignore=true
    struct GeometryState {
        std::size_t id = 0;
        BLPath path;
        double angle = 0.0;
        BLMatrix2D textMatrix = BLMatrix2D::make_identity();   // a Text object's placement (see SketchObject::textMatrix)
    };

    // Four world-space corners (TL, TR, BR, BL) of a possibly distorted frame, with the projective map
    // between the unit square and the quad.
    // @reflect ignore=true
    struct QuadFrame {
        BLPoint p[4];

        BLPoint map(double u, double v) const;
        // Inverse of map(); false when the quad is degenerate.
        bool unmap(const BLPoint& world, double& u, double& v) const;
        QuadFrame transformed(const BLMatrix2D& matrix) const;
    };

    // Opacity and blend mode of a whole group, applied to its members drawn together.
    // @reflect ignore=true
    struct GroupProps {
        double opacity = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        bool isDefault() const { return opacity >= 1.0 && blendMode == BlendMode::Normal; }
    };

    // A named appearance the user saved: the whole stack (fills, strokes, effects) plus the object's opacity and blend
    // mode. Applying one replaces those on every selected object. Gradients are stored relative to the object they paint,
    // so a style looks the same on a shape of any size.
    // @reflect ignore=true
    struct GraphicStyle {
        std::string name;
        std::vector<AppearanceItem> items;
        double opacity = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        bool operator==(const GraphicStyle& o) const { return name == o.name && items == o.items && opacity == o.opacity && blendMode == o.blendMode; }
    };
    // The styles a new document starts with.
    std::vector<GraphicStyle> defaultGraphicStyles();

    // An object's appearance at one moment - what undo of a style edit restores.
    // @reflect ignore=true
    struct StyleState {
        std::size_t id = 0;
        std::vector<AppearanceItem> items;
        double opacity = 1.0;
        BlendMode blendMode = BlendMode::Normal;
        std::size_t activeFill = 0;    // the active items (not part of ==: choosing a row is not an edit)
        std::size_t activeStroke = 1;
        std::size_t activeEffect = 0;
        // The object's outermost group (0 = none) and that group's props, so a group edit undoes with the objects.
        std::size_t group = 0;
        GroupProps groupProps;

        static StyleState of(const SketchObject& object) {
            return StyleState{object.id(), object.appearance(), object.opacity(), object.blendMode(), object.activeFillIndex(), object.activeStrokeIndex(), object.activeEffectIndex(), 0, GroupProps()};
        }
        void applyTo(SketchObject& object) const {
            object.setAppearance(items);
            object.setActiveIndices(activeFill, activeStroke, activeEffect);
            object.setOpacity(opacity);
            object.setBlendMode(blendMode);
        }
        // The active fill / stroke and its options (what the Color / Stroke panels edit).
        const SketchPaint& fill() const { return activeItemOf(items, AppearanceKind::Fill, activeFill).paint; }
        const SketchPaint& stroke() const { return activeItemOf(items, AppearanceKind::Stroke, activeStroke).paint; }
        const StrokeOptions& strokeOptions() const { return activeItemOf(items, AppearanceKind::Stroke, activeStroke).strokeOptions; }
        bool operator==(const StyleState& o) const { return id == o.id && items == o.items && opacity == o.opacity && blendMode == o.blendMode && group == o.group &&
            groupProps.opacity == o.groupProps.opacity && groupProps.blendMode == o.groupProps.blendMode; }
        bool operator!=(const StyleState& o) const { return !(*this == o); }
    };

    // The oriented box around the selection: `local` is measured in a frame turned by `angle`
    // (local = R(-angle) * world), so handles follow a rotated shape the way Illustrator's do.
    // @reflect ignore=true
    struct SelectionFrame {
        double angle = 0.0;
        BLBox local = BLBox(0, 0, 0, 0);

        BLPoint toWorld(double lx, double ly) const;
        BLPoint toLocal(const BLPoint& world) const;
        BLPoint center() const { return toWorld((local.x0 + local.x1) * 0.5, (local.y0 + local.y1) * 0.5); }

        // Set after a warp: the frame is then this quad instead of the rectangle above.
        std::optional<QuadFrame> quad;

        // The four corners as a quad (the rectangle's, or the warped one).
        QuadFrame asQuad() const;
        // World point at fraction (u, v) of the frame (0,0 = top-left, 1,1 = bottom-right) and its inverse.
        BLPoint pointAt(double u, double v) const { return asQuad().map(u, v); }
        bool unmapPoint(const BLPoint& world, double& u, double& v) const { return asQuad().unmap(world, u, v); }
    };

    // The selection-level frame state that an undo step must restore along with the outlines.
    // @reflect ignore=true
    struct FrameState {
        std::optional<double> groupAngle;
        std::optional<QuadFrame> quad;
    };

    // The document: a page (size in points) and a flat, back-to-front list of objects. Layers and
    // groups arrive with S3b; undoable commands with S2 - until then the mutators below are called
    // directly and each ends with changed(), which marks the document modified and repaints its views.
    // File I/O is SVG (sketchsvg.h): load()/save() go through readFromFile/writeToFile.
    class SketchDocument : public newui::Document {
    public:
        SketchDocument() = default;

        // ---- Graphic Styles (saved with the document, see sketchsvg.cpp) ----
        const std::vector<GraphicStyle>& graphicStyles() const { return graphicStyles_; }
        // Replaces the whole list with no undo step (a load, New). Does not mark the document modified.
        void setGraphicStyles(std::vector<GraphicStyle> styles) { graphicStyles_ = std::move(styles); onChanged(*this); }
        // Each of these is one undo step and returns false when nothing happened.
        // Saves the first selected object's appearance as a new style ("Style 1", "Style 2"...), at the end of the list.
        bool addGraphicStyleFromSelection();
        bool deleteGraphicStyle(std::size_t index);
        // Replaces the appearance, opacity and blend mode of every selected object with the style's.
        bool applyGraphicStyle(std::size_t index);

        // Page size in points (1/72 in), and the pixel density used when exporting/previewing.
        float pageWidth() const { return pageWidth_; }
        float pageHeight() const { return pageHeight_; }
        void setPageSize(float width, float height);
        float ppi() const { return ppi_; }
        void setPpi(float ppi);
        // The page's fill: white by default; nullopt = transparent (shown as a checkerboard, written without a
        // background rectangle). Not undoable.
        const std::optional<SketchColor>& pageBackground() const { return pageBackground_; }
        void setPageBackground(const std::optional<SketchColor>& color);

        std::size_t objectCount() const { return objects_.size(); }
        // Back-to-front: index 0 is painted first. Throws std::runtime_error when out of range.
        SketchObject& object(std::size_t index) const;

        // Takes ownership, assigns a fresh id, appends on top. Returns the object for chaining.
        SketchObject& add(SketchObject* object);
        // Removes and destroys the object with this id; false if there is none.
        bool remove(std::size_t id);
        SketchObject* findById(std::size_t id) const;

        // Topmost visible, unlocked object under point (document units), or nullptr.
        SketchObject* hitTest(const BLPoint& point, double tolerance) const;

        // ---- Groups ----
        // A group is a set of objects sharing a group id in SketchObject::groups() (nested groups: several ids), kept
        // adjacent in the stacking order. Selecting one member selects the whole outermost group (the canvas does it
        // through withGroupMembers; Ctrl-click bypasses it to pick a single member).
        std::size_t newGroupId() { return nextGroupId_++; }
        // Group opacity / blend mode (default: opaque, normal). Setting is not an undo step by itself - the Appearance panel
        // edits live and commits with commitStyle(), whose snapshots carry the group's props.
        GroupProps groupProps(std::size_t groupId) const;
        void setGroupProps(std::size_t groupId, const GroupProps& props);
        // The outermost group the selection is exactly (all its members and nothing else), else 0.
        std::size_t selectedGroup() const;
        void setSelectionGroupOpacity(double opacity);
        void setSelectionGroupBlendMode(BlendMode mode);
        // `ids` plus every visible, unlocked object sharing an outermost group with one of them.
        std::vector<std::size_t> withGroupMembers(const std::vector<std::size_t>& ids) const;
        // Groups the selection (two or more objects) into a new outermost group, placed where its topmost member was;
        // undoable. False when fewer than two objects are selected.
        bool groupSelection();
        // Dissolves the outermost group of each selected object (the objects stay selected); undoable. False when none is grouped.
        bool ungroupSelection();
        bool canGroup() const { return selection_.size() >= 2; }
        bool canUngroup() const;

        // ---- Selection (ids, in selection order; a removed object drops out) ----
        const std::vector<std::size_t>& selection() const { return selection_; }
        bool isSelected(std::size_t id) const;
        void setSelection(const std::vector<std::size_t>& ids);
        void selectAll();
        void clearSelection() { setSelection({}); }
        // Union of the selected objects' geometryBounds(); false when nothing is selected.
        bool selectionBounds(BLBox& out) const;
        // The oriented frame. One object: its own frame angle. Several: their shared angle when they all
        // agree, else the group angle (set when the group is rotated - Illustrator keeps a rotated group
        // frame even when its members are turned differently), else upright.
        // The group angle is dropped whenever the selection changes.
        std::optional<double> groupAngle() const { return groupAngle_; }
        void setGroupAngle(std::optional<double> angle) { groupAngle_ = angle; }
        // The warped frame (set by a perspective warp; the selection frame becomes this quad). Dropped
        // with the group angle when the selection changes.
        std::optional<QuadFrame> quadFrame() const { return quadFrame_; }
        void setQuadFrame(const std::optional<QuadFrame>& quad) { quadFrame_ = quad; }
        FrameState frameState() const { return FrameState{groupAngle_, quadFrame_}; }
        bool selectionFrame(SelectionFrame& out) const;
        // The transform origin (rotate/shear pivot): kept as a fraction of the frame's box so it rides along
        // with moves, resizes and rotations; back to the center whenever the selection changes. View state - not undoable.
        BLPoint selectionOrigin(const SelectionFrame& frame) const;
        void setSelectionOrigin(const SelectionFrame& frame, const BLPoint& world);
        typedef newui::Delegate<SketchDocument> SelectionChangedDelegate;
        SelectionChangedDelegate onSelectionChanged;

        // ---- Undoable edits (each pushes one step on undoStack()) ----
        newui::UndoStack& undoStack() { return undoStack_; }
        // Adds on top and selects it.
        // Adds the objects (new ids) as one undo step and selects them; returns the ids.
        std::vector<std::size_t> addObjectsUndoable(std::vector<std::unique_ptr<SketchObject>> objects, const std::string& description);
        SketchObject& addUndoable(SketchObject* object, const std::string& description);
        // Deletes the selection.
        void deleteSelection();
        // Duplicates the selection, offset by (dx, dy); the copies become the selection.
        void duplicateSelection(double dx, double dy);
        // Clipboard: the selection as SVG text (empty when nothing is selected), and the reverse - the objects in `svg`
        // are added (moved by dx, dy) as one undo step and become the selection. Returns false when `svg` has no objects.
        std::string selectionToSvg() const;
        bool pasteSvg(const std::string& svg, double dx, double dy);
        // Records an edit that has already been applied live (a drag): `before` holds each edited
        // object's pre-edit outline, the objects' current outlines are the "after".
        void commitGeometry(const std::vector<GeometryState>& before, const std::string& description,
            const FrameState& frameBefore = FrameState());
        // Applies `matrix` to the given objects as one undo step.
        void transformObjects(const std::vector<std::size_t>& ids, const BLMatrix2D& matrix, const std::string& description);
        // Selected objects' ids and current outlines - the `before` for a drag.
        std::vector<GeometryState> snapshotSelection() const;
        // Redraw after an edit made directly on an object (drag preview).
        void touch() { changed(); }

        // ---- Arrange / align / transform / lock / hide: each is one undo step on the selection, and returns
        // false (and records nothing) when it would change nothing ----
        enum class ArrangeOp { BringToFront, BringForward, SendBackward, SendToBack };
        // Moves the selected objects within the stacking order (index 0 is the back). Forward/Backward step past
        // one unselected neighbor, as in Illustrator.
        bool arrangeSelection(ArrangeOp op);
        enum class AlignMode { Left, Center, Right, Top, Middle, Bottom };
        // Aligns the selection's objects to the edge/center of their combined bounds - or, for one object, of the page.
        bool alignSelection(AlignMode mode);
        // Turns / mirrors the selection about the center of its bounds (the frame angles follow).
        bool rotateSelection90(bool clockwise);
        bool flipSelection(bool horizontal);

        // ---- Cutting and combining (documentcutting.cpp). Each is one undo step; the pieces wear the look of what they came from
        // and take its place in the stacking order; the new objects become the selection. ----

        // Objects swapped for others: `remove` are taken out and `add` put in where they were (at the lowest removed position, or at
        // the highest when `atTop`; at the top of the stack when nothing is removed).
        struct Replacement {
            std::vector<std::size_t> remove;
            std::vector<std::unique_ptr<SketchObject>> add;
            bool atTop = false;
        };
        // Applies every replacement as ONE undo step called `description`; returns the ids of all the objects added.
        std::vector<std::size_t> replaceUndoable(std::vector<Replacement> replacements, const std::string& description);
        // Pathfinder on the selected objects (two or more, by their outlines): Unite, Intersect, Exclude, or Subtract (the lowest
        // object minus every one above it). The result is one path with the look of the top object (of the lowest for Subtract); curves
        // become fine straight pieces. False, and nothing changes, when fewer than two are selected or the result would be empty.
        bool combineSelection(BoolOp op);
        // The Knife: the line `cut` (document units) divides the filled shapes it crosses into pieces, along the line carried straight on
        // past its two ends. When some of the shapes it crosses are selected only those are cut, otherwise all of them. False when
        // nothing was cut.
        bool knifeCut(const std::vector<BLPoint>& cut);
        // The Eraser: removes what a pen `width` wide dragged along `stroke` touches - parts of filled shapes, parts of lines. When some of
        // what it touches is selected only that is erased, otherwise everything it touches. False when it touched nothing.
        bool erase(const std::vector<BLPoint>& stroke, double width);
        // The Scissors: cuts the path nearest to `at` (within `tolerance`) at that point, keeping its curves. A closed path opens; an
        // open one divides in two. False when no path is close enough (or the point is at the end of an open path).
        bool scissorsCut(const BLPoint& at, double tolerance);
        // Lock/Hide act on the selection (which is then cleared - a locked or hidden object cannot be selected);
        // Unlock All / Show All act on the whole document.
        bool lockSelection();
        bool hideSelection();
        bool unlockAll();
        bool showAll();

        // ---- Style (fill / stroke / weight / opacity) ----
        // The setters below change the selected objects live, with no undo step - a slider drag calls them
        // many times; the caller then records one step with commitStyle(). With nothing selected the
        // panel edits the defaults new shapes are drawn with instead (no undo).
        std::vector<StyleState> snapshotStyle() const;
        void setSelectionFill(const SketchPaint& paint);
        void setSelectionStroke(const SketchPaint& paint);
        void setSelectionStrokeWidth(double width);
        void setSelectionOpacity(double opacity);
        // Blend mode of the selection (live, like the other style setters; commitStyle records the step).
        void setSelectionBlendMode(BlendMode mode);
        // ---- Appearance stack of the selection (see AppearanceItem) ----
        // "Active" = the fill / stroke row selected in the Appearance panel, on every selected object. Choosing the active
        // item is view state (no undo step, does not mark the document modified).
        void setActiveItem(AppearanceKind kind, std::size_t index);
        // Live (no undo step; the panel commits with commitStyle) edits of the active item of the kind.
        void setSelectionItemOpacity(AppearanceKind kind, double opacity);
        void setSelectionItemBlendMode(AppearanceKind kind, BlendMode mode);
        void setSelectionItemVisible(AppearanceKind kind, bool visible);
        // Each of these is one undo step and returns false when nothing changed.
        // A new fill / stroke on top of the stack (from the document's default paints), which becomes the active one.
        bool addAppearanceItem(AppearanceKind kind);
        // A new effect on top of the stack (with the defaults of its kind); it becomes the active effect.
        bool addEffect(EffectKind kind);
        // Edits the active effect of every selected object (one undo step).
        bool changeActiveEffect(const std::function<void(AppearanceItem&)>& change, const std::string& description);
        // A copy of the active item right above it. The copy becomes active.
        bool duplicateActiveItem(AppearanceKind kind);
        // Removes the active item; the last fill / stroke is set to a None paint instead, so there is always one of each.
        bool deleteActiveItem(AppearanceKind kind);
        // Moves the active item one place up (+1, painted later) or down (-1) the stack.
        bool moveActiveItem(AppearanceKind kind, int delta);
        // Leaves no fill and no stroke, full opacity, normal blend.
        bool clearAppearance();

        // Cap / join / miter limit of the selection: `change` edits a copy of each object's options.
        void changeSelectionStrokeOptions(const std::function<void(StrokeOptions&)>& change);
        // Pushes one undo step taking the selection from `before` to its current style; false (and no step)
        // when nothing differs.
        bool commitStyle(const std::vector<StyleState>& before, const std::string& description);

        const SketchPaint& defaultFill() const { return defaultFill_; }
        const SketchPaint& defaultStroke() const { return defaultStroke_; }
        double defaultStrokeWidth() const { return defaultStrokeWidth_; }
        const StrokeOptions& defaultStrokeOptions() const { return defaultStrokeOptions_; }
        void setDefaultStrokeOptions(const StrokeOptions& options);
        void setDefaultFill(const SketchPaint& paint);
        void setDefaultStroke(const SketchPaint& paint);
        void setDefaultStrokeWidth(double width);

        // ---- Color mode ----
        // The document's working color model: RGB or CMYK. It decides the space new colors are authored in
        // (the default fill/stroke, the Color panel's starting mode) and what the status bar shows; a
        // single object can still carry a color in another space (an RGB swatch in a CMYK document).
        ColorSpace colorMode() const { return colorMode_; }
        // Sets the mode and converts the defaults new shapes get; existing objects keep their colors. Not
        // undoable - used when loading a file and starting a new document. Only RGB and CMYK are document modes.
        void setColorMode(ColorSpace mode);
        // Illustrator's Document Color Mode: sets the mode and converts every object's fill, stroke and
        // gradient stops (and the defaults) into it, as one undo step. False when already in that mode.
        bool convertToColorMode(ColorSpace mode);

        // Fired after any change to the document (edits, undo/redo, style) - the panels follow it.
        typedef newui::Delegate<SketchDocument> ChangedDelegate;
        ChangedDelegate onChanged;

        // Straightens the selection frame: drops the warped quad and the group angle and sets every selected
        // object's frame angle to 0, so the handles wrap the outlines in an upright box again. Undoable.
        // False when nothing changed (no selection, or the frame was already upright).
        bool resetBoundingBox();

        // Back to an empty page (size and ppi kept), not modified.
        void clearObjects();

        void paint(BLContext& ctx) const;

        bool empty() const override { return objects_.empty(); }
        std::size_t size() const override { return objects_.size(); }

    protected:
        bool readFromFile(const std::string& path) override;
        bool writeToFile(const std::string& path) override;

    private:
        void changed();
        // Puts the objects in the stacking order given by ids (all of them, back to front).
        void reorder(const std::vector<std::size_t>& ids);
        // Records a visible/locked change of some objects (already applied) as one undo step.
        struct FlagState {
            std::size_t id = 0;
            bool visible = true;
            bool locked = false;
        };
        // Group membership of some objects, for the undo steps of group/ungroup.
        struct GroupState {
            std::size_t id = 0;
            std::vector<std::size_t> groups;
        };
        void applyGroups(const std::vector<GroupState>& states);
        // `state` plus the group props of its object's outermost group.
        StyleState withGroup(StyleState state) const;
        // Paints objects_[begin, end), which all share the first `depth` groups; a group with opacity / blend goes through a layer.
        void paintRange(BLContext& ctx, std::size_t begin, std::size_t end, std::size_t depth) const;
        bool applyFlags(const std::vector<FlagState>& before, const std::vector<FlagState>& after, const std::string& description);
        // Removes and returns the object (its index is stored in *index); null if not found.
        std::unique_ptr<SketchObject> detach(std::size_t id, std::size_t* index);
        void attach(std::unique_ptr<SketchObject> object, std::size_t index);
        void pruneSelection();

        float pageWidth_ = 612.0f;
        float pageHeight_ = 792.0f;
        float ppi_ = 72.0f;
        std::optional<SketchColor> pageBackground_ = SketchColor::rgb(1.0f, 1.0f, 1.0f);
        std::vector<std::unique_ptr<SketchObject>> objects_;
        std::size_t nextId_ = 1;
        std::size_t nextGroupId_ = 1;
        std::map<std::size_t, GroupProps> groupProps_;
        std::vector<GraphicStyle> graphicStyles_ = defaultGraphicStyles();
        SketchPaint defaultFill_ = SketchPaint::solid(SketchColor::fromHex("#d9e6ff"));
        SketchPaint defaultStroke_ = SketchPaint::solid(SketchColor::fromHex("#2f5fb3"));
        double defaultStrokeWidth_ = 1.5;
        StrokeOptions defaultStrokeOptions_;
        ColorSpace colorMode_ = ColorSpace::RGB;
        std::vector<std::size_t> selection_;
        std::optional<double> groupAngle_;
        std::optional<QuadFrame> quadFrame_;
        double originU_ = 0.5;
        double originV_ = 0.5;
        newui::UndoStack undoStack_;
    };

}

#include "sketchcommands.h"

#include "canvasview.h"
#include "sketchcontroller.h"
#include "sketchdocument.h"

#include <newui/runloop.h>

namespace sketch {

    SketchCommandController::SketchCommandController(SketchDocument& document, CanvasView& canvas, SketchDocumentController& files)
        : document_(document), canvas_(canvas), files_(files) {
        addFileCommands();
        addEditCommands();
        addObjectCommands();
        addArrangeCommands();
        addViewCommands();
    }

    void SketchCommandController::add(const std::string& id, std::function<void()> run, std::function<bool()> enabled) {
        commands_[id] = Command{std::move(run), std::move(enabled)};
    }

    bool SketchCommandController::enabled(const std::string& id) const {
        auto it = commands_.find(id);
        if (it == commands_.end()) {
            return false;
        }
        return !it->second.enabled || it->second.enabled();
    }

    bool SketchCommandController::run(const std::string& id) {
        auto it = commands_.find(id);
        if (it == commands_.end() || (it->second.enabled && !it->second.enabled())) {
            return false;
        }
        it->second.run();
        return true;
    }

    std::string SketchCommandController::undoText() const {
        return document_.undoStack().canUndo() ? "Undo " + document_.undoStack().undoDescription() : "Undo";
    }

    std::string SketchCommandController::redoText() const {
        return document_.undoStack().canRedo() ? "Redo " + document_.undoStack().redoDescription() : "Redo";
    }

    // ---- File: the document controller asks the questions and shows the dialogs ----

    void SketchCommandController::addFileCommands() {
        add("file.new", [this]() { files_.newDocument(); });
        add("file.open", [this]() { files_.open(); });
        add("file.save", [this]() { files_.save(); });
        add("file.saveAs", [this]() { files_.saveAs(); });
        add("file.documentSetup", [this]() { files_.documentSetup(); });
        add("file.exit", [this]() {
            if (files_.canClose()) {
                newui::RunLoop::current().quit();
            }
        });
    }

    // ---- Edit ----

    void SketchCommandController::addEditCommands() {
        add("edit.undo", [this]() { document_.undoStack().undo(); }, [this]() { return document_.undoStack().canUndo(); });
        add("edit.redo", [this]() { document_.undoStack().redo(); }, [this]() { return document_.undoStack().canRedo(); });
        add("edit.cut", [this]() { canvas_.cutSelection(); });
        add("edit.copy", [this]() { canvas_.copySelection(); });
        add("edit.paste", [this]() { canvas_.paste(false); });
        add("edit.pasteInPlace", [this]() { canvas_.paste(true); });
        add("edit.duplicate", [this]() { document_.duplicateSelection(12.0, 12.0); });
        add("edit.delete", [this]() { document_.deleteSelection(); });
        add("edit.selectAll", [this]() { document_.selectAll(); });
        add("edit.deselect", [this]() { document_.clearSelection(); });
    }

    // ---- Object: group, lock, hide, transform. One undo step each (the document does the work). ----

    void SketchCommandController::addObjectCommands() {
        add("object.group", [this]() { document_.groupSelection(); });
        add("object.ungroup", [this]() { document_.ungroupSelection(); });
        add("object.lock", [this]() { document_.lockSelection(); });
        add("object.unlockAll", [this]() { document_.unlockAll(); });
        add("object.hide", [this]() { document_.hideSelection(); });
        add("object.showAll", [this]() { document_.showAll(); });
        add("object.rotate90cw", [this]() { document_.rotateSelection90(true); });
        add("object.rotate90ccw", [this]() { document_.rotateSelection90(false); });
        add("object.flipHorizontal", [this]() { document_.flipSelection(true); });
        add("object.flipVertical", [this]() { document_.flipSelection(false); });
        add("object.resetBoundingBox", [this]() { document_.resetBoundingBox(); });
        // Pathfinder works on two or more selected objects.
        auto twoSelected = [this]() { return document_.selection().size() >= 2; };
        add("pathfinder.unite", [this]() { document_.combineSelection(BoolOp::Union); }, twoSelected);
        add("pathfinder.minusFront", [this]() { document_.combineSelection(BoolOp::Subtract); }, twoSelected);
        add("pathfinder.intersect", [this]() { document_.combineSelection(BoolOp::Intersect); }, twoSelected);
        add("pathfinder.exclude", [this]() { document_.combineSelection(BoolOp::Exclude); }, twoSelected);
        add("color.rgb", [this]() { document_.convertToColorMode(ColorSpace::RGB); });
        add("color.cmyk", [this]() { document_.convertToColorMode(ColorSpace::CMYK); });
    }

    // ---- Arrange: stacking order and alignment ----

    void SketchCommandController::addArrangeCommands() {
        using Arrange = SketchDocument::ArrangeOp;
        using Align = SketchDocument::AlignMode;
        add("arrange.bringToFront", [this]() { document_.arrangeSelection(Arrange::BringToFront); });
        add("arrange.bringForward", [this]() { document_.arrangeSelection(Arrange::BringForward); });
        add("arrange.sendBackward", [this]() { document_.arrangeSelection(Arrange::SendBackward); });
        add("arrange.sendToBack", [this]() { document_.arrangeSelection(Arrange::SendToBack); });
        add("arrange.alignLeft", [this]() { document_.alignSelection(Align::Left); });
        add("arrange.alignCenter", [this]() { document_.alignSelection(Align::Center); });
        add("arrange.alignRight", [this]() { document_.alignSelection(Align::Right); });
        add("arrange.alignTop", [this]() { document_.alignSelection(Align::Top); });
        add("arrange.alignMiddle", [this]() { document_.alignSelection(Align::Middle); });
        add("arrange.alignBottom", [this]() { document_.alignSelection(Align::Bottom); });
    }

    // ---- View: zoom and the canvas's own switches ----

    void SketchCommandController::addViewCommands() {
        // The zoom commands keep the point at the middle of the visible canvas where it is.
        auto middle = [this]() {
            newui::Rect client = canvas_.getClientBounds();
            return newui::Point(client.width() * 0.5f, client.height() * 0.5f);
        };
        add("view.zoomIn", [this, middle]() { canvas_.zoomAt(middle(), canvas_.zoom() * 1.25f); });
        add("view.zoomOut", [this, middle]() { canvas_.zoomAt(middle(), canvas_.zoom() / 1.25f); });
        add("view.fitPage", [this]() { canvas_.zoomToFit(); });
        add("view.actualSize", [this]() { canvas_.setZoom(1.0f); });
        add("view.toggleGrid", [this]() { canvas_.setShowGrid(!canvas_.showGrid()); });
        add("view.toggleRulers", [this]() { canvas_.setShowRulers(!canvas_.showRulers()); });
        add("view.toggleSnap", [this]() { canvas_.setSnapToGrid(!canvas_.snapToGrid()); });
    }

}

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
        add("file.exportImage", [this]() { files_.exportImage(); });
        add("file.traceImage", [this]() { files_.traceImage(); });
        add("file.exportArtboards", [this]() { files_.exportArtboards(); });
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
        auto same = [this](const char* id, SketchDocument::SameKind kind) {
            add(id, [this, kind]() { document_.selectSame(kind); }, [this]() { return !document_.selection().empty(); });
        };
        same("edit.sameFill", SketchDocument::SameKind::FillColor);
        same("edit.sameStroke", SketchDocument::SameKind::StrokeColor);
        same("edit.sameWeight", SketchDocument::SameKind::StrokeWeight);
        same("edit.sameOpacity", SketchDocument::SameKind::Opacity);
        same("edit.sameBlend", SketchDocument::SameKind::BlendMode);
        same("edit.sameType", SketchDocument::SameKind::ObjectType);
    }

    // ---- Object: group, lock, hide, transform. One undo step each (the document does the work). ----

    void SketchCommandController::addObjectCommands() {
        add("object.group", [this]() { document_.groupSelection(); });
        add("object.ungroup", [this]() { document_.ungroupSelection(); });
        add("symbol.new", [this]() { document_.makeSymbol(""); });
        add("symbol.place", [this]() { document_.placeActiveSymbol(); });
        add("symbol.redefine", [this]() { document_.redefineSymbol(document_.activeSymbol()); });
        add("symbol.break", [this]() { document_.breakSymbolLink(); });
        add("pattern.make", [this]() { document_.makePattern(""); });
        add("pattern.fill", [this]() { document_.applyPattern(document_.activePattern(), false); });
        add("pattern.stroke", [this]() { document_.applyPattern(document_.activePattern(), true); });
        add("artboard.new", [this]() { document_.addArtboardBesideLast(); });
        add("artboard.delete", [this]() { document_.deleteArtboard(document_.activeArtboard()); });
        add("artboard.previous", [this]() {
            std::size_t n = document_.artboards().size();
            document_.setActiveArtboard((document_.activeArtboard() + n - 1) % n);
        });
        add("artboard.next", [this]() { document_.setActiveArtboard((document_.activeArtboard() + 1) % document_.artboards().size()); });
        add("artboard.fit", [this]() { canvas_.fitActiveArtboard(); });
        add("object.clipMake", [this]() { document_.makeClippingMask(); });
        add("object.clipRelease", [this]() { document_.releaseClippingMask(); });
        add("object.lock", [this]() { document_.lockSelection(); });
        add("object.unlockAll", [this]() { document_.unlockAll(); });
        add("object.hide", [this]() { document_.hideSelection(); });
        add("object.showAll", [this]() { document_.showAll(); });
        add("object.rotate90cw", [this]() { document_.rotateSelection90(true); });
        add("object.rotate90ccw", [this]() { document_.rotateSelection90(false); });
        add("object.flipHorizontal", [this]() { document_.flipSelection(true); });
        add("object.flipVertical", [this]() { document_.flipSelection(false); });
        add("object.transformAgain", [this]() { document_.transformAgain(); }, [this]() { return document_.canTransformAgain() && !document_.selection().empty(); });
        add("object.resetBoundingBox", [this]() { document_.resetBoundingBox(); });
        // Pathfinder works on two or more selected objects.
        auto twoSelected = [this]() { return document_.selection().size() >= 2; };
        add("pathfinder.unite", [this]() { document_.combineSelection(BoolOp::Union); }, twoSelected);
        add("pathfinder.minusFront", [this]() { document_.combineSelection(BoolOp::Subtract); }, twoSelected);
        add("pathfinder.intersect", [this]() { document_.combineSelection(BoolOp::Intersect); }, twoSelected);
        add("pathfinder.exclude", [this]() { document_.combineSelection(BoolOp::Exclude); }, twoSelected);
        add("path.join", [this]() { document_.joinSelection(); });
        add("path.reverse", [this]() { document_.reverseSelection(); });
        add("path.simplify", [this]() { document_.simplifySelection(); });
        add("path.outlineStroke", [this]() { document_.outlineStroke(); });
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
        add("view.toggleGuides", [this]() { canvas_.setShowGuides(!canvas_.showGuides()); });
        add("view.lockGuides", [this]() { canvas_.setGuidesLocked(!canvas_.guidesLocked()); });
        add("view.clearGuides", [this]() { document_.clearGuides(); }, [this]() { return !document_.guides().empty(); });
        add("view.toggleSmartGuides", [this]() { canvas_.setSmartGuides(!canvas_.smartGuides()); });
    }

}

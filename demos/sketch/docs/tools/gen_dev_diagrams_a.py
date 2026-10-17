"""Developer diagrams, part A: architecture, document, appearance, rendering, layers. Run: python gen_dev_diagrams_a.py"""
from svgkit import *


def architecture():
    s = Svg(940, 600, "Architecture of sketch (MVC)", "Views send input to controllers, controllers edit the model through undoable commands, the model notifies the views.")
    s.text(470, 28, "sketch - model / view / controller", "h", "middle")
    cols = [(25, "VIEWS", "what is drawn", ["CanvasView", "LayerTree", "StylePanel", "TransformPanel / TextPanel", "BrushPanel / LibraryPanel", "ArtboardPanel / LayersPanel", "dialogs (New Document, Trace)"]),
            (355, "CONTROLLERS", "what is done with input", ["SketchWindowController", "SketchCommandController (by id)", "SketchDocumentController (File)", "one controller per panel", "CanvasView tool code", "(transformtools, cuttools, ...)"]),
            (685, "MODEL", "what the document is", ["SketchDocument", "SketchObject (outline + appearance)", "SymbolDef / BrushDef / ImageData", "Layer, Artboard, Guide, GraphicStyle", "UndoStack (newui)", "pure geometry: pathops, shapetools,", "pathflatten, brush, textwarp"])]
    for x, title, sub, items in cols:
        s.rect(x, 50, 230, 440, "panel", 8)
        s.text(x + 115, 76, title, "h", "middle")
        s.text(x + 115, 92, sub, "s", "middle")
        for i, item in enumerate(items):
            s.rect(x + 12, 108 + i * 52, 206, 40, "box", 6)
            s.text(x + 115, 133 + i * 52, item, "t", "middle")
    s.arrow(255, 150, 355, 150)
    s.text(305, 140, "mouse, keys", "s", "middle")
    s.arrow(585, 190, 685, 190, acc=True)
    s.text(635, 180, "edit + undo step", "s", "middle")
    # notifications run underneath the columns, model -> views
    s.add('<path d="M800 490 L800 530 L140 530 L140 492" class="acc dash" marker-end="url(#arrowAcc)"/>')
    s.text(470, 524, "onChanged / onSelectionChanged / updateAllViews  ->  refresh()", "t", "middle")
    s.text(470, 568, "Controllers never draw; views never edit the document: a view reports what happened, a controller turns it into a command.", "s", "middle")
    s.text(470, 584, "Panels are controllers that also watch the document: every edit (and every selection change) calls their refresh().", "s", "middle")
    s.save("architecture")


def document_structure():
    s = Svg(1000, 700, "Structure of a SketchDocument", "The document holds artboards, a layer tree, a flat back-to-front list of objects, and libraries.")
    s.text(500, 26, "SketchDocument", "h", "middle")
    s.rect(20, 40, 960, 650, "panel", 8)
    # left column: artboards, layers, guides etc.
    s.rect(40, 60, 250, 120, "box", 6)
    s.text(165, 80, "artboards_ : vector<Artboard>", "t b", "middle")
    s.lines(54, 100, ["name, x, y, width, height", "never empty; one is active", "= \"the page\" (Align, trace, export)"], "s")
    s.rect(40, 196, 250, 168, "box", 6)
    s.text(165, 216, "layers_ : vector<Layer>", "t b", "middle")
    s.lines(54, 236, ["id, parent (0 = top level), name", "visible, locked, expanded, color", "siblings listed back to front", "tree depth by parent chain", "activeLayer_ = where new objects go"], "s")
    s.rect(40, 380, 250, 110, "box", 6)
    s.text(165, 400, "guides_, swatches_, graphicStyles_", "t b", "middle")
    s.lines(54, 420, ["Guide { vertical, position }", "swatch = named color", "GraphicStyle = appearance + opacity"], "s")
    s.rect(40, 506, 250, 164, "box", 6)
    s.text(165, 526, "groupProps_ : id -> GroupProps", "t b", "middle")
    s.lines(54, 546, ["opacity, blend mode, clip flag", "a group is only an id: members", "list it in groups_ (outermost", "first); members stay adjacent in", "the stacking order"], "s")
    # centre: objects
    s.rect(320, 60, 360, 610, "soft", 8)
    s.text(500, 82, "objects_ : vector<unique_ptr<SketchObject>>", "t b", "middle")
    s.text(500, 98, "flat, back (index 0) to front; sorted by layer after every change", "s", "middle")
    for i, name in enumerate(["Rect  (layer 1)", "Path  (layer 1, group 4)", "Text  (layer 1, group 4)", "Image (layer 2)", "Symbol (layer 2)", "Ellipse (layer 3)"]):
        y = 112 + i * 40
        s.rect(344, y, 312, 32, "box", 5)
        s.text(358, y + 20, name, "t")
        s.text(644, y + 20, "#%d" % i, "s", "end")
    s.arrow(500, 112 + 6 * 40, 500, 112 + 6 * 40 + 20)
    # object anatomy
    s.rect(340, 380, 320, 276, "box", 6)
    s.text(500, 400, "SketchObject", "t b", "middle")
    s.lines(354, 420, ["type_   Line Rect RoundRect Ellipse Polygon Polyline",
                       "        Path Text Symbol Image", "path_   BLPath in document points, y down",
                       "items_  appearance: fills, strokes, effects", "opacity_, blendMode_, groups_, layer_",
                       "visible_/locked_  (+ layer state, kept by doc)", "frameAngle_  (the box the handles use)",
                       "text: text_ font size style textMatrix_ warp",
                       "symbol_ -> SymbolDef,  image_ -> ImageData", "id (unique, stable across undo)"], "s", 15.5)
    # right column: libraries
    s.rect(710, 60, 250, 610, "box", 6)
    s.text(835, 80, "libraries (shared, immutable)", "t b", "middle")
    libs = [("symbols_", "SymbolDef: id, name, box,", "content = a small SketchDocument", "-> Symbol objects (instances)"),
            ("patterns_", "SymbolDef used as a tile", "-> SketchPaint::Pattern", "(id, scale, angle)"),
            ("brushes_", "BrushDef: kind + numbers", "(+ artwork SymbolDef)", "-> StrokeOptions::brush"),
            ("images", "ImageData: file bytes + pixels", "no library list: the Image objects", "share the pointer; saved once")]
    for i, (name, a, b, c) in enumerate(libs):
        y = 98 + i * 124
        s.rect(724, y, 222, 112, "soft", 6)
        s.text(835, y + 20, name, "t b", "middle")
        s.lines(738, y + 40, [a, b, c], "s")
    s.line(290, 120, 320, 120, "thin")
    s.line(290, 280, 320, 280, "thin")
    s.line(680, 200, 710, 200, "thin")
    s.save("document-structure")


def appearance_stack():
    s = Svg(900, 420, "The appearance stack of an object", "Items are painted bottom to top; effects act on everything below them.")
    s.text(450, 26, "items_  (vector<AppearanceItem>) - painted bottom to top", "h", "middle")
    rows = [("Fill   gradient", "paint, opacity, blend, visible", "drawItem: fill_path(shown)", SOFT),
            ("Stroke  4 pt", "paint, width, StrokeOptions (cap, join, miter, dashes, width profile, brush)", "drawItem: paintStrokeItem", SOFT),
            ("Fill   solid 30%  Multiply", "a second fill on top", "drawItem: fill_path", SOFT),
            ("Round Corners  14", "geometry effect: reshapes the outline of the items below", "applied to `shown` path", "#fff3d6"),
            ("Drop Shadow", "pixel effect: needs the items below drawn into a layer first", "paintWithEffects", "#fde3e3")]
    for i, (a, b, c, fill) in enumerate(rows):
        y = 60 + (len(rows) - 1 - i) * 56
        s.add('<rect x="40" y="%s" width="520" height="46" rx="6" fill="%s" stroke="%s"/>' % (f(y), fill, EDGE))
        s.text(56, y + 20, a, "t b")
        s.text(56, y + 36, b, "s")
        s.text(580, y + 28, c, "s")
    s.text(40, 54, "top", "s")
    s.text(40, 360, "bottom (painted first)", "s")
    s.arrow(20, 330, 20, 70)
    s.text(660, 340, "object-level: opacity_  blendMode_  (group props too)", "s")
    s.text(40, 395, "Every object always has at least one fill and one stroke (\"primary\" items; either may be paint = none); the panels edit the active ones.", "s")
    s.save("appearance-stack")


def render_pipeline():
    s = Svg(980, 640, "Rendering pipeline", "How a document is painted into a BLContext.")
    s.text(490, 26, "SketchDocument::paint(ctx)", "h", "middle")
    def node(x, y, w, h, title, sub=None, cls="box"):
        s.box_label(x, y, w, h, title, sub, cls)
    node(380, 44, 220, 44, "paintRange(begin, end, depth)", "walks objects_ in stacking order", "soft")
    node(60, 130, 250, 54, "object not in a group at this depth", "-> object.paint(ctx)")
    node(380, 130, 250, 54, "a run of members of one group", "same groups_[depth] (they are adjacent)")
    node(700, 130, 240, 54, "group has opacity / blend / clip", "-> draw members into an offscreen layer")
    s.arrow(490, 88, 185, 130)
    s.arrow(490, 88, 505, 130)
    s.arrow(630, 157, 700, 157)
    s.text(660, 150, "else recurse", "s", "middle")
    node(700, 210, 240, 54, "layer composited with opacity + mode", "clip group: layer x coverage of the mask")
    s.arrow(820, 184, 820, 210)
    s.text(490, 246, "SketchObject::paint(ctx)", "h", "middle")
    s.arrow(185, 184, 185, 270)
    node(60, 270, 250, 54, "pixel effects present?", "blur / shadow / glow / inner shadow")
    node(380, 270, 250, 54, "opacity < 1 or blend != normal?", "paintIsolated(layer)")
    node(700, 270, 240, 54, "otherwise paintItems(ctx)", "straight into the context")
    s.arrow(310, 297, 380, 297)
    s.arrow(630, 297, 700, 297)
    s.text(345, 290, "no", "s", "middle")
    s.text(665, 290, "no", "s", "middle")
    node(60, 360, 250, 54, "paintWithEffects", "items into a device-resolution layer; each pixel effect post-processes it")
    s.arrow(185, 324, 185, 360)
    s.text(205, 346, "yes", "s")
    s.text(40, 452, "for every visible item, bottom to top: drawItem(index)", "h")
    node(60, 470, 160, 70, "Fill", "setPaintStyle: solid, gradient,\npattern (BLPattern tile)")
    node(240, 470, 200, 70, "Stroke", "plain / dashed / width outline /\ngradient along path / brush")
    node(460, 470, 150, 70, "Symbol / Image", "artwork or pixels at the\nfirst fill's place")
    node(630, 470, 150, 70, "Geometry effects", "round corners, offset,\nroughen -> `shown` path")
    node(800, 470, 140, 70, "Text", "just a path: the\nglyph outlines")
    s.arrow(490, 414, 140, 470)
    s.arrow(490, 414, 340, 470)
    s.arrow(490, 414, 535, 470)
    s.arrow(490, 414, 705, 470)
    s.arrow(490, 414, 870, 470)
    s.text(490, 585, "Everything is painted from the object's BLPath in document units; the context transform (zoom, pan) is applied by CanvasView.", "s", "middle")
    s.text(490, 603, "Offscreen layers are made at the context's device resolution (makeLayer), so effects stay sharp when zoomed.", "s", "middle")
    s.save("render-pipeline")


def layer_ordering():
    s = Svg(940, 400, "Layers and stacking order", "The layer tree is flattened post-order; the object list is stable-sorted by that rank.")
    s.text(470, 26, "Layer tree  ->  rank  ->  order of objects_", "h", "middle")
    s.rect(30, 50, 250, 300, "panel", 8)
    s.text(155, 72, "layers_ (tree)", "t b", "middle")
    tree = [("Layer 2", 0, 5), ("  Sublayer 2a", 1, 1), ("  Sublayer 2b", 1, 2), ("Layer 1", 0, 4)]
    y = 100
    s.lines(46, y, ["Layer 1   (back)", "Layer 2   (front)", "   Sublayer 2a (back)", "   Sublayer 2b (front)"], "t", 28)
    s.text(46, 230, "rank = post-order: a layer's sublayers", "s")
    s.text(46, 244, "come first (behind), then the layer itself", "s")
    s.text(46, 268, "Layer 1 = 0   2a = 1   2b = 2   Layer 2 = 3", "m")
    s.arrow(285, 200, 345, 200, acc=True)
    s.text(315, 190, "syncLayers()", "s", "middle")
    s.rect(350, 50, 280, 300, "soft", 8)
    s.text(490, 72, "objects_ after every change", "t b", "middle")
    for i, (name, rank) in enumerate([("rect  (Layer 1)", 0), ("path  (Layer 1)", 0), ("star  (2a)", 1), ("text  (2b)", 2), ("circle (Layer 2)", 3), ("image (Layer 2)", 3)]):
        yy = 90 + i * 40
        s.rect(366, yy, 248, 32, "box", 5)
        s.text(380, yy + 20, name, "t")
        s.text(604, yy + 20, "rank %d" % rank, "s", "end")
    s.arrow(660, 200, 720, 200, acc=True)
    s.rect(725, 50, 190, 300, "box", 8)
    s.text(820, 72, "consequences", "t b", "middle")
    s.lines(738, 96, ["stable sort: order inside", "a layer is kept", "", "Arrange (Bring Forward ...)", "works inside the layer", "", "hide / lock a layer ->", "setLayerState on every", "object in it (and below)", "", "files store the flat order", "+ run lengths per layer"], "s", 16)
    s.text(470, 385, "The Layers panel lists the same tree the other way up (front first), with each layer's own objects above its sublayers.", "s", "middle")
    s.save("layer-ordering")


if __name__ == "__main__":
    architecture()
    document_structure()
    appearance_stack()
    render_pipeline()
    layer_ordering()

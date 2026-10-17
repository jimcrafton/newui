"""Developer diagrams, part D: files, libraries, clipping, interaction. Run: python gen_dev_diagrams_d.py"""
import math
from svgkit import *


def file_formats():
    s = Svg(980, 660, "The two file formats", "SVG with data attributes, and the .skb chunked binary format.")
    s.text(490, 26, "File formats", "h", "middle")
    # SVG
    s.rect(20, 44, 460, 600, "panel", 6)
    s.text(250, 66, "sketch SVG (.svg, .svgz = gzip)", "t b", "middle")
    code = ['<svg width viewBox data-ppi data-color-mode',
            '     data-page-background data-artboards',
            '     data-active-artboard data-guides',
            '     data-layers data-swatches>',
            '  <defs>',
            '    gradients  <linearGradient>/<radialGradient>',
            '    <svg data-sketch-symbol="1" data-box=...>',
            '       (a whole nested document)',
            '    <svg data-sketch-pattern="2" ...>',
            '    <svg data-sketch-brush="3" data-brush-kind',
            '         data-brush-params="angle,round,..."/>',
            '  </defs>',
            '  <rect id="pageBackground..."/>  (one per artboard)',
            '  <g> group (opacity, blend, data-clip) ...',
            '    <path d=... data-type="Rect|Path|Text|..."',
            '       fill stroke stroke-width ... data-name',
            '       data-layer data-locked data-frame-angle',
            '       data-text data-font-size data-text-matrix',
            '       data-text-warp data-warp-a data-warp-b',
            '       data-symbol data-brush data-fill-pattern/>',
            '    <image transform=matrix(..) href=data:.../>',
            '    <g data-sketch="appearance">  one <path> per',
            '       fill / stroke / effect item',
            '  </g>',
            '  <g data-sketch="graphic-styles"> ... </g>',
            '</svg>']
    for i, row in enumerate(code):
        s.text(34, 92 + i * 19, row, "m")
    s.text(34, 92 + len(code) * 19 + 10, "Other viewers show shapes, gradients, groups, images and text outlines;", "s")
    s.text(34, 92 + len(code) * 19 + 23, "what only sketch understands rides in data-* attributes. On open, <use>,", "s")
    s.text(34, 92 + len(code) * 19 + 36, "<symbol>, nested <svg>, clip-path, simple filters and <image> are expanded.", "s")
    # SKB
    x0 = 500
    s.rect(x0, 44, 460, 600, "panel", 6)
    s.text(x0 + 230, 66, "sketch binary (.skb), little-endian", "t b", "middle")
    s.rect(x0 + 20, 82, 420, 40, "soft", 4)
    s.text(x0 + 30, 106, '"SKB1"   version u16 (2)   flags u16 (bit 0: body gzipped)', "m")
    chunks = [("PAGE", "size, ppi, color mode, background"), ("GUID", "guides"), ("LAYR", "layers (+ colour) and per-layer runs"), ("SWCH", "swatches"),
              ("STRS", "every distinct string once"), ("STYL", "distinct appearances, shared by objects"), ("GRPS / CLIP", "group props, clipping group ids"),
              ("SYMB / PATN", "symbol and pattern libraries (nested .skb)"), ("BRSH", "brushes (+ nested artwork)"), ("IMGS", "each picture's file bytes, once"),
              ("ABRD", "artboards"), ("GSTY", "graphic styles"), ("OBJS", "objects in stacking order")]
    for i, (tag, what) in enumerate(chunks):
        y = 132 + i * 26
        s.rect(x0 + 20, y, 420, 22, "box", 3)
        s.text(x0 + 30, y + 15, tag, "m")
        s.text(x0 + 130, y + 15, what, "s")
    s.text(x0 + 20, 132 + len(chunks) * 26 + 14, "chunk = tag (4 ASCII) + length u64 + bytes; unknown tags are skipped", "s")
    y = 132 + len(chunks) * 26 + 36
    s.text(x0 + 20, y, "one object in OBJS", "t b")
    s.lines(x0 + 20, y + 18, ["type u8, flags u8 (visible, locked, angle, groups, text, style,", "symbol, warp), name (string index), look index (STYL)", "[frame angle] [groups] [text state] [style] [symbol id | picture index]", "[warp] then the outline:"], "s", 14)
    s.text(x0 + 20, y + 80, "version 2 outline = count, one command byte each, then for every", "s")
    s.text(x0 + 20, y + 94, "vertex (not Close) dx, dy as f32 from the previous DECODED vertex", "s")
    s.text(x0 + 20, y + 108, "-> half the size; no drift; exactly representable values are exact", "s")
    s.save("file-formats")


def libraries():
    s = Svg(960, 440, "Symbols, patterns and images: artwork placed by a matrix", "A quadrilateral outline carries the definition's box; patterns repeat a pixel-exact tile.")
    s.text(480, 26, "Shared artwork", "h", "middle")
    # symbol quad
    s.rect(20, 44, 450, 230, "panel", 6)
    s.text(245, 64, "Symbol / Image instance = a quad + a matrix", "t b", "middle")
    s.rect(40, 90, 110, 70, "box")
    s.text(95, 85, "definition box (0,0)-(w,h)", "s", "middle")
    s.add('<circle cx="75" cy="115" r="14" fill="%s"/><path d="M95 150 l25 -35 l22 35z" fill="%s"/>' % (AMBER, ACCENT))
    s.arrow(160, 125, 215, 125, acc=True)
    q = [(240, 100), (400, 80), (440, 170), (270, 215)]
    s.add('<path d="%s" fill="%s" stroke="%s"/>' % (poly(q, True), SOFT, ACCENT))
    for i, (x, y) in enumerate(q):
        s.dot(x, y, 3.2, RED)
        s.text(x - 12 if i in (0, 3) else x + 8, y - 6 if i < 2 else y + 14, "v%d" % i, "s")
    s.lines(40, 245, ["outline = 4 vertices; matrix from v0, v1, v3: a = (v1-v0)/w, c = (v3-v0)/h, e,f = v0", "so move / scale / rotate / SHEAR are just edits of the outline; Image draws pixels,", "Symbol paints its SketchDocument; both appear where the first fill would"], "s", 14)
    # pattern
    s.rect(490, 44, 450, 230, "panel", 6)
    s.text(715, 64, "Pattern paint = tile, scale, angle", "t b", "middle")
    for i in range(5):
        for j in range(3):
            x, y = 520 + i * 38, 90 + j * 38
            s.rect(x, y, 38, 38, "box")
            s.add('<circle cx="%s" cy="%s" r="9" fill="%s"/>' % (f(x + 19), f(y + 19), RED))
    s.text(520, 218, "SymbolDef::tile(pw, ph) renders the artwork at the DEVICE", "s")
    s.text(520, 231, "resolution (cached) -> BLPattern(REPEAT, scale/rotate matrix),", "s")
    s.text(520, 244, "anchored at the document origin; whole-pixel size = exact period", "s")
    s.rect(20, 290, 920, 130, "soft", 6)
    s.text(34, 312, "Sharing and identity", "t b")
    s.lines(34, 334, ["- Definitions are immutable: redefine / rename swap in a NEW SymbolDef and rebind the instances (undo swaps back). Objects hold shared_ptr.",
                      "- Copy / paste writes the used definitions into the clipboard SVG; pasteSvg calls importSymbol / importPattern / importBrush, which reuse an identical entry",
                      "  (compared by name, box and the SVG of the content) instead of adding a copy per paste. ImageData is shared by pointer and saved once per file.",
                      "- A definition's content is itself a SketchDocument (nested symbols and brushes inside it are carried along)."], "s", 17)
    s.save("libraries")


def clipping():
    s = Svg(900, 340, "Clipping masks", "A clipping group draws its members into a layer and multiplies it by the mask's coverage.")
    s.text(450, 26, "Clipping mask = group with GroupProps::clip", "h", "middle")
    s.add('<defs><clipPath id="m"><circle cx="460" cy="150" r="52"/></clipPath></defs>')
    s.rect(40, 60, 150, 100, "box")
    for i in range(6):
        s.add('<rect x="%s" y="60" width="12" height="100" fill="%s" opacity="0.7"/>' % (f(46 + i * 24), ACCENT))
    s.text(115, 180, "members (below the mask)", "s", "middle")
    s.arrow(200, 110, 250, 110, acc=True)
    s.rect(250, 70, 130, 80, "soft")
    s.text(315, 105, "layer (device", "s", "middle")
    s.text(315, 118, "resolution)", "s", "middle")
    s.circle(460, 150, 52, "box", 'stroke-dasharray="4 3"')
    s.text(460, 225, "mask = topmost member's outline", "s", "middle")
    s.text(460, 238, "(filled non-zero -> coverage image)", "s", "middle")
    s.arrow(390, 110, 420, 130, acc=True)
    s.add('<g clip-path="url(#m)"><rect x="400" y="98" width="120" height="104" fill="%s" opacity="0"/>' % SOFT)
    for i in range(6):
        s.add('<rect x="%s" y="98" width="12" height="104" fill="%s" opacity="0.7"/>' % (f(412 + i * 20), ACCENT))
    s.add('</g>')
    s.arrow(525, 150, 590, 150, acc=True)
    s.text(660, 128, "result = layer x coverage, then", "s", "middle")
    s.text(660, 142, "composited with the group's opacity", "s", "middle")
    s.text(660, 156, "and blend mode (paintClipped)", "s", "middle")
    s.rect(20, 270, 860, 56, "panel", 6)
    s.text(34, 292, "hitTest ignores the parts a mask cuts away; selectionBounds is the mask's bounds intersected; the mask object itself paints nothing.", "s")
    s.text(34, 308, "Saved as <g data-clip=\"true\"> and the skb CLIP chunk. SVG import of clip-path builds the same group (content + one mask object of the clip shapes).", "s")
    s.save("clipping")


def interaction():
    s = Svg(960, 480, "How a mouse gesture becomes an edit", "Tool dispatch, live preview and one undo step.")
    s.text(480, 26, "CanvasView gesture handling (canvasview.cpp and the tool files)", "h", "middle")
    stages = [("handleMouseDown", "pan? ruler guide?\nthen by tool_:\nrecord dragStart,\nset drag_ (Drag enum)"),
              ("updateDrag (moves)", "the live preview:\nrestore dragBefore_,\napply the new matrix /\nshape; snapping;\nmarkDirty()"),
              ("finishDrag (up)", "updateDrag once more,\nthen by the drag kind:\ncommitGeometry /\naddDrawn / replaceUndoable\n= ONE undo step"),
              ("paintOverlay", "frames, handles, guides,\ntool previews (never\npart of the document)")]
    for i, (a, b) in enumerate(stages):
        x = 20 + i * 235
        s.box_label(x, 50, 215, 110, a, b, "soft" if i == 1 else "box")
        if i:
            s.arrow(x - 20, 105, x, 105)
    s.text(480, 182, "Esc during a drag restores the outlines and drops the gesture (nothing is pushed).", "s", "middle")
    s.rect(20, 200, 920, 260, "panel", 6)
    s.text(34, 222, "Tools and what they push when the gesture ends", "t b")
    rows = [("Select / handles", "Move, Resize, Rotate, Shear, Warp, Origin", "commitGeometry (+ noteTransform for Transform Again)"),
            ("Rotate / Scale / Reflect", "TransformTool about the transform origin", "commitGeometry, or duplicateTransformed with Alt"),
            ("shapes, Line", "Draw (rubber band, snapPoint)", "addDrawn -> addUndoable"),
            ("Pen, Pencil, Paintbrush", "PenHandle / Pencil", "finishPen / finishPencil -> addDrawn (fitCurve)"),
            ("Direct Select", "DirectEdit (anchor or handle of SubPath)", "replaces the outline from subPathsToPath"),
            ("Scissors, Knife, Eraser, Shape Builder", "cuts via pathops", "replaceUndoable (remove + add objects)"),
            ("Gradient, Width", "GradientNew/End/Stop, Width", "commitStyle / StrokeOptions edits"),
            ("Artboard", "ArtboardNew/Move/Resize (live rect)", "setArtboard / addArtboard (release only)"),
            ("Lasso, Magic Wand, marquee", "selection only", "no undo step (selection is a view)")]
    for i, (a, b, c) in enumerate(rows):
        y = 246 + i * 23
        s.text(34, y, a, "t b")
        s.text(250, y, b, "s")
        s.text(560, y, c, "s")
    s.save("interaction")


def snapping_hit():
    s = Svg(940, 300, "Snapping, hit testing and selection", "How the canvas decides what a point means.")
    s.text(470, 26, "Snapping and hit testing", "h", "middle")
    s.rect(20, 44, 440, 230, "panel", 6)
    s.text(240, 66, "snapAxis / snapPoint", "t b", "middle")
    s.lines(34, 90, ["candidates (view pixels, within 6 px of the pointer):", "  grid lines (when Snap to Grid is on)", "  ruler guides", "  Smart Guides: left / center / right and top / middle /", "  bottom of every other object (up to 3000 objects)", "", "a moving selection tries its 3 vertical and 3 horizontal", "edges; the nearest catch wins, a magenta line shows it;", "while moving, the selection itself is left out of the list"], "s", 15)
    s.rect(480, 44, 440, 230, "panel", 6)
    s.text(700, 66, "hitTest(point, tolerance)", "t b", "middle")
    s.lines(494, 90, ["SketchDocument: front to back, visible and unlocked objects;", "an object in a clipping group counts only inside its mask.", "", "SketchObject: bounds first (cheap reject), then", "  Symbol / Image: inside the quad", "  filled closed shape: path.hit_test (non-zero)", "  stroke: stroke the outline at width + 2 * tolerance (the", "  stroker the painter uses) and test inside; brushes use reach", "tolerance = 4 screen pixels / zoom"], "s", 15)
    s.save("snapping-hit")


if __name__ == "__main__":
    file_formats()
    libraries()
    clipping()
    interaction()
    snapping_hit()

"""Developer diagrams, part C: strokes, brushes, text, frames, undo, files. Run: python gen_dev_diagrams_c.py"""
import math
from svgkit import *


def s_curve(x0, y0, w, h, n=60):
    pts = []
    for i in range(n + 1):
        t = i / n
        pts.append((x0 + t * w, y0 + math.sin(t * math.pi * 2) * h))
    return pts


def normals(pts):
    out = []
    for i, p in enumerate(pts):
        a = pts[max(i - 1, 0)]
        b = pts[min(i + 1, len(pts) - 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dx, dy) or 1
        out.append((dy / l, -dx / l))   # left of the direction of travel (up for a path running right)
    return out


def stroke_variants():
    s = Svg(960, 470, "The ways a stroke is drawn", "Plain, dashed, width profile, gradient along the path, brush.")
    s.text(480, 26, "Stroke rendering (paintStrokeItem)", "h", "middle")
    titles = [("plain", "Blend2D stroker:", "cap, join, miter limit"), ("dashes", "dashedPath(): flatten, cut by", "the pattern; caps on every dash"), ("width profile", "widthOutline(): offset by averaged", "vertex normals, filled outline"),
              ("gradient along", "strokeAlongPath(): ~400 short", "butt pieces, color by length"), ("brush", "StrokeOptions::brush: nib hull,", "bent artwork or scatter")]
    for k, (name, a, b) in enumerate(titles):
        ox = 20 + k * 185
        s.rect(ox, 44, 175, 250, "panel", 6)
        s.text(ox + 87, 64, name, "t b", "middle")
        pts = s_curve(ox + 14, 150, 147, 38, 50)
        if k == 0:
            s.add('<path d="%s" fill="none" stroke="%s" stroke-width="9" stroke-linecap="round" stroke-linejoin="round"/>' % (poly(pts), ACCENT))
        elif k == 1:
            s.add('<path d="%s" fill="none" stroke="%s" stroke-width="7" stroke-dasharray="14 9"/>' % (poly(pts), ACCENT))
        elif k == 2:
            nr = normals(pts)
            left, right = [], []
            for i, (p, n) in enumerate(zip(pts, nr)):
                t = i / (len(pts) - 1)
                wl = 3 + 14 * math.sin(math.pi * t)
                wr = 3 + 5 * t
                left.append((p[0] + n[0] * wl, p[1] + n[1] * wl))
                right.append((p[0] - n[0] * wr, p[1] - n[1] * wr))
            s.add('<path d="%s" fill="%s" opacity="0.85"/>' % (poly(left + right[::-1], True), ACCENT))
            s.path(poly(pts), "thin", 'stroke-dasharray="3 3"')
        elif k == 3:
            for i in range(len(pts) - 1):
                t = i / (len(pts) - 1)
                col = "#%02x%02x%02x" % (int(30 + 200 * t), int(111 - 40 * t), int(224 - 180 * t))
                s.add('<line x1="%s" y1="%s" x2="%s" y2="%s" stroke="%s" stroke-width="9" stroke-linecap="butt"/>' % (f(pts[i][0]), f(pts[i][1]), f(pts[i + 1][0]), f(pts[i + 1][1]), col))
        else:
            nr = normals(pts)
            for i in range(0, len(pts), 2):
                s.add('<ellipse cx="%s" cy="%s" rx="7" ry="2.6" transform="rotate(-40 %s %s)" fill="%s" opacity="0.35"/>' % (f(pts[i][0]), f(pts[i][1]), f(pts[i][0]), f(pts[i][1]), ACCENT))
        s.text(ox + 10, 232, a, "s")
        s.text(ox + 10, 245, b, "s")
    s.rect(20, 310, 920, 140, "soft", 6)
    s.text(34, 332, "Which branch runs (paintStrokeItem, sketchobject.cpp):", "t b")
    s.lines(34, 352, ["1. StrokeOptions::brush set      -> paintBrushStroke  (dashes, caps, widths and \"along path\" are ignored)",
                      "2. width profile (WidthPoint t, left, right) -> filled outline from widthOutline, caps by StrokeCap",
                      "3. gradient paint with alongStroke -> strokeAlongPath; opaque strokes get a disc at each vertex so bends stay filled",
                      "4. otherwise Blend2D strokes the (possibly dashed) path; hit testing uses the same stroker at width + tolerance"], "s", 17)
    s.save("stroke-variants")


def brushes():
    s = Svg(960, 400, "How brushes work", "Calligraphic nib sweep, art brush bending, scatter placement.")
    s.text(480, 26, "Brushes (brush.cpp)", "h", "middle")
    # calligraphic
    ox = 20
    s.rect(ox, 44, 300, 260, "panel", 6)
    s.text(ox + 150, 64, "Calligraphic", "t b", "middle")
    pts = s_curve(ox + 20, 150, 260, 36, 22)
    for i in range(len(pts) - 1):
        for t in (0, 1):
            p = pts[i + t]
            s.add('<ellipse cx="%s" cy="%s" rx="15" ry="4" transform="rotate(-45 %s %s)" fill="%s" opacity="0.12"/>' % (f(p[0]), f(p[1]), f(p[0]), f(p[1]), ACCENT))
    s.path(poly(pts), "thin", 'stroke-dasharray="3 3"')
    s.add('<ellipse cx="%s" cy="%s" rx="15" ry="4" transform="rotate(-45 %s %s)" fill="none" stroke="%s" stroke-width="1.5"/>' % (f(pts[11][0]), f(pts[11][1]), f(pts[11][0]), f(pts[11][1]), RED))
    s.lines(ox + 10, 232, ["nib = 16-gon ellipse: major = stroke weight / 2,", "minor = major * roundness, turned by -angle", "each path segment: convex hull of the nib at", "both ends (monotone chain, one winding);", "all hulls filled non-zero = their union"], "s", 13)
    # art
    ox = 330
    s.rect(ox, 44, 300, 260, "panel", 6)
    s.text(ox + 150, 64, "Art (bend along the path)", "t b", "middle")
    s.add('<rect x="%s" y="86" width="120" height="26" fill="%s" opacity="0.5" stroke="%s"/>' % (f(ox + 20), AMBER, EDGE))
    s.text(ox + 80, 103, "artwork box", "s", "middle")
    s.arrow(ox + 150, 99, ox + 190, 99, acc=True)
    pts = s_curve(ox + 20, 170, 260, 32, 40)
    nr = normals(pts)
    top = [(p[0] + n[0] * 12, p[1] + n[1] * 12) for p, n in zip(pts, nr)]
    bottom = [(p[0] - n[0] * 12, p[1] - n[1] * 12) for p, n in zip(pts, nr)]
    s.add('<path d="%s" fill="%s" opacity="0.5" stroke="%s"/>' % (poly(top + bottom[::-1], True), AMBER, EDGE))
    s.path(poly(pts), "thin", 'stroke-dasharray="3 3"')
    s.lines(ox + 10, 232, ["u = (x - box.x0) / width  ->  arc length s = u * L", "point = P(s) + N(s) * (centre y - y) * weight", "outlines are flattened and long edges cut so", "they bend; weight 1 = the artwork's own size", "(bendAlong, per subpath)"], "s", 13)
    # scatter
    ox = 640
    s.rect(ox, 44, 300, 260, "panel", 6)
    s.text(ox + 150, 64, "Scatter", "t b", "middle")
    pts = s_curve(ox + 20, 150, 260, 36, 60)
    s.path(poly(pts), "thin", 'stroke-dasharray="3 3"')
    nr = normals(pts)
    for i in range(0, 61, 6):
        h = ((i * 7919) % 13 - 6) * 2.2
        sz = 5 + (i * 31 % 5)
        s.add('<circle cx="%s" cy="%s" r="%s" fill="%s"/>' % (f(pts[i][0] + nr[i][0] * h), f(pts[i][1] + nr[i][1] * h), f(sz), ACCENT))
    s.lines(ox + 10, 232, ["copies every  width * weight * spacing%  of arc", "length; each: translate to the artwork centre,", "scale (size variation), turn (follow the path +", "random turn), move across the path (stray)", "random values: a hash of the copy index"], "s", 13)
    s.rect(20, 320, 920, 60, "soft", 6)
    s.text(34, 343, "A BrushDef is immutable and shared (shared_ptr in StrokeOptions); editing a brush swaps in a new one and rebinds every stroke and graphic style.", "s")
    s.text(34, 360, "bounds() and hitTest() use BrushDef::reach(weight); the artwork parts are built once from the definition's SketchDocument (parts()).", "s")
    s.save("brushes")


def text_warp():
    s = Svg(960, 470, "Text on a path and text between two paths", "Mappings from the flat text space onto the paths.")
    s.text(480, 26, "TextWarper: from the flat text onto paths", "h", "middle")
    # flat text
    s.rect(20, 44, 260, 190, "panel", 6)
    s.text(150, 64, "flat text space", "t b", "middle")
    s.line(50, 150, 250, 150, "thin")
    s.add('<text x="50" y="150" style="font-size:44px;font-weight:700;fill:%s" opacity="0.5">Text</text>' % ACCENT)
    s.line(50, 150, 50, 108, "thin")
    s.text(54, 164, "x runs along the first baseline", "s")
    s.text(54, 178, "y is down; capitals reach to about -0.78 * size", "s")
    s.text(54, 192, "frame: x0..x1 (ink), yTop = -0.78 size,", "s")
    s.text(54, 206, "yBottom = last baseline + 0.22 size", "s")
    # on path
    s.rect(300, 44, 320, 190, "panel", 6)
    s.text(460, 64, "OnPath", "t b", "middle")
    path = [(320 + t * 280, 190 - 80 * math.sin(t * math.pi * 0.9)) for t in [i / 40 for i in range(41)]]
    s.path(poly(path), "line")
    nr = normals(path)
    for i in (3, 10, 17, 24, 31):
        p, n = path[i], nr[i]
        s.line(p[0], p[1], p[0] + n[0] * 26, p[1] + n[1] * 26, "acc")
        s.dot(p[0], p[1], 2.4)
    s.text(314, 222, "p = P(start + x) + N * (-y);  N = (dy, -dx)", "s")
    s.text(314, 84, "start = start% * length;  flip reverses the path", "s")
    # between
    s.rect(640, 44, 300, 190, "panel", 6)
    s.text(790, 64, "Between", "t b", "middle")
    top = [(655 + t * 270, 100 - 26 * math.sin(t * 3)) for t in [i / 30 for i in range(31)]]
    bot = [(655 + t * 270, 200 + 22 * math.sin(t * 3 + 1)) for t in [i / 30 for i in range(31)]]
    s.path(poly(top), "line")
    s.path(poly(bot), "line")
    for k in range(0, 31, 5):
        s.line(top[k][0], top[k][1], bot[k][0], bot[k][1], "thin")
    for v in (0.25, 0.5, 0.75):
        s.path(poly([(top[k][0] + (bot[k][0] - top[k][0]) * v, top[k][1] + (bot[k][1] - top[k][1]) * v) for k in range(31)]), "thin", 'stroke-dasharray="3 3"')
    s.text(652, 215, "p = lerp(T(u), B(u), v)", "s")
    s.text(652, 228, "u = (x-x0)/(x1-x0)  v = (y-yTop)/(yBottom-yTop)", "s")
    # pipeline
    s.rect(20, 256, 920, 190, "soft", 6)
    s.text(34, 278, "SketchObject::rebuildText()", "t b")
    steps = ["textOutline()", "flatten + cut edges", "TextWarper.map(x, y)", "x textMatrix_", "path_"]
    for i, name in enumerate(steps):
        x = 34 + i * 180
        s.box_label(x, 292, 160, 40, name, None, "box")
        if i:
            s.arrow(x - 20, 312, x, 312)
    s.lines(34, 356, ["- the warp paths are COPIES in the text's own flat space (the space textMatrix_ places on the page): moving, scaling or turning the text moves them",
                      "  with it, and undo of a move (path + textMatrix snapshot) needs nothing more. Making a warp sets textMatrix_ to identity (paths are in page coordinates).",
                      "- Between: both paths are measured by arc length; the higher one is the top; paths are turned to run left to right and the same way.",
                      "- mapTextPoint(x, y) = textMatrix_ * warp(x, y): where the editing caret is drawn. Clicking in warped text puts the caret at the end.",
                      "- Saved as data-text-warp + data-warp-a / data-warp-b (SVG) and object flag 128 (skb)."], "s", 16)
    s.save("text-warp")


def selection_frame():
    s = Svg(940, 460, "The selection frame and how a resize is applied", "Oriented box, handles, rotate knob and transform origin.")
    s.text(470, 26, "Selection frame (SelectionFrame, CanvasView handles)", "h", "middle")
    ang = -0.35
    cx, cy, w, h = 220, 220, 220, 130

    def rot(x, y):
        dx, dy = x - cx, y - cy
        return (cx + dx * math.cos(ang) - dy * math.sin(ang), cy + dx * math.sin(ang) + dy * math.cos(ang))
    corners = [rot(cx - w / 2, cy - h / 2), rot(cx + w / 2, cy - h / 2), rot(cx + w / 2, cy + h / 2), rot(cx - w / 2, cy + h / 2)]
    s.add('<path d="%s" fill="%s" stroke="%s" stroke-width="1.5"/>' % (poly(corners, True), SOFT, ACCENT))
    handles = [rot(cx - w / 2, cy - h / 2), rot(cx, cy - h / 2), rot(cx + w / 2, cy - h / 2), rot(cx + w / 2, cy), rot(cx + w / 2, cy + h / 2), rot(cx, cy + h / 2), rot(cx - w / 2, cy + h / 2), rot(cx - w / 2, cy)]
    for i, (x, y) in enumerate(handles):
        s.rect(x - 4, y - 4, 8, 8, "box")
        s.text(x + (8 if i in (2, 3, 4) else -8), y + (-8 if i in (0, 1, 2) else 16), str(i), "s", "end" if i in (0, 6, 7) else None)
    top = handles[1]
    knob = (top[0] + 22 * math.sin(ang), top[1] - 22 * math.cos(ang))
    s.line(top[0], top[1], knob[0], knob[1], "acc")
    s.circle(knob[0], knob[1], 5, "box")
    s.text(knob[0] + 10, knob[1] - 4, "8  rotate knob", "s")
    s.circle(cx, cy, 5, "box")
    s.line(cx - 8, cy, cx + 8, cy, "thin")
    s.line(cx, cy - 8, cx, cy + 8, "thin")
    s.text(cx + 10, cy + 4, "9", "s")
    s.text(70, 362, "9 = transform origin: the pivot of rotate and shear and of the", "s")
    s.text(70, 375, "Rotate / Scale / Reflect tools (a click with those tools moves it)", "s")
    s.text(70, 400, "frame.angle = the shared frameAngle of the selection (one object: its own;", "s")
    s.text(70, 413, "several: their common angle, else the group angle set while rotating)", "s")
    s.rect(470, 50, 450, 380, "panel", 6)
    s.text(485, 74, "Applying a drag (updateDrag -> applyToSelection)", "t b")
    s.lines(485, 98, ["1. press: dragBefore_ = snapshotSelection() (outlines, text matrices, angles)", "   and dragFrame_ = selectionFrame()",
                      "2. every move: put the outlines back to dragBefore_, transform by M, setFrameAngle", "   (no accumulation of error; the preview is cheap and exact)",
                      "3. release: commitGeometry(before, name, frameBefore) = ONE undo step"], "s", 15)
    s.text(485, 192, "Resize in the frame's own axes:", "t b")
    s.text(485, 212, "M = R(a) * Scale(sx, sy) about the anchor * R(-a)", "m")
    s.lines(485, 236, ["anchor = the opposite handle (Alt: the centre); sx, sy from the", "mouse in frame-local coordinates; Shift keeps proportions;", "an upright frame also snaps its moving edges (grid, guides,", "other objects)."], "s", 15)
    s.text(485, 312, "Other drags use the same loop:", "t b")
    s.lines(485, 332, ["Rotate (knob): angle from the origin, Shift = 15 deg steps", "Shear (Alt+Shift on a middle handle), Warp (Alt+Shift corner):", "projective map from the old quad to the new quad (QuadFrame)", "Rotate / Scale / Reflect tools: matrix about the origin,", "Alt at release -> duplicateTransformed instead"], "s", 15)
    s.save("selection-frame")


def undo_model():
    s = Svg(960, 470, "Undo and redo", "Every edit is an UndoableAction with do and undo closures; patterns used to make them.")
    s.text(480, 26, "Undo (newui::UndoStack of {description, doIt, undoIt})", "h", "middle")
    s.rect(20, 44, 270, 210, "panel", 6)
    s.text(155, 64, "UndoStack", "t b", "middle")
    for i, name in enumerate(["Move", "Draw Rectangle", "Fill Color", "Make Symbol"]):
        y = 80 + i * 40
        s.rect(40, y, 230, 32, "box", 5)
        s.text(54, y + 21, name, "t")
    s.text(155, 245, "push(action): runs doIt() at once, clears redo", "s", "middle")
    s.arrow(300, 150, 350, 150, acc=True)
    s.rect(350, 44, 590, 210, "soft", 6)
    s.text(364, 66, "Patterns used for the closures", "t b")
    pats = [("snapshot before / after", "geometry drags: commitGeometry(before, ...) takes the outlines as they are now as `after`;", "undo puts `before` back (GeometryState + FrameState). Style edits: StyleState (appearance items)."),
            ("apply(state) lambda", "most edits build apply(states) and push {do: apply(after), undo: apply(before)}", "(text changes, flags, layer snapshots, brush swaps, artboards)."),
            ("slots for objects", "add / delete / replace share objects through std::shared_ptr<Slot>: do attaches,", "undo detaches (detach() records the stacking index so redo puts it back where it was)."),
            ("library changes", "symbols_ / patterns_ / brushes_ are replaced wholesale by before / after vectors.", "(Known gap: undoing Make Symbol removes the instance, not the definition.)")]
    for i, (a, b, c) in enumerate(pats):
        y = 90 + i * 40
        s.text(364, y, a, "t b")
        s.text(364, y + 13, b, "s")
        s.text(364, y + 25, c, "s")
    s.rect(20, 276, 920, 170, "panel", 6)
    s.text(34, 298, "Rules that keep it sound", "t b")
    s.lines(34, 320, ["- Object ids are stable for the life of the document (nextId_), so closures refer to ids, never pointers; findById() on every apply.",
                      "- Selection and the active layer/artboard are views of the document: not undo steps. Edits that remove objects fix the selection (dropUnselectable).",
                      "- Live editing (drags, scrubbing, previews) changes objects WITHOUT pushing; the one step is pushed when the gesture ends (or nothing, if cancelled with Esc).",
                      "- A load or New clears the stack. batch edits inside a Batch announce changes once (documentFromSvg / documentFromBinary).",
                      "- Each edit ends in changed(): syncLayers(), markModified(), updateAllViews(), onChanged -> panels refresh."], "s", 17)
    s.save("undo")


if __name__ == "__main__":
    stroke_variants()
    brushes()
    text_warp()
    selection_frame()
    undo_model()

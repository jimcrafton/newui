"""Developer diagrams, part B: geometry. Run: python gen_dev_diagrams_b.py"""
import math
from svgkit import *

KAPPA = 0.5522847498


def anchors(s, pts, r=3.2):
    for x, y in pts:
        s.add('<rect x="%s" y="%s" width="%s" height="%s" fill="#fff" stroke="%s" stroke-width="1.4"/>' % (f(x - r), f(y - r), f(2 * r), f(2 * r), ACCENT))


def handle(s, a, h):
    s.line(a[0], a[1], h[0], h[1], "thin")
    s.dot(h[0], h[1], 2.2, ACCENT)


def shape_geometry():
    s = Svg(960, 560, "How the shapes are made", "Each tool produces a BLPath; the shapes are plain Bezier or line paths.")
    s.text(480, 26, "Geometry of the drawing tools", "h", "middle")
    cell = lambda i: (30 + (i % 3) * 310, 50 + (i // 3) * 260)
    # rectangle
    x, y = cell(0)
    s.text(x, y, "Rectangle - makeRect", "t b")
    s.path("M%s %s h160 v100 h-160 z" % (x + 20, y + 30), "acc")
    anchors(s, [(x + 20, y + 30), (x + 180, y + 30), (x + 180, y + 130), (x + 20, y + 130)])
    s.lines(x, y + 160, ["add_rect: move, 3 lines, close", "(4 anchors; a closing line that ends", "on the first point is folded in)"], "s")
    # round rect
    x, y = cell(1)
    s.text(x, y, "Rounded rectangle - add_round_rect", "t b")
    r = 28
    k = r * KAPPA
    x0, y0, w, h = x + 20, y + 30, 160, 100
    d = ("M%s %s h%s c%s 0 %s %s %s %s v%s c0 %s %s %s %s %s h-%s c-%s 0 -%s -%s -%s -%s v-%s c0 -%s %s -%s %s -%s z" %
         (f(x0 + r), f(y0), f(w - 2 * r), f(k), f(r), f(r - k), f(r), f(r), f(h - 2 * r), f(k), f(-(r - k)), f(r), f(-r), f(r), f(w - 2 * r), f(k), f(r), f(r - k), f(r), f(r), f(h - 2 * r), f(k), f(r - k), f(r), f(r), f(r)))
    s.path(d, "acc")
    s.text(x + 190, y + 45, "radius r", "s")
    s.text(x + 190, y + 60, "corner = cubic with", "s")
    s.text(x + 190, y + 74, "handles r * 0.5523", "s")
    s.lines(x, y + 160, ["live handle (kLiveRadius) recognises the", "shape again from its outline alone:", "analyzeRoundRect(path, frameAngle)"], "s")
    # ellipse
    x, y = cell(2)
    s.text(x, y, "Ellipse / circle - add_ellipse", "t b")
    cx, cy, rx, ry = x + 100, y + 80, 80, 52
    s.add('<ellipse cx="%s" cy="%s" rx="%s" ry="%s" class="acc"/>' % (f(cx), f(cy), f(rx), f(ry)))
    pts = [(cx + rx, cy), (cx, cy + ry), (cx - rx, cy), (cx, cy - ry)]
    anchors(s, pts)
    for (px, py), (hx, hy) in [((cx + rx, cy), (cx + rx, cy - ry * KAPPA)), ((cx, cy - ry), (cx + rx * KAPPA, cy - ry)), ((cx - rx, cy), (cx - rx, cy + ry * KAPPA)), ((cx, cy + ry), (cx - rx * KAPPA, cy + ry))]:
        handle(s, (px, py), (hx, hy))
    s.lines(x, y + 160, ["four cubics, handle length = radius * 0.5523", "(the circle tool drags out from the centre)"], "s")
    # polygon
    x, y = cell(3)
    s.text(x, y, "Polygon - regularPolygonPoints", "t b")
    cx, cy, rad, n = x + 100, y + 80, 62, 6
    pts = [(cx + rad * math.cos(-math.pi / 2 + 2 * math.pi * i / n), cy + rad * math.sin(-math.pi / 2 + 2 * math.pi * i / n)) for i in range(n)]
    s.path(poly(pts, True), "acc")
    anchors(s, pts)
    s.circle(cx, cy, 2.5, "box")
    s.lines(x + 190, y + 40, ["p(i) = center +", "radius * (cos a(i), sin a(i))", "a(i) = first + 2 pi i / n", "", "Up / Down while dragging", "changes n (polygonSides_)"], "s")
    s.text(x, y + 190, "Shift = first corner points straight up (-pi/2)", "s")
    # star
    x, y = cell(4)
    s.text(x, y, "Star - starPoints", "t b")
    cx, cy, outer, inner, n = x + 100, y + 80, 66, 28, 5
    pts = []
    for i in range(2 * n):
        rr = outer if i % 2 == 0 else inner
        a = -math.pi / 2 + math.pi * i / n
        pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
    s.path(poly(pts, True), "acc")
    anchors(s, pts)
    s.lines(x + 190, y + 40, ["2n vertices, alternating", "outer radius R and", "inner radius R * ratio", "", "analyzeRegularShape() finds", "n, R, ratio, first angle from", "the outline (live handles)"], "s")
    # arc
    x, y = cell(5)
    s.text(x, y, "Arc - arcPath", "t b")
    cx, cy, rad = x + 90, y + 96, 70
    start, sweep = -math.pi * 0.85, math.pi * 1.1
    segs = int(math.ceil(abs(sweep) / (math.pi / 2)))
    step = sweep / segs
    d = ""
    for i in range(segs):
        a0, a1 = start + i * step, start + (i + 1) * step
        kk = 4 / 3 * math.tan((a1 - a0) / 4) * rad
        p0 = (cx + rad * math.cos(a0), cy + rad * math.sin(a0))
        p3 = (cx + rad * math.cos(a1), cy + rad * math.sin(a1))
        p1 = (p0[0] - kk * math.sin(a0), p0[1] + kk * math.cos(a0))
        p2 = (p3[0] + kk * math.sin(a1), p3[1] - kk * math.cos(a1))
        if i == 0:
            d += "M%s %s " % (f(p0[0]), f(p0[1]))
        d += "C%s %s %s %s %s %s " % (f(p1[0]), f(p1[1]), f(p2[0]), f(p2[1]), f(p3[0]), f(p3[1]))
        anchors(s, [p0, p3])
        handle(s, p0, p1)
        handle(s, p3, p2)
    s.path(d, "acc")
    s.lines(x + 170, y + 30, ["cubics of at most a", "quarter turn each:", "k = 4/3 tan(da / 4) * r", "", "start + sweep, clockwise", "on screen (y grows down)"], "s")
    s.save("shape-geometry")


def curve_pipeline():
    s = Svg(960, 400, "From freehand strokes to Bezier curves", "Pencil and Paintbrush: sampling, light smoothing, Schneider curve fitting.")
    s.text(480, 26, "Pencil / Paintbrush: samples -> smoothed -> fitted cubics", "h", "middle")
    import random
    random.seed(7)
    raw = []
    for i in range(70):
        t = i / 69
        raw.append((20 + t * 270, 110 - 60 * math.sin(t * 6.3) * (1 - 0.3 * t) + random.uniform(-6, 6)))
    sm = list(raw)
    for _ in range(2):
        sm = [sm[0]] + [((sm[i - 1][0] + 2 * sm[i][0] + sm[i + 1][0]) / 4, (sm[i - 1][1] + 2 * sm[i][1] + sm[i + 1][1]) / 4) for i in range(1, len(sm) - 1)] + [sm[-1]]
    titles = ["1. samples (pencilMove)", "2. smoothPolyline x 2", "3. fitCurve (Schneider)"]
    for k in range(3):
        ox = 10 + k * 315
        s.rect(ox, 50, 300, 190, "panel", 6)
        s.text(ox + 10, 70, titles[k], "t b")
    for x, y in raw:
        s.dot(x + 10, y + 45, 1.8, DIM)
    s.path(poly([(x + 325, y + 45) for x, y in sm]), "line")
    for x, y in sm:
        s.dot(x + 325, y + 45, 1.6, DIM)
    # fitted: pick anchors, tangents from the smoothed curve
    idx = [0, 18, 38, 56, 69]
    pts = [sm[i] for i in idx]
    d = ""
    ox = 640
    for j in range(len(idx) - 1):
        a, b = idx[j], idx[j + 1]
        p0, p3 = sm[a], sm[b]
        t0 = (sm[min(a + 3, 69)][0] - sm[max(a - 3, 0)][0], sm[min(a + 3, 69)][1] - sm[max(a - 3, 0)][1])
        t3 = (sm[min(b + 3, 69)][0] - sm[max(b - 3, 0)][0], sm[min(b + 3, 69)][1] - sm[max(b - 3, 0)][1])
        n0 = math.hypot(*t0) or 1
        n3 = math.hypot(*t3) or 1
        L = math.hypot(p3[0] - p0[0], p3[1] - p0[1]) / 3
        p1 = (p0[0] + t0[0] / n0 * L, p0[1] + t0[1] / n0 * L)
        p2 = (p3[0] - t3[0] / n3 * L, p3[1] - t3[1] / n3 * L)
        if j == 0:
            d += "M%s %s " % (f(p0[0] + ox), f(p0[1] + 45))
        d += "C%s %s %s %s %s %s " % (f(p1[0] + ox), f(p1[1] + 45), f(p2[0] + ox), f(p2[1] + 45), f(p3[0] + ox), f(p3[1] + 45))
        handle(s, (p0[0] + ox, p0[1] + 45), (p1[0] + ox, p1[1] + 45))
        handle(s, (p3[0] + ox, p3[1] + 45), (p2[0] + ox, p2[1] + 45))
    s.path(d, "acc")
    anchors(s, [(p[0] + ox, p[1] + 45) for p in pts])
    notes = [["gap = 1.5 / zoom: a sample is kept only when", "the pointer moved that far (pencilPoints_)", "a drag shorter than 4 screen px is a click"],
             ["each pass: p'(i) = (p(i-1) + 2 p(i) + p(i+1)) / 4", "the two ends stay put; takes the tremor out", "so the fit follows the stroke, not the hand"],
             ["fewest cubics within tolerance = 2.5 / zoom px", "least-squares handles, Newton re-parametrisation,", "split at the worst point; turns > ~60 deg stay corners"]]
    for k in range(3):
        s.lines(10 + k * 315, 262, notes[k], "s")
    s.rect(10, 330, 940, 50, "soft", 6)
    s.text(480, 351, "penPath(anchors, closed)   ->   BLPath of move_to + cubic_to(in/out handles)   ->   addDrawn(...)  (one undo step: \"Draw with Pencil\")", "t", "middle")
    s.text(480, 369, "Paintbrush runs the same code, then stores the active BrushDef in the stroke options.", "s", "middle")
    s.save("curve-pipeline")


def flattening():
    s = Svg(960, 400, "Flattening curves into line segments", "Curves are sampled into polylines for measuring, dashing, brushes, text warps and boolean operations.")
    s.text(480, 26, "Flattening", "h", "middle")
    p0, p1, p2, p3 = (30, 190), (110, 40), (250, 40), (330, 190)
    s.path("M%s %s C%s %s %s %s %s %s" % (30, 190, 110, 40, 250, 40, 330, 190), "line")
    for p in (p0, p1, p2, p3):
        s.dot(p[0], p[1], 2.5, ACCENT)
    s.line(p0[0], p0[1], p1[0], p1[1], "thin")
    s.line(p2[0], p2[1], p3[0], p3[1], "thin")
    s.line(p1[0], p1[1], p2[0], p2[1], "thin")
    pts = bezier_points(p0, p1, p2, p3, 12)
    s.path(poly([(x + 0, y + 0) for x, y in pts]), "acc", 'transform="translate(0 0)"')
    for x, y in pts:
        s.dot(x, y, 2.6, RED)
    s.text(30, 232, "control polygon length L = |p0p1| + |p1p2| + |p2p3|", "s")
    s.text(30, 247, "steps = clamp(L / 3, 8, 96)   (flattenPath, pathflatten.cpp)", "s")
    s.text(30, 262, "t = k / steps; B(t) by the Bernstein form", "s")
    s.rect(380, 50, 560, 150, "panel", 6)
    s.text(394, 72, "Who flattens, and how fine", "t b")
    rows = [("flattenPath / measurePath", "3 units per piece (8..96 pieces per curve)", "dashes, brush outlines, text warp, path length"),
            ("flattenToLoops (pathops)", "tolerance 0.05 pt from the curve", "Pathfinder, knife, eraser, Shape Builder"),
            ("TextWarper::apply", "also cuts long straight edges into", "pieces of about line-height / 18"),
            ("BrushDef::bendAlong", "edges cut to artwork width / 150", "art brush: artwork follows the stroke")]
    for i, (a, b, c) in enumerate(rows):
        y = 94 + i * 25
        s.text(394, y, a, "t b")
        s.text(580, y, b, "s")
        s.text(580, y + 11, c, "s")
    # measured path with arc length
    s.text(30, 305, "Measured path (MeasuredPath: points + running length at each point)", "t b")
    base = 345
    pts2 = [(30 + i * 56, base - 18 * math.sin(i * 0.9)) for i in range(11)]
    s.path(poly(pts2), "line")
    for i, (x, y) in enumerate(pts2):
        s.dot(x, y, 2.4, DIM)
    s.arrow(30, 372, 590, 372, acc=True)
    s.text(300, 388, "s = running length   ->   sampleMeasured(s) = point + unit tangent of the segment (upper_bound on `at`)", "s", "middle")
    s.text(640, 318, "beyond the ends the position is clamped,", "s")
    s.text(640, 332, "or (text on a path) carried straight on", "s")
    s.text(640, 346, "along the first / last segment", "s")
    s.save("flattening")


def pathfinder():
    s = Svg(960, 420, "Pathfinder, knife and Shape Builder: shapes as polygons", "Shapes are flattened to polygon loops, combined with the non-zero rule, and turned back into paths.")
    s.text(480, 26, "Boolean operations work on flattened polygons (pathops.cpp)", "h", "middle")
    boxes = [("BLPath", "curves"), ("flattenToLoops", "tolerance 0.05"), ("Loops a, b", "polygons, non-zero"), ("combineLoops", "Union / Intersect /\nSubtract / Exclude"), ("splitIntoRegions", "outer loop + its holes"), ("loopsToPath", "one subpath per loop")]
    for i, (a, b) in enumerate(boxes):
        x = 20 + i * 155
        s.box_label(x, 46, 135, 60, a, b, "soft" if i == 3 else "box")
        if i:
            s.arrow(x - 20, 76, x, 76)
    names = ["Union", "Intersect", "Subtract (a - b)", "Exclude"]
    for i, name in enumerate(names):
        ox = 40 + i * 230
        s.text(ox + 80, 150, name, "t b", "middle")
        a = (ox + 55, 215)
        b = (ox + 105, 215)
        r = 48
        cid = "c%d" % i
        s.add('<defs><clipPath id="clipA%d"><circle cx="%s" cy="%s" r="%d"/></clipPath><mask id="maskB%d"><rect x="%s" y="150" width="200" height="140" fill="#fff"/><circle cx="%s" cy="%s" r="%d" fill="#000"/></mask></defs>' % (i, f(a[0]), f(a[1]), r, i, f(ox - 20), f(b[0]), f(b[1]), r))
        if i == 0:
            s.add('<circle cx="%s" cy="%s" r="%d" fill="%s" stroke="%s"/><circle cx="%s" cy="%s" r="%d" fill="%s" stroke="%s"/>' % (f(a[0]), f(a[1]), r, SOFT, ACCENT, f(b[0]), f(b[1]), r, SOFT, ACCENT))
            s.add('<path d="M%s %s" fill="none"/>' % (0, 0))
        elif i == 1:
            s.add('<circle cx="%s" cy="%s" r="%d" fill="none" stroke="%s" stroke-dasharray="4 3"/><circle cx="%s" cy="%s" r="%d" fill="none" stroke="%s" stroke-dasharray="4 3"/>' % (f(a[0]), f(a[1]), r, EDGE, f(b[0]), f(b[1]), r, EDGE))
            s.add('<circle cx="%s" cy="%s" r="%d" fill="%s" stroke="%s" clip-path="url(#clipA%d)"/>' % (f(b[0]), f(b[1]), r, ACCENT, ACCENT, i))
        elif i == 2:
            s.add('<circle cx="%s" cy="%s" r="%d" fill="none" stroke="%s" stroke-dasharray="4 3"/>' % (f(b[0]), f(b[1]), r, EDGE))
            s.add('<circle cx="%s" cy="%s" r="%d" fill="%s" stroke="%s" mask="url(#maskB%d)"/>' % (f(a[0]), f(a[1]), r, ACCENT, ACCENT, i))
        else:
            s.add('<path fill-rule="evenodd" fill="%s" stroke="%s" d="M%s %s a%d %d 0 1 0 %d 0 a%d %d 0 1 0 %d 0 M%s %s a%d %d 0 1 0 %d 0 a%d %d 0 1 0 %d 0"/>' %
                  (ACCENT, ACCENT, f(a[0] - r), f(a[1]), r, r, 2 * r, r, r, -2 * r, f(b[0] - r), f(b[1]), r, r, 2 * r, r, r, -2 * r))
    s.rect(20, 300, 920, 100, "panel", 6)
    s.text(34, 322, "Results follow one convention: outer outlines have positive signedArea, holes negative - so a result can be filled non-zero.", "t")
    s.lines(34, 345, ["Knife: cutAlongLine(region, line) divides a shape along a stroke carried straight on past its ends -> pieces on each side.",
                      "Eraser: strokeToLoops(pointer path, width) subtracted from shapes; lines use clipLineOutside.",
                      "Shape Builder: arrangeCells(shapes) cuts the plane into cells; dragging over cells unites them (Alt: erases them)."], "s", 16)
    s.save("pathfinder")


if __name__ == "__main__":
    shape_geometry()
    curve_pipeline()
    flattening()
    pathfinder()

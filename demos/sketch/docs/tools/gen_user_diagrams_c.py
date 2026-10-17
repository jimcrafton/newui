"""User-guide diagrams, part C: drawing and transform gestures."""
import math
from svgkit import *

OBJ = "#e8452c"
OBJ2 = "#6a4fc7"


def cell(s, x, y, w, h, title, caption=None):
    s.rect(x, y, w, h, "panel", 6)
    s.text(x + 10, y + 18, title, "t b")
    if caption:
        for i, row in enumerate(caption):
            s.text(x + 10, y + h - 10 - (len(caption) - 1 - i) * 13, row, "s")


def pointer(s, x, y, down=False):
    s.add('<path d="M%s %s l0 15 l4 -4 l3 7 l3 -1.5 l-3 -7 l5.5 0z" fill="%s" stroke="#fff" stroke-width="1"/>' % (f(x), f(y), "#222"))
    if down:
        s.circle(x, y, 7, "thin", 'stroke="%s"' % RED)


def anchor(s, x, y):
    s.rect(x - 3.5, y - 3.5, 7, 7, "box")


def draw_shapes():
    s = Svg(1000, 560, "Drawing shapes", "")
    s.text(500, 24, "Drawing shapes: drag on the canvas", "h", "middle")
    W, H = 320, 160
    specs = [("Rectangle / Ellipse", ["drag from corner to corner", "Shift = square / circle"]), ("Circle", ["drag out from the center"]), ("Rounded rectangle", ["Up / Down arrows while dragging", "change the corner radius"]),
             ("Polygon", ["drag from the center", "Up / Down = number of sides, Shift = upright"]), ("Star", ["drag from the center", "Up / Down = number of points"]), ("Arc", ["drag out from the center", "Up / Down = sweep; handles on its ends"])]
    for i, (title, cap) in enumerate(specs):
        x, y = 20 + (i % 3) * 330, 44 + (i // 3) * 255
        cell(s, x, y, W, 240, title, cap)
        cx, cy = x + 160, y + 105
        if i == 0:
            s.add('<rect x="%s" y="%s" width="130" height="80" fill="%s" stroke="#1b3a6b" stroke-width="2"/>' % (cx - 95, cy - 40, OBJ))
            s.add('<ellipse cx="%s" cy="%s" rx="45" ry="30" fill="%s" opacity="0.85"/>' % (cx + 60, cy + 30, ACCENT))
            s.arrow(cx - 95, cy - 40, cx + 35, cy + 40, acc=True); pointer(s, cx + 35, cy + 40)
        elif i == 1:
            s.add('<circle cx="%s" cy="%s" r="55" fill="%s" stroke="#1b3a6b" stroke-width="2"/>' % (cx, cy, ACCENT))
            s.arrow(cx, cy, cx + 55, cy, acc=True); s.dot(cx, cy, 3); pointer(s, cx + 55, cy)
        elif i == 2:
            s.add('<rect x="%s" y="%s" width="140" height="90" rx="24" fill="%s" stroke="#1b3a6b" stroke-width="2"/>' % (cx - 70, cy - 45, OBJ))
            s.add('<path d="M%s %s l0 -12 M%s %s l12 0" class="acc"/>' % (cx - 70 + 24, cy - 45, cx - 70, cy - 45 + 24))
        elif i == 3:
            pts = [(cx + 60 * math.cos(-math.pi / 2 + 2 * math.pi * k / 6), cy + 60 * math.sin(-math.pi / 2 + 2 * math.pi * k / 6)) for k in range(6)]
            s.add('<path d="%s" fill="%s" stroke="#1b3a6b" stroke-width="2"/>' % (poly(pts, True), OBJ2))
            s.arrow(cx, cy, pts[0][0], pts[0][1], acc=True); s.dot(cx, cy, 3)
        elif i == 4:
            pts = []
            for k in range(10):
                rr = 64 if k % 2 == 0 else 28
                a = -math.pi / 2 + math.pi * k / 5
                pts.append((cx + rr * math.cos(a), cy + rr * math.sin(a)))
            s.add('<path d="%s" fill="%s" stroke="#1b3a6b" stroke-width="2"/>' % (poly(pts, True), AMBER))
            s.arrow(cx, cy, pts[0][0], pts[0][1], acc=True); s.dot(cx, cy, 3)
        else:
            a0, a1 = -math.pi * 0.8, math.pi * 0.2
            r = 62
            p0 = (cx + r * math.cos(a0), cy + r * math.sin(a0))
            p1 = (cx + r * math.cos(a1), cy + r * math.sin(a1))
            s.add('<path d="M%s %s A%s %s 0 1 1 %s %s" fill="none" stroke="#1b3a6b" stroke-width="3"/>' % (f(p0[0]), f(p0[1]), r, r, f(p1[0]), f(p1[1])))
            s.circle(p0[0], p0[1], 5, "box"); s.circle(p1[0], p1[1], 5, "box"); s.dot(cx, cy, 3)
            s.line(cx, cy, p0[0], p0[1], "thin")
    s.text(500, 556, "Hold Shift while drawing a line for 45 degree steps. New shapes take the colors of the fill and stroke wells.", "s", "middle")
    s.save("draw-shapes")


def pen_pencil():
    s = Svg(1000, 520, "Pen and Pencil", "")
    s.text(500, 24, "Pen, Pencil and Direct Select", "h", "middle")
    cell(s, 20, 44, 310, 230, "Pen: click for corners", ["click, click, click ... a corner point each", "click the first point to close; Enter or", "double-click ends; Backspace removes the last"])
    pts = [(60, 200), (130, 100), (200, 190), (270, 110)]
    s.path(poly(pts), "acc")
    for p in pts:
        anchor(s, *p)
    pointer(s, 280, 120)
    cell(s, 345, 44, 310, 230, "Pen: click and drag for smooth points", ["drag pulls the handles out of the new point;", "Alt-drag breaks the handle pair apart"])
    a = [(390, 190), (500, 120), (600, 190)]
    s.path("M390 190 C420 110 460 100 500 120 S570 220 600 190", "acc")
    for p in a:
        anchor(s, *p)
    for (px, py), (hx, hy) in [((500, 120), (540, 108)), ((500, 120), (460, 132))]:
        s.line(px, py, hx, hy, "thin"); s.dot(hx, hy, 2.6, ACCENT)
    pointer(s, 545, 104, True)
    cell(s, 670, 44, 310, 230, "Pencil / Paintbrush: draw freehand", ["the stroke is smoothed and fitted with as few", "curves as keep it close; sharp corners stay sharp"])
    import random
    random.seed(3)
    raw = [(700 + t * 250, 190 - 70 * math.sin(t * 5) + random.uniform(-4, 4)) for t in [i / 60 for i in range(61)]]
    s.path(poly(raw), "thin", 'stroke="#aaa"')
    s.path("M700 190 C740 80 790 80 820 150 S900 240 950 190", "acc")
    cell(s, 20, 290, 480, 210, "Direct Select: edit the points", ["click an object to show its anchors; drag an anchor or a handle", "Alt-drag a corner point to pull curve handles out of it", "Delete removes the picked anchor"])
    s.path("M60 440 C100 330 180 330 220 400 S330 470 450 380", "acc")
    for (x, y) in [(60, 440), (220, 400), (450, 380)]:
        anchor(s, x, y)
    for (px, py, hx, hy) in [(60, 440, 100, 330), (220, 400, 180, 340), (220, 400, 260, 450), (450, 380, 330, 470)]:
        s.line(px, py, hx, hy, "thin"); s.dot(hx, hy, 2.6, ACCENT)
    s.rect(220 - 4, 400 - 4, 8, 8, "soft")
    cell(s, 515, 290, 465, 210, "Scissors", ["Scissors: click a path to cut it there - a closed path", "becomes open, an open path divides in two."])
    s.path("M560 430 C620 340 700 340 760 430", "acc")
    s.path("M800 430 C860 340 940 340 960 430", "acc")
    s.add('<path d="M772 380 l12 12 M784 380 l-12 12" stroke="%s" stroke-width="2.4"/>' % RED)
    s.icon("tool-scissors", 770, 345, 28)
    s.save("pen-pencil")


def transform_tools():
    s = Svg(1000, 520, "Rotate, Scale and Reflect tools", "")
    s.text(500, 24, "Rotate, Scale and Reflect", "h", "middle")
    for i, (title, icon, caps) in enumerate([("Rotate", "tool-rotate", ["1 select  2 click to set the center (optional)", "3 drag: the angle swept round the center", "Shift = 15 degree steps"]),
                                              ("Scale", "tool-scale", ["click to set the center, then drag away from", "or toward it; uniform by default", "Shift = each direction on its own"]),
                                              ("Reflect", "tool-reflect", ["click a point on the mirror line, then drag:", "the axis follows the pointer round that point", "Shift = 45 degree steps"])]):
        x = 20 + i * 330
        cell(s, x, 44, 320, 300, title, caps)
        s.icon(icon, x + 270, y := 50, 32)
        cx, cy = x + 160, 190
        if i == 0:
            s.add('<rect x="%s" y="%s" width="80" height="50" fill="%s" opacity="0.3" stroke="%s"/>' % (cx - 40, cy - 25, OBJ, EDGE))
            s.add('<g transform="rotate(35 %s %s)"><rect x="%s" y="%s" width="80" height="50" fill="%s" stroke="#1b3a6b"/></g>' % (cx, cy, cx - 40, cy - 25, OBJ))
            s.dot(cx, cy, 4, RED); s.path("M%s %s A60 60 0 0 1 %s %s" % (cx + 60, cy, cx + 49, cy + 34), "acc", 'marker-end="url(#arrowAcc)"')
        elif i == 1:
            s.add('<rect x="%s" y="%s" width="70" height="44" fill="%s" opacity="0.3" stroke="%s"/>' % (cx - 20, cy - 22, OBJ, EDGE))
            s.add('<rect x="%s" y="%s" width="120" height="76" fill="%s" opacity="0.85" stroke="#1b3a6b"/>' % (cx - 20, cy - 22, OBJ))
            s.dot(cx - 20, cy - 22, 4, RED); s.arrow(cx + 50, cy + 22, cx + 100, cy + 54, acc=True)
        else:
            s.add('<path d="M%s %s h60 l-30 -70z" fill="%s" opacity="0.4" stroke="%s"/>' % (cx - 90, cy + 40, OBJ, EDGE))
            s.add('<path d="M%s %s h-60 l30 -70z" fill="%s" stroke="#1b3a6b" transform="translate(%s 0)"/>' % (cx + 90, cy + 40, OBJ, 0))
            s.line(cx, cy - 70, cx, cy + 70, "acc", 'stroke-dasharray="5 4"'); s.dot(cx, cy + 40, 4, RED)
        s.rect(x + 10, 280, 300, 0.1, "thin")
    cell(s, 20, 360, 960, 140, "All three", ["The center is the small circle in the selection frame (also draggable with the Select tool).", "A click without dragging only moves it. Alt held when the button is let go leaves the original and adds a transformed copy.",
                                                  "Object > Transform has Rotate 90, Flip, Transform Again (Ctrl+Shift+D) to repeat the last move / scale / rotate, and Reset Bounding Box."])
    s.save("transform-tools")


def align_distribute():
    s = Svg(1000, 430, "Align and Distribute", "")
    s.text(500, 24, "Arrange > Align and Distribute", "h", "middle")
    def row(y, title, before, after, cap):
        cell(s, 20, y, 470, 175, title, cap)
        cell(s, 510, y, 470, 175, title + " - result")
        for (x, yy, w, h, c) in before:
            s.add('<rect x="%s" y="%s" width="%s" height="%s" fill="%s" stroke="#1b3a6b"/>' % (30 + x, y + 30 + yy, w, h, c))
        for (x, yy, w, h, c) in after:
            s.add('<rect x="%s" y="%s" width="%s" height="%s" fill="%s" stroke="#1b3a6b"/>' % (520 + x, y + 30 + yy, w, h, c))
    row(44, "Align Left", [(20, 0, 80, 30, OBJ), (60, 45, 120, 30, ACCENT), (120, 90, 60, 30, AMBER)], [(20, 0, 80, 30, OBJ), (20, 45, 120, 30, ACCENT), (20, 90, 60, 30, AMBER)], ["to the left edge of the selection (one object: the artboard)"])
    row(235, "Distribute Spacing", [(20, 40, 40, 40, OBJ), (110, 40, 70, 40, ACCENT), (320, 40, 30, 40, AMBER)], [(20, 40, 40, 40, OBJ), (155, 40, 70, 40, ACCENT), (320, 40, 30, 40, AMBER)], ["equal gaps between objects; the outer two stay put"])
    s.text(500, 422, "Align and Distribute treat a group as one object. Align Center / Middle use the selection's center; Distribute needs three or more objects.", "s", "middle")
    s.save("align-distribute")


if __name__ == "__main__":
    draw_shapes()
    pen_pencil()
    transform_tools()
    align_distribute()

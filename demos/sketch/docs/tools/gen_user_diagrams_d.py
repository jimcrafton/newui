"""User-guide diagrams, part D: cutting, color, text, brushes, libraries, clipping, artboards, snapping, quick start."""
import math
from svgkit import *
from gen_user_diagrams_c import cell, pointer, anchor, OBJ, OBJ2


def cut_tools():
    s = Svg(1000, 520, "Cutting and combining", "")
    s.text(500, 24, "Knife, Eraser, Shape Builder and Pathfinder", "h", "middle")
    # knife
    cell(s, 20, 44, 310, 220, "Knife", ["drag across filled shapes: every shape the", "stroke crosses (or only the selection) is cut"])
    s.add('<ellipse cx="120" cy="150" rx="70" ry="55" fill="%s" stroke="#1b3a6b"/>' % OBJ)
    s.path("M60 70 L190 235", "acc", 'stroke-dasharray="6 4"')
    s.add('<ellipse cx="250" cy="150" rx="50" ry="55" fill="%s" stroke="#1b3a6b" opacity="0.4"/>' % OBJ)
    s.add('<path d="M214 125 A50 55 0 0 1 214 190" fill="none"/>')
    s.icon("tool-knife", 270, 52, 28)
    # eraser
    cell(s, 345, 44, 310, 220, "Eraser", ["drag over shapes and lines to rub them out;", "Up / Down arrows change the size"])
    s.add('<rect x="380" y="100" width="230" height="90" fill="%s" stroke="#1b3a6b"/>' % ACCENT)
    s.add('<path d="M470 100 a24 24 0 0 0 48 0z" fill="#f4f4f4"/><circle cx="494" cy="145" r="22" fill="#f4f4f4"/>')
    s.path("M440 140 Q494 100 560 170", "thin", 'stroke-dasharray="3 3"')
    s.icon("tool-eraser", 590, 52, 28)
    # shape builder
    cell(s, 670, 44, 310, 220, "Shape Builder", ["select overlapping shapes, drag across the pieces", "to join them; Alt-drag erases them"])
    s.add('<circle cx="770" cy="150" r="52" fill="%s" opacity="0.55" stroke="#1b3a6b"/><circle cx="830" cy="150" r="52" fill="%s" opacity="0.55" stroke="#1b3a6b"/>' % (OBJ, ACCENT))
    s.path("M740 150 L860 150", "acc", 'stroke-dasharray="6 4"')
    s.icon("tool-shapebuilder", 940, 52, 28)
    # pathfinder row
    cell(s, 20, 280, 960, 220, "Object > Pathfinder (two or more selected shapes)", ["Unite = one outline of everything; Minus Front = the lowest shape without those above it; Intersect = where all overlap; Exclude = what is in exactly one.", "Curves are cut into short straight pieces first, so the results are polygons. Object > Path: Join, Reverse, Simplify, Outline Stroke."])
    for i, name in enumerate(["Unite", "Minus Front", "Intersect", "Exclude"]):
        x = 60 + i * 230
        s.text(x + 60, 322, name, "t b", "middle")
        a, b = (x + 40, 395), (x + 80, 395)
        s.add('<defs><clipPath id="ca%d"><circle cx="%s" cy="%s" r="40"/></clipPath><mask id="mb%d"><rect x="%s" y="340" width="200" height="120" fill="#fff"/><circle cx="%s" cy="%s" r="40" fill="#000"/></mask></defs>' % (i, a[0], a[1], i, x - 20, b[0], b[1]))
        if i == 0:
            s.add('<circle cx="%s" cy="%s" r="40" fill="%s" stroke="#1b3a6b"/><circle cx="%s" cy="%s" r="40" fill="%s" stroke="#1b3a6b"/>' % (a[0], a[1], ACCENT, b[0], b[1], ACCENT))
        elif i == 1:
            s.add('<circle cx="%s" cy="%s" r="40" fill="%s" stroke="#1b3a6b" mask="url(#mb%d)"/>' % (a[0], a[1], ACCENT, i))
        elif i == 2:
            s.add('<circle cx="%s" cy="%s" r="40" fill="%s" stroke="#1b3a6b" clip-path="url(#ca%d)"/>' % (b[0], b[1], ACCENT, i))
        else:
            s.add('<path fill-rule="evenodd" fill="%s" stroke="#1b3a6b" d="M%s %s a40 40 0 1 0 80 0 a40 40 0 1 0 -80 0 M%s %s a40 40 0 1 0 80 0 a40 40 0 1 0 -80 0"/>' % (ACCENT, a[0] - 40, a[1], b[0] - 40, b[1]))
    s.save("cut-tools")


def gradient_width():
    s = Svg(1000, 430, "Gradient and Width tools", "")
    s.text(500, 24, "Gradient tool and Width tool", "h", "middle")
    cell(s, 20, 44, 470, 370, "Gradient tool", ["drag across the object to lay a gradient axis (Shift = 45 degrees)", "drag the circle / square ends or the diamond stops; click the axis to add a stop;", "Alt-click a stop to delete it. With the Color panel on Stroke, it edits the stroke."])
    s.add('<defs><linearGradient id="gg" gradientUnits="userSpaceOnUse" x1="90" y1="120" x2="400" y2="270"><stop offset="0" stop-color="#f2b824"/><stop offset=".55" stop-color="#e8452c"/><stop offset="1" stop-color="#6a4fc7"/></linearGradient></defs>')
    s.add('<rect x="70" y="90" width="370" height="200" rx="14" fill="url(#gg)" stroke="#1b3a6b" stroke-width="2"/>')
    s.line(90, 120, 400, 270, "acc")
    s.circle(90, 120, 6, "box"); s.rect(394, 264, 12, 12, "box")
    for t in (0.55,):
        x, y = 90 + t * 310, 120 + t * 150
        s.add('<path d="M%s %s l7 8 l-7 8 l-7 -8z" fill="#fff" stroke="%s" stroke-width="1.6"/>' % (f(x), f(y - 8), INK))
    cell(s, 510, 44, 470, 370, "Width tool", ["drag a path to add a width point; drag its handles to widen the", "stroke, the diamond to slide it; Alt-click a point to delete it.", "Stroke panel > Profile: uniform, taper, bulge presets."])
    pts = [(540 + t * 410, 220 - 70 * math.sin(t * math.pi * 1.5)) for t in [i / 60 for i in range(61)]]
    nr = []
    for i, p in enumerate(pts):
        a = pts[max(i - 1, 0)]; b = pts[min(i + 1, 60)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        l = math.hypot(dx, dy) or 1
        nr.append((dy / l, -dx / l))
    left = [(p[0] + n[0] * (3 + 16 * math.sin(math.pi * i / 60)), p[1] + n[1] * (3 + 16 * math.sin(math.pi * i / 60))) for i, (p, n) in enumerate(zip(pts, nr))]
    right = [(p[0] - n[0] * (3 + 16 * math.sin(math.pi * i / 60)), p[1] - n[1] * (3 + 16 * math.sin(math.pi * i / 60))) for i, (p, n) in enumerate(zip(pts, nr))]
    s.add('<path d="%s" fill="#1b3a6b"/>' % poly(left + right[::-1], True))
    mid = pts[30]
    n = nr[30]
    s.line(mid[0] + n[0] * 19, mid[1] + n[1] * 19, mid[0] - n[0] * 19, mid[1] - n[1] * 19, "acc")
    s.circle(mid[0] + n[0] * 19, mid[1] + n[1] * 19, 4.5, "box"); s.circle(mid[0] - n[0] * 19, mid[1] - n[1] * 19, 4.5, "box")
    s.add('<path d="M%s %s l6 6 l-6 6 l-6 -6z" fill="#fff" stroke="%s" stroke-width="1.6"/>' % (f(mid[0]), f(mid[1] - 6), INK))
    s.save("gradient-width")


def text_guide():
    s = Svg(1000, 560, "Text, text on a path and text between paths", "")
    s.text(500, 24, "Text", "h", "middle")
    cell(s, 20, 44, 310, 230, "Text tool", ["click the canvas and type; Enter = new line", "Ctrl+Up / Down = size; Esc or click elsewhere", "finishes. Click existing text to edit it."])
    s.add('<text x="50" y="140" style="font-size:36px;font-weight:700;fill:#1b3a6b">Hello,</text><text x="50" y="182" style="font-size:36px;font-weight:700;fill:#1b3a6b">sketch<tspan style="fill:#1e6fe0">|</tspan></text>')
    cell(s, 345, 44, 310, 230, "Text on a path", ["select the text and ONE path, then Object >", "Text on Path > Make Text on Path.", "Start % and Flip are in the Text panel."])
    s.path("M370 200 Q500 60 630 200", "thin", 'stroke="#aaa"')
    word = "TEXT ON A PATH"
    for i, ch in enumerate(word):
        t = (i + 0.5) / len(word)
        x = 370 + 260 * t
        y = 200 - 2 * 70 * 2 * (t * (1 - t)) * 1.0
        a = math.degrees(math.atan2(-(4 * 70 * (1 - 2 * t)) / 1.0 * 0.5, 260))
        s.add('<text x="%s" y="%s" transform="rotate(%s %s %s)" text-anchor="middle" style="font-size:22px;font-weight:700;fill:#c0392b">%s</text>' % (f(x), f(y - 6), f(-a * 0 + math.degrees(math.atan2(-(280 * (1 - 2 * t)) * 0.5 / 1.0, 260))), f(x), f(y - 6), ch))
    cell(s, 670, 44, 310, 230, "Text between two paths", ["select the text and TWO paths, then Make Text", "Between Paths: the text is stretched like an", "envelope between them (the higher one is the top)."])
    s.path("M690 110 Q820 80 960 120", "thin", 'stroke="#aaa"')
    s.path("M690 210 Q820 250 960 190", "thin", 'stroke="#aaa"')
    for i, ch in enumerate("ENVELOPE"):
        t = (i + 0.5) / 8
        x = 690 + 270 * t
        top = 110 - 30 * math.sin(t * math.pi) * 1.0 + 10 * t
        bottom = 210 + 40 * math.sin(t * math.pi) - 20 * t
        sy = (bottom - top) / 40
        s.add('<text transform="translate(%s %s) scale(1 %s)" text-anchor="middle" style="font-size:40px;font-weight:700;fill:#2f7d4f">%s</text>' % (f(x), f(bottom - (bottom - top) * 0.24), f(sy), ch))
    s.rect(20, 290, 960, 250, "soft", 6)
    s.text(34, 312, "Good to know", "t b")
    s.lines(34, 336, ["- The text keeps its words: after Make Text on Path you can still change the font, size, tracking or retype it with the Text tool (the caret is drawn on the path).",
                      "- Make copies the paths into the text: the original paths stay where they are; delete or restyle them as you like. Moving the text moves its copy of the path with it.",
                      "- Release Text from Path makes it plain text again, where its start is now. Undo takes it off in one step.",
                      "- Text is a shape: give it a fill, stroke, gradient, effects, or put it in a clipping mask. Pathfinder works on text outlines too.",
                      "- Clicking inside warped text puts the typing caret at the end of the text.",
                      "- The Text panel captions Size, Track and Lead scrub: drag them sideways."], "s", 22)
    s.save("text-guide")


def brushes_guide():
    s = Svg(1000, 470, "Brushes", "")
    s.text(500, 24, "Brushes", "h", "middle")
    names = [("Calligraphic", "a slanted oval nib: thick across,", "thin along it; weight = nib size"), ("Art", "artwork stretched along the stroke;", "weight 1 = its own size"), ("Scatter", "copies of artwork along the stroke;", "spacing, stray, size, turn")]
    for i, (n, a, b) in enumerate(names):
        x = 20 + i * 330
        cell(s, x, 44, 320, 200, n, [a, b])
        pts = [(x + 25 + t * 270, 150 - 40 * math.sin(t * 6.28)) for t in [k / 50 for k in range(51)]]
        if i == 0:
            for k in range(len(pts) - 1):
                for t in (0, 1):
                    p = pts[k + t]
                    s.add('<ellipse cx="%s" cy="%s" rx="10" ry="2.8" transform="rotate(-45 %s %s)" fill="#1b3a6b" opacity="0.2"/>' % (f(p[0]), f(p[1]), f(p[0]), f(p[1])))
        elif i == 1:
            nr = []
            for k, p in enumerate(pts):
                a2 = pts[max(k - 1, 0)]; b2 = pts[min(k + 1, 50)]
                dx, dy = b2[0] - a2[0], b2[1] - a2[1]
                l = math.hypot(dx, dy) or 1
                nr.append((dy / l, -dx / l))
            w = [8 * math.sin(math.pi * k / 50) + 0.5 for k in range(51)]
            L = [(p[0] + n[0] * ww, p[1] + n[1] * ww) for p, n, ww in zip(pts, nr, w)]
            R = [(p[0] - n[0] * ww, p[1] - n[1] * ww) for p, n, ww in zip(pts, nr, w)]
            s.add('<path d="%s" fill="#c0392b"/>' % poly(L + R[::-1], True))
        else:
            for k in range(0, 51, 4):
                s.add('<circle cx="%s" cy="%s" r="%s" fill="#1b3a6b"/>' % (f(pts[k][0]), f(pts[k][1] + ((k * 37) % 9 - 4) * 2), 3 + (k * 13) % 4))
    s.rect(20, 260, 960, 190, "soft", 6)
    s.text(34, 282, "Using brushes", "t b")
    steps = [("1", "Object > Brush > Add Brush Presets,", "or make your own: select artwork,", "then New Art / Scatter Brush"), ("2", "Pick the brush in the Brushes panel;", "its numbers appear (angle, roundness,", "spacing ...) - edit and press Enter"), ("3", "Select a stroked path and press Apply;", "or choose the Paintbrush tool and", "draw: it paints with the active brush"), ("4", "Remove Brush returns to a plain stroke;", "the stroke color is the nib color; art and", "scatter keep the artwork's own colors")]
    for i, (n, a, b, c) in enumerate(steps):
        x = 40 + i * 235
        s.callout(n, x + 8, 306)
        s.lines(x + 24, 310, [a, b, c], "s", 15)
    s.text(34, 400, "Editing a brush changes every stroke that uses it (one undo step). A brush that is in use cannot be deleted. Expand Appearance and Outline Stroke ignore the brush.", "s")
    s.text(34, 418, "Brushes are saved with the file and travel with copied objects.", "s")
    s.save("brushes-guide")


def libraries_guide():
    s = Svg(1000, 430, "Symbols, patterns and pictures", "")
    s.text(500, 24, "Symbols, Patterns and Pictures", "h", "middle")
    cell(s, 20, 44, 310, 230, "Symbols", ["New Symbol turns the selection into one instance", "of a library symbol; Place adds more. Redefine", "changes every instance; Break Link frees one."])
    for i, (x, y, sc, rot) in enumerate([(80, 130, 1.0, 0), (180, 150, 1.4, 15), (260, 140, 0.8, -20)]):
        s.add('<g transform="translate(%s %s) rotate(%s) scale(%s)"><path d="M0 -26 L8 -8 L28 -8 L12 4 L18 24 L0 12 L-18 24 L-12 4 L-28 -8 L-8 -8z" fill="%s" stroke="#1b3a6b" stroke-width="2"/><circle r="7" fill="%s"/></g>' % (x, y, rot, sc, AMBER, ACCENT))
    cell(s, 345, 44, 310, 230, "Patterns", ["Make Pattern turns the selection into a tile (it stays).", "Fill / Stroke apply it; scale and angle are in the", "Patterns panel. The tile stays sharp when zoomed."])
    s.add('<defs><pattern id="pt" width="26" height="26" patternUnits="userSpaceOnUse" patternTransform="rotate(20)"><rect width="26" height="26" fill="#fff4d6"/><circle cx="13" cy="13" r="7" fill="#e8452c"/></pattern></defs>')
    s.add('<rect x="380" y="95" width="240" height="120" rx="10" fill="url(#pt)" stroke="#1b3a6b" stroke-width="2"/>')
    cell(s, 670, 44, 310, 230, "Pictures (File > Place Image)", ["PNG, JPEG, BMP, QOI; phone photos come out upright.", "It is an object: move, scale, turn, shear, set opacity,", "blend, clip it, or Trace Image it from the file."])
    s.add('<defs><linearGradient id="pic" x1="0" y1="0" x2="1" y2="1"><stop offset="0" stop-color="#2b5bd7"/><stop offset="1" stop-color="#ffb24d"/></linearGradient></defs>')
    s.add('<g transform="rotate(-8 820 150)"><rect x="740" y="100" width="160" height="100" fill="url(#pic)" stroke="#fff" stroke-width="5"/><circle cx="790" cy="130" r="9" fill="#fff"/><path d="M740 200 L800 150 L840 180 L870 160 L900 200z" fill="#2f7d4f"/></g>')
    s.rect(20, 290, 960, 120, "soft", 6)
    s.lines(34, 314, ["- The libraries live in the file; copy and paste between files brings a symbol, pattern, brush or picture along once, however often you paste.",
                      "- A symbol instance can be sheared and rotated like any shape, but it is one object: Break Link to Symbol to edit its parts.",
                      "- Delete a symbol, pattern or brush from its panel only when nothing uses it.",
                      "- Large pictures draw from reduced copies when zoomed out, so a 12-megapixel photo stays quick."], "s", 22)
    s.save("libraries-guide")


def clip_artboards():
    s = Svg(1000, 470, "Clipping masks and artboards", "")
    s.text(500, 24, "Clipping Masks and Artboards", "h", "middle")
    cell(s, 20, 44, 470, 230, "Clipping mask", ["put the shape that should show through on TOP, select it with", "the artwork, Object > Clipping Mask > Make (Ctrl+7).", "Release: Ctrl+Shift+7. The mask's own fill and stroke are not drawn."])
    s.add('<defs><clipPath id="cm"><circle cx="150" cy="150" r="58"/></clipPath></defs>')
    s.add('<g clip-path="url(#cm)">')
    for i in range(10):
        s.add('<rect x="%s" y="90" width="14" height="120" fill="%s"/>' % (80 + i * 14, [ACCENT, OBJ][i % 2]))
    s.add('</g>')
    s.circle(150, 150, 58, "thin", 'stroke-dasharray="4 3"')
    s.arrow(240, 150, 300, 150, acc=True)
    s.add('<g clip-path="url(#cm2)"><defs><clipPath id="cm2"><circle cx="390" cy="150" r="58"/></clipPath></defs>')
    for i in range(10):
        s.add('<rect x="%s" y="90" width="14" height="120" fill="%s"/>' % (320 + i * 14, [ACCENT, OBJ][i % 2]))
    s.add('</g>')
    cell(s, 510, 44, 470, 230, "Artboards", ["Artboard tool: drag to make one, click to select it, drag it or its", "handles to move / resize; Delete removes it. The Artboards panel (and", "Object > Artboards) switch, add, delete and fit them."])
    for (x, y, w, h, c, name) in [(540, 100, 120, 150, "#fff", "Artboard 1"), (700, 100, 200, 120, "#fff", "Artboard 2")]:
        s.add('<rect x="%s" y="%s" width="%s" height="%s" fill="%s" stroke="%s" stroke-width="%s"/>' % (x, y, w, h, c, ACCENT if name == "Artboard 2" else EDGE, 2.5 if name == "Artboard 2" else 1))
        s.text(x, y - 6, name, "s")
    s.add('<circle cx="600" cy="170" r="26" fill="%s"/><rect x="740" y="130" width="110" height="60" fill="%s"/>' % (OBJ, ACCENT))
    s.rect(20, 290, 960, 160, "soft", 6)
    s.lines(34, 314, ["- The active artboard (blue outline) is \"the page\": Align with one object centers it on the page, Image Trace places the result on it, File > Export As Image writes it.",
                      "- File > Export All Artboards writes one image per artboard (name_1_Artboard-1.png ...).",
                      "- File > Document Setup changes the size, resolution, color mode and background of the active artboard.",
                      "- Clicking inside an artboard with the Artboard tool makes it the active one; View > Fit Page fits all the artboards; Object > Artboards > Fit Artboard in Window fits the active one.",
                      "- A clipping mask needs two or more objects (the top one is the mask) and a mask with some area."], "s", 22)
    s.save("clip-artboards")


def guides_snapping():
    s = Svg(1260, 460, "Rulers, guides and snapping", "")
    s.text(630, 24, "Rulers, Guides and Snapping", "h", "middle")
    s.rect(20, 44, 560, 400, "box", 4)
    s.rect(20, 44, 560, 18, "panel"); s.rect(20, 44, 18, 400, "panel")
    for i in range(10):
        s.line(40 + i * 54, 44, 40 + i * 54, 56, "thin")
        s.text(42 + i * 54, 60, str(i * 100), "s")
    s.add('<rect x="38" y="62" width="540" height="380" fill="#7a7a7a"/><rect x="80" y="80" width="420" height="350" fill="#fff"/>')
    for i in range(8):
        s.line(80 + i * 60, 80, 80 + i * 60, 430, "thin", 'stroke="#e5e5e5"')
    for i in range(7):
        s.line(80, 80 + i * 55, 500, 80 + i * 55, "thin", 'stroke="#e5e5e5"')
    s.line(200, 62, 200, 442, "acc", 'stroke="#0aa5d0"')
    s.add('<rect x="120" y="140" width="80" height="60" fill="%s" stroke="#1b3a6b"/>' % OBJ)
    s.add('<rect x="280" y="250" width="120" height="70" fill="%s" stroke="#1b3a6b"/>' % ACCENT)
    s.line(60, 285, 520, 285, "thin", 'stroke="#ec008c"')
    s.line(340, 140, 340, 330, "thin", 'stroke="#ec008c"')
    s.add('<rect x="%s" y="%s" width="120" height="60" fill="%s" opacity="0.7" stroke="#1b3a6b"/>' % (340, 255, AMBER))
    s.callout(1, 200, 50); s.callout(2, 300, 285); s.callout(3, 460, 100); s.callout(4, 30, 250)
    legend(s, 610, 70, [(1, "Drag from a ruler to make a guide; drag it to move it; drag it back onto the ruler to remove it"),
                        (2, "Smart Guides: a magenta line shows what the dragged edge or center caught (other objects' edges and centers)"),
                        (3, "The grid: View > Show Grid; Snap to Grid pulls drawing and moving onto it (toolbar button)"), (4, "Rulers show points from the document origin; View > Show Rulers")], gap=64)
    s.text(610, 340, "View menu: Show / Lock / Clear Guides, Smart Guides, Snap to Grid.", "s")
    s.text(610, 356, "Snapping catches within about 6 screen pixels; Shift while drawing a", "s")
    s.text(610, 370, "line or rotating constrains angles; hold Alt while resizing for the center.", "s")
    s.save("guides-snapping")


def quickstart():
    s = Svg(1000, 350, "A first drawing in six steps", "")
    s.text(500, 24, "A first drawing", "h", "middle")
    steps = [("Draw", "Pick the Rectangle or Star tool in the palette and drag on the page.", "tool-rectangle"), ("Color", "Click a swatch in the Color panel (the Fill / Stroke target is at its top).", "fill-solid"),
             ("Edit", "Select tool: click the shape, drag its handles; Direct Select edits points.", "tool-select"), ("Combine", "Draw more, then Object > Pathfinder or the Shape Builder.", "tool-shapebuilder"),
             ("Organize", "Group (Ctrl+G), arrange with Arrange / Layers, align with the toolbar.", "group"), ("Save / export", "File > Save (.svg or .skb); File > Export As Image for a PNG.", "save")]
    for i, (t, d, icon) in enumerate(steps):
        x = 20 + (i % 3) * 330
        y = 46 + (i // 3) * 140
        s.rect(x, y, 320, 126, "panel", 6)
        s.callout(i + 1, x + 20, y + 24)
        s.icon(icon, x + 38, y + 10, 28)
        s.text(x + 76, y + 30, t, "h")
        words = d.split(" ")
        line, rows = "", []
        for w in words:
            if len(line) + len(w) > 46:
                rows.append(line)
                line = ""
            line += w + " "
        rows.append(line)
        s.lines(x + 16, y + 62, rows, "s", 16)
    s.text(500, 336, "Ctrl+Z / Ctrl+Y undo and redo every step; Esc cancels a drag or deselects.", "s", "middle")
    s.save("quickstart")


if __name__ == "__main__":
    cut_tools()
    gradient_width()
    text_guide()
    brushes_guide()
    libraries_guide()
    clip_artboards()
    guides_snapping()
    quickstart()

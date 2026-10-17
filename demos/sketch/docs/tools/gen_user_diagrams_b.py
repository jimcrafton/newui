"""User-guide diagrams, part B: the panels."""
import math
from svgkit import *


class P:
    """Draws one panel (header + rows) at x, y with width w; returns the next free y."""

    def __init__(self, s, x, y, w, title, info=""):
        self.s, self.x, self.w = s, x, w
        s.rect(x, y, w, 24, "panel", 0)
        s.text(x + 8, y + 16, title, "t b")
        if info:
            s.text(x + w - 8, y + 16, info, "s", "end")
        self.y = y + 32
        self.top = y

    def label(self, x, text, w=None):
        self.s.text(self.x + x, self.y + 14, text, "s")

    def field(self, x, w, text="", right=False):
        self.s.rect(self.x + x, self.y, w, 20, "field", 2)
        self.s.text(self.x + x + 5, self.y + 14, text, "s")

    def drop(self, x, w, text=""):
        self.field(x, w, text)
        self.s.path("M%s %s l4 5 l4 -5z" % (f(self.x + x + w - 14), f(self.y + 7)), "line")

    def icons(self, x, names, size=22, checked=None):
        for i, n in enumerate(names):
            bx = self.x + x + i * (size + 4)
            self.s.rect(bx, self.y - 1, size + 2, size + 2, "soft" if checked == i else "box", 3)
            self.s.icon(n, bx + 2, self.y + 1, size - 2)

    def row(self, h=28):
        self.y += h

    def end(self):
        self.s.add('<rect x="%s" y="%s" width="%s" height="%s" fill="none" stroke="%s"/>' % (f(self.x), f(self.top), f(self.w), f(self.y - self.top), EDGE))
        return self.y


def panels_1():
    s = Svg(1000, 640, "Transform and Color panels", "")
    s.text(500, 24, "Transform and Color", "h", "middle")
    p = P(s, 20, 44, 300, "Transform", "1 object")
    p.label(8, "X"); p.field(26, 80, "120"); p.label(120, "Y"); p.field(138, 80, "88")
    s.callout(1, 235, p.y + 10); p.row()
    p.label(8, "W"); p.field(26, 80, "140"); p.label(120, "H"); p.field(138, 80, "90"); p.icons(228, ["link-aspect"], 22)
    s.callout(2, 285, p.y + 24); p.row()
    p.label(8, "Angle"); p.field(48, 80, "0"); p.label(138, "deg")
    s.callout(3, 190, p.y + 10)
    p.row(); e = p.end()
    legend(s, 20, e + 24, [(1, "Position of the selection's box (points)"), (2, "Size; the link keeps proportions"), (3, "Rotation of the frame")], gap=22)
    s.text(20, e + 100, "Drag a caption (X, W, Angle ...) to scrub:", "s")
    s.text(20, e + 114, "1 unit a pixel; Shift 0.1; Ctrl+Shift 0.01.", "s")
    s.text(20, e + 132, "Or type a value and press Enter.", "s")
    p = P(s, 340, 44, 640, "Color", "Fill  ●   Stroke  ○")
    p.icons(8, ["fill-none", "fill-solid", "fill-linear", "fill-radial"], 24, checked=2)
    s.callout(1, 468, 90)
    p.row(32)
    s.rect(p.x + 8, p.y, 600, 24, "box", 2)
    s.add('<defs><linearGradient id="gb"><stop offset="0" stop-color="#f2b824"/><stop offset="0.55" stop-color="#e8452c"/><stop offset="1" stop-color="#6a4fc7"/></linearGradient></defs>')
    s.add('<rect x="%s" y="%s" width="600" height="24" fill="url(#gb)"/>' % (f(p.x + 8), f(p.y)))
    for t in (0, 0.55, 1):
        s.add('<path d="M%s %s l-5 9 h10z" fill="#fff" stroke="%s"/>' % (f(p.x + 8 + t * 600), f(p.y + 24), INK))
    s.callout(2, 960, p.y + 12); p.row(40)
    p.label(8, "Stop"); p.field(44, 56, "55%"); p.label(120, "Angle"); p.field(160, 60, "35"); p.icons(236, ["stop-add", "stop-delete", "gradient-reverse"], 22)
    s.callout(3, 450, p.y + 10); p.row(28)
    p.label(8, "Type"); p.label(44, "Linear"); p.label(130, "Spread"); p.drop(180, 100, "Pad"); p.label(300, "Apply"); p.drop(344, 180, "Across the object")
    s.callout(4, 560, p.y + 10); p.row(32)
    for i, name in enumerate(["Gray", "RGB", "CMYK", "HSB"]):
        s.rect(p.x + 8 + i * 70, p.y, 70, 22, "soft" if i == 1 else "box", 3)
        s.text(p.x + 43 + i * 70, p.y + 15, name, "s", "middle")
    s.callout(5, p.x + 306, p.y + 11); p.row(30)
    for i, (n, v) in enumerate([("R", 232), ("G", 69), ("B", 44), ("A", 100)]):
        p.label(8, n)
        s.rect(p.x + 28, p.y + 4, 480, 12, "field", 2)
        s.rect(p.x + 28 + v / 255 * 460, p.y + 1, 10, 18, "soft", 2)
        p.field(520, 56, str(v))
        p.row(26)
    s.callout(6, 620, p.y - 70)
    p.label(8, "Hex"); p.field(40, 100, "#E8452C"); s.callout(7, p.x + 160, p.y + 10); p.row(28)
    p.label(8, "Swatches"); p.icons(80, ["layer-add", "layer-delete"], 22); p.row(28)
    for i, c in enumerate(["#000000", "#555555", "#aaaaaa", "#ffffff", "#e8452c", "#f2b824", "#2f7d4f", "#0078d4", "#6a4fc7", "#1b3a6b", "#00aeef", "#ec008c"]):
        s.add('<rect x="%s" y="%s" width="24" height="24" fill="%s" stroke="%s"/>' % (f(p.x + 8 + i * 28), f(p.y), c, EDGE))
    s.callout(8, 360, p.y + 12); p.row(28); e = p.end()
    legend(s, 340, e + 24, [(1, "What the fill is: none, solid, linear or radial gradient"), (2, "Gradient bar: drag a stop to move it, click the bar to add one, Alt-click a stop to delete it"),
                            (3, "Selected stop's position; angle of the gradient; add, delete, reverse"), (4, "Spread past the ends; apply across the object or (for strokes) along the path"),
                            (5, "Color model of the sliders (Gray, RGB, CMYK, HSB)"), (6, "Sliders and value fields; A = opacity of the color"), (7, "Hex color (also none)"), (8, "Swatches: click to apply, + saves the current color")], gap=20)
    s.save("panels-transform-color")


def panels_2():
    s = Svg(1300, 600, "Stroke, Appearance and Graphic Styles panels", "")
    s.text(650, 24, "Stroke, Appearance, Graphic Styles", "h", "middle")
    p = P(s, 20, 44, 330, "Stroke", "Stroke 1")
    p.label(8, "Weight"); p.field(56, 64, "4 pt"); p.label(136, "Miter"); p.field(176, 48, "10")
    s.callout(1, 250, p.y + 10); p.row(30)
    p.label(8, "Cap"); p.icons(36, ["cap-butt", "cap-round", "cap-square"], 24, 0); p.label(130, "Join"); p.icons(160, ["join-miter", "join-round", "join-bevel"], 24, 0)
    s.callout(2, 270, p.y + 12); p.row(32)
    p.label(8, "Align"); p.icons(44, ["align-stroke-center", "align-stroke-inside", "align-stroke-outside"], 24, 0); p.label(134, "Profile"); p.icons(180, ["profile-uniform", "profile-taper", "profile-bulge"], 24, 0)
    s.callout(3, 300, p.y + 12); p.row(32)
    p.label(8, "Dashed"); s.rect(p.x + 56, p.y + 3, 14, 14, "box", 2); p.field(80, 40, "12"); p.field(124, 40, "6"); p.field(168, 40, ""); p.field(212, 40, "")
    s.callout(4, 290, p.y + 10); p.row(28)
    p.label(8, "Arrows"); p.drop(52, 60, "None"); p.drop(118, 60, "Filled"); p.label(190, "Scale"); p.field(224, 50, "100%")
    s.callout(5, 300, p.y + 10); p.row(28); e1 = p.end()
    p = P(s, 20, e1 + 20, 330, "Graphic Styles")
    p.icons(8, ["graphic-style-new", "delete"], 22); p.row(30)
    for i, c in enumerate(["#ffffff", "#e8452c", "#0078d4", "#00aeef", "#ffffff", "#6a4fc7"]):
        s.add('<rect x="%s" y="%s" width="40" height="30" rx="3" fill="%s" stroke="%s"/>' % (f(p.x + 8 + i * 50), f(p.y), c, EDGE))
    s.callout(6, 330, p.y + 15); p.row(40); e2 = p.end()
    # appearance
    p = P(s, 380, 44, 340, "Appearance", "Path: Rounded Rectangle")
    rows = [("Stroke", "4 pt   100%", "#1b3a6b", True), ("Fill", "Gradient   100%", "#e8452c", False), ("Fill", "Solid   30%  Multiply", "#0078d4", False), ("Drop Shadow", "Multiply, 75%, x 4 y 4, blur 6", None, False), ("Opacity", "75%  Multiply", None, False)]
    for i, (a, b, c, st) in enumerate(rows):
        y = p.y + i * 30
        s.rect(p.x + 8, y, 324, 26, "soft" if i == 3 else "box", 3)
        s.icon("eye", p.x + 12, y + 5, 16)
        s.text(p.x + 36, y + 17, a, "s b")
        s.text(p.x + 110, y + 17, b, "s")
        if c:
            s.add('<rect x="%s" y="%s" width="22" height="14" fill="%s" %s/>' % (f(p.x + 300), f(y + 6), c if not st else "none", ('stroke="%s" stroke-width="3"' % c) if st else 'stroke="#333"'))
    s.callout(1, 705, p.y + 15)
    p.y += 5 * 30 + 6
    p.label(8, "Opacity"); s.rect(p.x + 64, p.y + 4, 180, 12, "field", 2); s.rect(p.x + 64 + 135, p.y + 1, 10, 18, "soft", 2); p.field(256, 60, "75%"); s.callout(2, 705, p.y + 10); p.row(28)
    p.label(8, "Blend"); p.drop(56, 250, "Multiply"); p.row(28)
    p.label(8, "X"); p.field(26, 50, "4"); p.label(90, "Y"); p.field(106, 50, "4"); p.label(170, "Blur"); p.field(204, 50, "6"); s.callout(3, 705, p.y + 10); p.row(28)
    p.label(8, "Color"); s.rect(p.x + 56, p.y + 3, 22, 14, "box"); p.field(86, 90, "#000000"); p.row(28)
    p.icons(8, ["new-stroke", "new-fill", "fx", "eye", "item-up", "item-down", "clear-appearance", "duplicate-item", "delete"], 26)
    s.callout(4, 705, p.y + 14); p.row(34); e3 = p.end()
    legend(s, 740, 60, [(1, "Weight and miter limit"), (2, "Caps and joins"), (3, "Align: where the stroke sits (not connected yet). Profile: uniform, taper or bulge width"), (4, "Dashes (dash, gap, dash, gap)"),
                        (5, "Arrowheads (not connected yet)"), (6, "Graphic Styles: click to apply, + saves the selected look")], gap=34)
    legend(s, 380, e3 + 24, [(1, "Appearance list, bottom to top: click a row to edit it; eye hides it; drag to reorder with the arrows"), (2, "Opacity and blend mode of the row (or of the whole object on the last row)"),
                             (3, "Options of the chosen effect (shadow, glow, blur, round corners, offset, roughen)"), (4, "Add stroke, add fill, add effect (fx menu), show / hide, move up / down, clear, duplicate, delete")], gap=22)
    s.save("panels-stroke-appearance")


def panels_3():
    s = Svg(1340, 560, "Text, Symbols, Patterns and Brushes panels", "")
    s.text(670, 24, "Text, Symbols, Patterns, Brushes", "h", "middle")
    p = P(s, 20, 44, 330, "Text")
    p.label(8, "Font"); p.drop(44, 270, "Segoe UI"); p.row(30)
    p.label(8, "Size"); p.field(44, 50, "24"); p.icons(104, ["text-bold", "text-italic", "text-align-left", "text-align-center", "text-align-right"], 22, 2)
    s.callout(1, 330, p.y + 11); p.row(30)
    p.label(8, "On path"); p.label(60, "Start"); p.field(96, 50, "0"); p.label(150, "%"); p.icons(172, ["text-flip"], 22); s.callout(2, 330, p.y + 11); p.row(30)
    p.label(8, "Track"); p.field(44, 50, "0"); p.label(110, "Lead"); p.field(142, 50, "100"); p.label(198, "%"); s.callout(3, 330, p.y + 11); p.row(30); e1 = p.end()
    p = P(s, 20, e1 + 18, 330, "Symbols")
    p.drop(8, 312, "Badge"); p.row(28)
    p.icons(8, ["symbol-new", "symbol-place", "symbol-redefine", "symbol-break", "layer-delete"], 24); s.callout(4, 330, p.y + 12); p.row(30)
    p.label(8, "Name"); p.field(48, 262, "Badge"); p.row(30); e2 = p.end()
    p = P(s, 20, e2 + 18, 330, "Patterns")
    p.drop(8, 312, "Dots"); p.row(28)
    p.icons(8, ["pattern-new", "pattern-fill", "pattern-stroke", "layer-delete"], 24); s.callout(5, 330, p.y + 12); p.row(30)
    p.label(8, "Name"); p.field(48, 262, "Dots"); p.row(28)
    p.label(8, "Scale"); p.field(48, 60, "100"); p.label(112, "%"); p.label(130, "Angle"); p.field(170, 60, "0"); p.row(30); e3 = p.end()
    p = P(s, 380, 44, 330, "Brushes")
    p.drop(8, 312, "Calligraphic 45"); p.row(28)
    p.icons(8, ["brush-new", "brush-art", "brush-scatter", "brush-apply", "brush-remove", "brush-presets", "layer-delete"], 24); s.callout(6, 725, p.y + 12); p.row(30)
    p.label(8, "Name"); p.field(48, 262, "Calligraphic 45"); p.row(28)
    p.label(8, "Angle"); p.field(48, 56, "45"); p.label(120, "Round"); p.field(168, 56, "25"); s.callout(7, 725, p.y + 10); p.row(28)
    p.label(8, "Space"); p.field(48, 56, ""); p.label(120, "Stray"); p.field(168, 56, ""); s.callout(8, 725, p.y + 10); p.row(28)
    p.label(8, "Size"); p.field(48, 56, ""); p.label(120, "Turn"); p.field(168, 56, ""); p.row(28)
    p.label(8, "Follow path"); p.icons(76, ["brush-follow"], 22); p.row(30); e4 = p.end()
    legend(s, 740, 60, [(1, "Font, size, bold, italic, alignment of the selected text"), (2, "Text on a path: where it starts (% of the path) and which side"), (3, "Tracking (extra space per character) and leading (line distance, %)"),
                        (4, "New symbol from the selection, place, redefine, break link, delete"), (5, "Make a pattern, fill / stroke with it; scale and angle edit the selected shape's pattern"),
                        (6, "New calligraphic / art / scatter brush, apply to the selection, back to a plain stroke, add presets, delete"), (7, "Calligraphic: nib angle and roundness (a thin nib = 25)"), (8, "Scatter: spacing, stray, size and turn variation; follow the path")], gap=46)
    s.text(380, e4 + 22, "Drag the Size, Track and Lead captions to scrub their numbers.", "s")
    s.save("panels-text-libraries")


def panels_4():
    s = Svg(1300, 540, "Layers and Artboards panels", "")
    s.text(650, 24, "Layers and Artboards", "h", "middle")
    p = P(s, 20, 44, 360, "Layers")
    p.icons(8, ["layer-add", "layer-sublayer", "layer-delete"], 24); p.row(32)
    top = p.y
    rows = [(0, "layer", "Layer 2", "#FF4F4F", True, False, False), (1, "object", "<Rounded Rectangle>", "#FF4F4F", False, True, False), (1, "group", "<Group>", "#FF4F4F", False, False, False), (2, "object", "<Polygon>", "#FF4F4F", False, False, True),
            (1, "object", "Sketch demo", "#FF4F4F", False, False, False), (1, "layer", "Sublayer", "#FF4F4F", False, False, False), (0, "layer", "Layer 1", "#4F80FF", False, False, False), (1, "object", "<Path>", "#4F80FF", False, False, False)]
    for i, (depth, kind, name, col, active, selected, locked) in enumerate(rows):
        y = top + i * 22
        if active:
            s.add('<rect x="%s" y="%s" width="360" height="22" fill="#cfe0f7"/>' % (f(p.x), f(y)))
        if selected:
            s.add('<rect x="%s" y="%s" width="360" height="22" fill="%s" opacity="0.2"/>' % (f(p.x), f(y), col))
        s.add('<rect x="%s" y="%s" width="3" height="22" fill="%s"/>' % (f(p.x), f(y), col))
        s.icon("eye", p.x + 8, y + 4, 14)
        s.icon("lock", p.x + 26, y + 4, 14) if locked else s.text(p.x + 32, y + 15, "-", "s")
        x = p.x + 46 + depth * 14
        if kind == "layer" or kind == "group":
            s.add('<path d="M%s %s l7 0 l-3.5 5z" fill="%s"/>' % (f(x), f(y + 8), INK))
        if kind == "object":
            s.add('<rect x="%s" y="%s" width="10" height="10" fill="%s" stroke="#666"/>' % (f(x + 12), f(y + 6), ["#e8452c", "none", "#6a4fc7", "#bbb", "#ddd"][i % 5] if i % 5 != 1 else "#c0392b"))
        elif kind == "group":
            s.add('<rect x="%s" y="%s" width="7" height="7" fill="none" stroke="#666"/><rect x="%s" y="%s" width="7" height="7" fill="none" stroke="#666"/>' % (f(x + 12), f(y + 8), f(x + 15), f(y + 5)))
        s.text(x + (28 if kind != "layer" else 14), y + 15, name, "s")
        s.add('<rect x="%s" y="%s" width="10" height="10" fill="%s" stroke="%s"/>' % (f(p.x + 336), f(y + 6), col if (selected or locked and False) else "none", col))
    p.y = top + len(rows) * 22 + 8
    p.label(8, "Name"); p.field(48, 300, "Layer 2"); p.row(28)
    p.label(8, "Color"); p.drop(48, 300, "Red"); p.row(28); e1 = p.end()
    s.callout(1, 396, top + 11); s.callout(2, 396, top + 33); s.callout(3, 396, top + 55); s.callout(4, 396, top + 143)
    p = P(s, 20, e1 + 18, 360, "Artboards")
    p.icons(8, ["layer-add", "layer-delete"], 24); p.drop(70, 280, "Artboard 1"); p.row(30)
    p.label(8, "Name"); p.field(48, 300, "Artboard 1"); p.row(28)
    p.label(8, "X"); p.field(26, 70, "0"); p.label(110, "Y"); p.field(128, 70, "0"); p.row(28)
    p.label(8, "W"); p.field(26, 70, "612"); p.label(110, "H"); p.field(128, 70, "792"); p.row(28); p.end()
    legend(s, 420, 70, [(1, "Layer row: eye (show / hide), lock, triangle (open / close), name, selection square (click: move the selection into the layer)"),
                        (2, "Object row: the swatch is the object's fill; a tinted row is selected"), (3, "Group row: closed until opened; eye and lock act on every member"),
                        (4, "The colored strip is the layer's color: it also colors the selection frame of its objects")], gap=34)
    s.text(420, 240, "Working in the tree", "t b")
    tips = ["Click a layer: it becomes the active one (new shapes go there); Ctrl-click also selects its objects.",
            "Click an object or group row to select it (Shift adds, Ctrl toggles); the name field then renames it.",
            "Drag a row onto a layer to move it there, or just above / below another row to reorder it.",
            "Drag a layer onto another layer's middle to make it a sublayer, onto an edge to reorder.",
            "The mouse wheel scrolls a long tree; the Layer menu has Duplicate, Merge, Flatten and more.",
            "Artboards panel: pick one to make it active; Align, Image Trace and export use the active one."]
    for i, t in enumerate(tips):
        s.text(420, 262 + i * 20, "- " + t, "s")
    s.save("panels-layers-artboards")


if __name__ == "__main__":
    panels_1()
    panels_2()
    panels_3()
    panels_4()

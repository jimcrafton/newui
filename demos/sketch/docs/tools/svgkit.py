"""A tiny SVG builder for the sketch documentation diagrams (no dependencies).

Every diagram is a self-contained .svg: a white background, one embedded <style>, text in a system sans-serif font, and the
application's own icons inlined (not linked), so it renders the same in a browser, in an <img> tag and on GitHub.
"""
import math
import os
import re
from xml.sax.saxutils import escape

HERE = os.path.dirname(os.path.abspath(__file__))
ICONS = os.path.normpath(os.path.join(HERE, "..", "..", "Resources", "icons"))
OUT = os.path.normpath(os.path.join(HERE, "..", "diagrams"))

INK = "#222222"
DIM = "#666666"
ACCENT = "#1e6fe0"
SOFT = "#e8f0fc"
PANEL = "#f4f4f4"
EDGE = "#9a9a9a"
RED = "#d4380d"
GREEN = "#2f7d4f"
AMBER = "#f2b824"

STYLE = """
text{font-family:'Segoe UI',Arial,Helvetica,sans-serif;font-size:12px;fill:%s}
.t{font-size:12px}.s{font-size:10.5px;fill:%s}.b{font-weight:600}.h{font-size:15px;font-weight:700}.m{font-family:Consolas,'Courier New',monospace;font-size:11px;white-space:pre}
.c{text-anchor:middle}.e{text-anchor:end}
.box{fill:#fff;stroke:%s;stroke-width:1.2}.soft{fill:%s;stroke:%s;stroke-width:1.2}.panel{fill:%s;stroke:%s;stroke-width:1}
.line{stroke:%s;stroke-width:1.4;fill:none}.thin{stroke:%s;stroke-width:1;fill:none}.acc{stroke:%s;stroke-width:1.8;fill:none}
.dash{stroke-dasharray:5 4}.field{fill:#fff;stroke:#b5b5b5;stroke-width:1}
""" % (INK, DIM, EDGE, SOFT, ACCENT, PANEL, EDGE, INK, EDGE, ACCENT)


def f(n):
    return ("%.2f" % n).rstrip("0").rstrip(".")


class Svg:
    def __init__(self, width, height, title="", desc=""):
        self.w, self.h = width, height
        self.parts = []
        self.title, self.desc = title, desc
        self.defs = ['<marker id="arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
                     '<path d="M0 0L10 5L0 10z" fill="%s"/></marker>' % INK,
                     '<marker id="arrowAcc" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">'
                     '<path d="M0 0L10 5L0 10z" fill="%s"/></marker>' % ACCENT]

    # ---- primitives -------------------------------------------------------------------------------------------
    def add(self, markup):
        self.parts.append(markup)

    def rect(self, x, y, w, h, cls="box", r=0, extra=""):
        self.add('<rect x="%s" y="%s" width="%s" height="%s" rx="%s" class="%s" %s/>' % (f(x), f(y), f(w), f(h), f(r), cls, extra))

    def text(self, x, y, s, cls="t", anchor=None, extra=""):
        c = cls + ({"middle": " c", "end": " e"}.get(anchor, ""))
        self.add('<text x="%s" y="%s" class="%s" %s>%s</text>' % (f(x), f(y), c, extra, escape(s)))

    def lines(self, x, y, rows, cls="t", gap=15, anchor=None):
        for i, row in enumerate(rows):
            self.text(x, y + i * gap, row, cls, anchor)

    def line(self, x1, y1, x2, y2, cls="line", extra=""):
        self.add('<line x1="%s" y1="%s" x2="%s" y2="%s" class="%s" %s/>' % (f(x1), f(y1), f(x2), f(y2), cls, extra))

    def arrow(self, x1, y1, x2, y2, acc=False, dash=False, both=False):
        cls = "acc" if acc else "line"
        marker = "arrowAcc" if acc else "arrow"
        ends = ' marker-end="url(#%s)"' % marker + (' marker-start="url(#%s)"' % marker if both else "")
        self.add('<line x1="%s" y1="%s" x2="%s" y2="%s" class="%s%s"%s/>' % (f(x1), f(y1), f(x2), f(y2), cls, " dash" if dash else "", ends))

    def path(self, d, cls="line", extra=""):
        self.add('<path d="%s" class="%s" %s/>' % (d, cls, extra))

    def curve_arrow(self, d, acc=False):
        marker = "arrowAcc" if acc else "arrow"
        self.add('<path d="%s" class="%s" marker-end="url(#%s)"/>' % (d, "acc" if acc else "line", marker))

    def circle(self, x, y, r, cls="box", extra=""):
        self.add('<circle cx="%s" cy="%s" r="%s" class="%s" %s/>' % (f(x), f(y), f(r), cls, extra))

    def dot(self, x, y, r=3.2, fill=INK):
        self.add('<circle cx="%s" cy="%s" r="%s" fill="%s"/>' % (f(x), f(y), f(r), fill))

    def callout(self, n, x, y):
        """A numbered marker; the legend explains the numbers."""
        self.add('<circle cx="%s" cy="%s" r="9.5" fill="%s"/><text x="%s" y="%s" class="c b" style="fill:#fff;font-size:11px">%s</text>' % (f(x), f(y), RED, f(x), f(y + 4), n))

    def box_label(self, x, y, w, h, title, sub=None, cls="box", r=6):
        self.rect(x, y, w, h, cls, r)
        if sub:
            rows = sub.split(chr(10))
            top = y + h / 2 - 3 - (len(rows) - 1) * 6
            self.text(x + w / 2, top, title, "t b", "middle")
            for i, row in enumerate(rows):
                self.text(x + w / 2, top + 15 + i * 12, row, "s", "middle")
        else:
            self.text(x + w / 2, y + h / 2 + 4, title, "t b", "middle")

    def icon(self, name, x, y, size=22):
        """Inlines the application's icon `name` (Resources/icons/<name>.svg) at x, y."""
        path = os.path.join(ICONS, name + ".svg")
        with open(path, encoding="utf-8") as fh:
            text = fh.read()
        root = re.search(r"<svg([^>]*)>", text)
        attrs = root.group(1)
        inner = text[root.end():text.rindex("</svg>")]
        box = re.search(r'viewBox="([^"]+)"', attrs)
        vx, vy, vw, vh = [float(v) for v in box.group(1).split()] if box else (0, 0, 24, 24)
        keep = " ".join(m.group(0) for m in re.finditer(r'(fill|stroke|stroke-width|stroke-linecap|stroke-linejoin)="[^"]*"', attrs))
        scale = size / max(vw, vh)
        self.add('<g transform="translate(%s %s) scale(%s) translate(%s %s)" %s>%s</g>' % (f(x), f(y), f(scale), f(-vx), f(-vy), keep, inner))

    # ---- output -----------------------------------------------------------------------------------------------
    def save(self, name):
        os.makedirs(OUT, exist_ok=True)
        head = '<svg xmlns="http://www.w3.org/2000/svg" width="%s" height="%s" viewBox="0 0 %s %s" role="img"' % (f(self.w), f(self.h), f(self.w), f(self.h))
        if self.title:
            head += ' aria-label="%s"' % escape(self.title, {'"': "&quot;"})
        head += ">\n"
        body = head
        if self.title:
            body += "<title>%s</title>\n" % escape(self.title)
        if self.desc:
            body += "<desc>%s</desc>\n" % escape(self.desc)
        body += "<defs><style>%s</style>%s</defs>\n" % (STYLE, "".join(self.defs))
        body += '<rect width="100%" height="100%" fill="#ffffff"/>\n'
        body += "\n".join(self.parts) + "\n</svg>\n"
        with open(os.path.join(OUT, name + ".svg"), "w", encoding="utf-8", newline="\n") as fh:
            fh.write(body)
        print("wrote", name + ".svg")


def legend(svg, x, y, items, width=None, gap=18):
    """A numbered legend: [(n, 'text'), ...] one per row."""
    for i, (n, label) in enumerate(items):
        svg.callout(n, x + 10, y + i * gap)
        svg.text(x + 26, y + i * gap + 4, label, "t")


def bezier_points(p0, p1, p2, p3, n=24):
    pts = []
    for i in range(n + 1):
        t = i / n
        u = 1 - t
        pts.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                    u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))
    return pts


def poly(points, close=False):
    d = "M" + " L".join("%s %s" % (f(x), f(y)) for x, y in points)
    return d + (" Z" if close else "")

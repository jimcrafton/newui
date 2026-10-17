"""User-guide diagrams, part E: clipping masks in depth. Run: python gen_user_diagrams_e.py"""
from svgkit import *

SKY = "#8ec5f2"
HILL1 = "#3f8f5a"
HILL2 = "#2b6b43"
MASK_FILL = "#fbe3df"


def photo(s, x, y, w, h, uid, clip=None):
    """A little landscape standing in for a picture (sky, sun, two hills); `clip` is a clip-path id to draw it through."""
    s.defs.append('<linearGradient id="%s" x1="0" y1="0" x2="0" y2="1"><stop offset="0" stop-color="#5aa9ee"/><stop offset="1" stop-color="#cfe9fb"/></linearGradient>' % uid)
    g = '<g%s>' % (' clip-path="url(#%s)"' % clip if clip else "")
    g += '<rect x="%s" y="%s" width="%s" height="%s" fill="url(#%s)"/>' % (f(x), f(y), f(w), f(h), uid)
    g += '<circle cx="%s" cy="%s" r="%s" fill="#ffd45e"/>' % (f(x + w * 0.74), f(y + h * 0.27), f(h * 0.12))
    g += '<path d="M%s %s Q%s %s %s %s T%s %s L%s %s L%s %s Z" fill="%s"/>' % (
        f(x), f(y + h * 0.72), f(x + w * 0.2), f(y + h * 0.42), f(x + w * 0.45), f(y + h * 0.68), f(x + w * 0.85), f(y + h * 0.62),
        f(x + w), f(y + h), f(x), f(y + h), HILL1)
    g += '<path d="M%s %s Q%s %s %s %s T%s %s L%s %s L%s %s Z" fill="%s"/>' % (
        f(x + w * 0.3), f(y + h), f(x + w * 0.55), f(y + h * 0.66), f(x + w * 0.8), f(y + h * 0.84), f(x + w), f(y + h * 0.8),
        f(x + w), f(y + h), f(x + w * 0.3), f(y + h), HILL2)
    s.add(g + '</g>')


def stripes(s, x, y, w, h, clip=None, colors=("#e8472b", "#f2b824", "#1e6fe0", "#2f7d4f"), step=14):
    own = "box%d" % len(s.defs)
    s.defs.append('<clipPath id="%s"><rect x="%s" y="%s" width="%s" height="%s"/></clipPath>' % (own, f(x), f(y), f(w), f(h)))
    g = '<g clip-path="url(#%s)"><g%s>' % (own, ' clip-path="url(#%s)"' % clip if clip else "")
    n = int(w // step) + 2
    for i in range(n):
        g += '<rect x="%s" y="%s" width="%s" height="%s" fill="%s"/>' % (f(x + i * step), f(y), f(step), f(h), colors[i % len(colors)])
    s.add(g + '</g></g>')


def clip_def(s, cid, shape_markup):
    s.defs.append('<clipPath id="%s">%s</clipPath>' % (cid, shape_markup))


def handles(s, x0, y0, x1, y1):
    s.add('<rect x="%s" y="%s" width="%s" height="%s" fill="none" stroke="%s" stroke-width="1.2"/>' % (f(x0), f(y0), f(x1 - x0), f(y1 - y0), ACCENT))
    for hx in (x0, (x0 + x1) / 2, x1):
        for hy in (y0, (y0 + y1) / 2, y1):
            if (hx, hy) != ((x0 + x1) / 2, (y0 + y1) / 2):
                s.add('<rect x="%s" y="%s" width="6" height="6" fill="#fff" stroke="%s" stroke-width="1.2"/>' % (f(hx - 3), f(hy - 3), ACCENT))


def mask_steps():
    s = Svg(960, 330, "Making a clipping mask", "Stack the mask on top, select both, make the mask; release puts everything back.")
    xs = [20, 255, 490, 725]
    titles = ["1  Stack", "2  Select both", "3  Make", "Release"]
    notes = [["Draw the artwork, then put the", "shape that should show through", "ON TOP of it."],
             ["Select the artwork and the shape", "(drag a box round them, or", "Shift+click each). The mask's own", "colors stop showing in step 3."],
             ["Object > Clipping Mask > Make", "(Ctrl+7). The artwork now shows", "only inside the shape."],
             ["Object > Clipping Mask > Release", "(Ctrl+Shift+7): the shape and the", "artwork are separate again."]]
    for i, x in enumerate(xs):
        s.rect(x, 20, 215, 292, "panel", 8)
        s.text(x + 14, 44, titles[i], "h")
        s.lines(x + 14, 232, notes[i], "t", 15)
    # the artwork rectangle at (x+22, 70) 170 x 120; the mask ellipse centred at (x+107, 130)
    for i, x in enumerate(xs):
        ax, ay, aw, ah = x + 22, 70, 170, 120
        if i in (0, 1):
            stripes(s, ax, ay, aw, ah)
            s.add('<ellipse cx="%s" cy="%s" rx="62" ry="42" fill="%s" fill-opacity="0.85" stroke="%s" stroke-width="2"/>' % (f(x + 107), 130, MASK_FILL, RED))
            if i == 1:
                handles(s, ax - 2, ay - 2, ax + aw + 2, ay + ah + 2)
        else:
            clip_def(s, "mk%d" % i, '<ellipse cx="%s" cy="130" rx="62" ry="42"/>' % f(x + 107))
            if i == 2:
                stripes(s, ax, ay, aw, ah, "mk%d" % i)
                s.add('<ellipse cx="%s" cy="130" rx="62" ry="42" fill="none" stroke="%s" stroke-width="1.2" stroke-dasharray="4 3"/>' % (f(x + 107), ACCENT))
                s.text(x + 107, 205, "(faint) outline of the mask", "s", "middle")
            else:
                stripes(s, ax, ay, aw, ah)
                s.add('<ellipse cx="%s" cy="130" rx="62" ry="42" fill="%s" fill-opacity="0.85" stroke="%s" stroke-width="2"/>' % (f(x + 107), MASK_FILL, RED))
    for i in range(3):
        s.arrow(xs[i] + 217, 130, xs[i + 1] - 2, 130, True)
    s.save("mask-steps")


def mask_examples():
    s = Svg(960, 620, "Clipping mask examples", "Four uses: a picture in a shape, a picture or pattern seen through letters, trimming overflow, and rounding a block's corners.")
    cards = [(20, 20, "A  Crop a picture to a shape", "Crop a photo to a circle, a badge or any outline. The photo is not cut: release the mask to get it all back."),
             (490, 20, "B  Text as a window", "Make the words the mask: the stripes (or a photo, a gradient) show only through the letters."),
             (20, 330, "C  Trim what spills over", "A pattern or drawing that runs past the edge of a card, banner or artboard: put a rectangle on top and mask."),
             (490, 330, "D  Rounded or shaped corners", "A gradient header, a bar chart or a swatch row takes on the corners of the shape that masks it.")]
    for x, y, title, why in cards:
        s.rect(x, y, 450, 280, "panel", 8)
        s.text(x + 14, y + 26, title, "h")
        words = why.split(" ")
        line, rows = "", []
        for w in words:
            if len(line) + len(w) > 62:
                rows.append(line)
                line = w
            else:
                line = (line + " " + w).strip()
        rows.append(line)
        s.lines(x + 14, y + 238, rows, "s", 13)

    # A: photo in a circle
    photo(s, 40, 70, 150, 110, "skyA")
    s.text(115, 198, "before", "s", "middle")
    s.arrow(200, 125, 245, 125, True)
    s.text(222, 112, "mask", "s", "middle")
    clip_def(s, "cA", '<circle cx="330" cy="125" r="56"/>')
    photo(s, 255, 65, 150, 110, "skyA2", "cA")
    s.add('<circle cx="330" cy="125" r="56" fill="none" stroke="%s" stroke-width="3"/>' % INK)
    s.text(330, 198, "after (circle on top, then Make)", "s", "middle")

    # B: stripes through text
    word = 'style="font-family:Arial Black,Arial,sans-serif;font-weight:900;font-size:54px;text-anchor:middle;%s"'
    stripes(s, 520, 62, 390, 58, step=11)
    s.add('<text x="715" y="108" %s>SKETCH</text>' % (word % ("fill:#fff;stroke:%s;stroke-width:2" % RED)))
    s.text(512, 138, "before: the word sits on top of the stripes", "s")
    clip_def(s, "cB", '<text x="715" y="205" %s>SKETCH</text>' % (word % ""))
    stripes(s, 520, 155, 390, 70, "cB", step=11)
    s.text(512, 232, "after: Make, and the stripes show only in the letters", "s")

    # C: spill over a card: before / after
    clip_def(s, "cCv1", '<rect x="40" y="365" width="190" height="150"/>')
    clip_def(s, "cCv2", '<rect x="265" y="365" width="190" height="150"/>')
    clip_def(s, "cC", '<rect x="337" y="390" width="120" height="95"/>')
    for ox, view, inner in ((0, "cCv1", None), (225, "cCv2", "cC")):
        g = '<g clip-path="url(#%s)">' % view
        if inner:
            g += '<g clip-path="url(#%s)">' % inner
        for i in range(6):
            for j in range(5):
                g += '<circle cx="%s" cy="%s" r="20" fill="%s" fill-opacity="0.8"/>' % (f(52 + ox + i * 38), f(380 + j * 38), ["#e8472b", "#f2b824", "#1e6fe0"][(i + j) % 3])
        if inner:
            g += '</g>'
        s.add(g + '</g>')
    s.add('<rect x="112" y="390" width="120" height="95" fill="none" stroke="%s" stroke-width="1.4" stroke-dasharray="5 4"/>' % ACCENT)
    s.add('<rect x="337" y="390" width="120" height="95" fill="none" stroke="%s" stroke-width="1.2" stroke-dasharray="4 3"/>' % ACCENT)
    s.text(135, 532, "before: runs past the card (dashed)", "s", "middle")
    s.text(360, 532, "after: cut to the card", "s", "middle")

    # D: header band, before and after
    clip_def(s, "cD", '<rect x="530" y="385" width="170" height="110" rx="22"/>')
    s.defs.append('<linearGradient id="gD" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#1e6fe0"/><stop offset="1" stop-color="#8a3fd1"/></linearGradient>')
    s.add('<rect x="510" y="375" width="210" height="130" fill="url(#gD)" clip-path="url(#cD)"/>')
    s.add('<rect x="530" y="385" width="170" height="110" rx="22" fill="none" stroke="%s" stroke-width="1.2" stroke-dasharray="4 3"/>' % ACCENT)
    s.text(615, 514, "a plain rectangle, masked by a rounded one", "s", "middle")
    s.add('<rect x="745" y="385" width="170" height="110" fill="url(#gD)"/>')
    s.text(830, 514, "without the mask", "s", "middle")
    s.save("mask-examples")


def mask_rules():
    s = Svg(960, 330, "What a clipping mask does and does not do", "The mask's own paint is ignored, edges are hard, holes cut holes, and masks can be nested.")
    cards = [("Its own look is dropped", "Fill, stroke and effects of the mask shape are not drawn. Only its outline counts."),
             ("Hard edges", "Inside is fully shown, outside is fully hidden. For a soft edge, blur or fade the artwork itself instead."),
             ("A hole stays a hole", "A mask with a hole (a ring made with Pathfinder > Minus Front) shows the artwork only in the ring."),
             ("Masks inside masks", "Select a masked group and another shape on top, and Make again: both cuts apply.")]
    xs = [20, 255, 490, 725]
    for i, (t, d) in enumerate(cards):
        x = xs[i]
        s.rect(x, 20, 215, 292, "panel", 8)
        s.text(x + 14, 44, t, "h")
        words, line, rows = d.split(" "), "", []
        for w in words:
            if len(line) + len(w) > 31:
                rows.append(line)
                line = w
            else:
                line = (line + " " + w).strip()
        rows.append(line)
        s.lines(x + 14, 228, rows, "t", 15)
    # 1: the mask had a loud look; the result shows none of it
    clip_def(s, "r1", '<rect x="40" y="85" width="175" height="95" rx="30"/>')
    stripes(s, 30, 70, 195, 125, "r1")
    s.text(127, 205, "mask had a red fill and a thick stroke", "s", "middle")
    # 2: hard edge, hidden part shown as a ghost
    s.add('<g opacity="0.16">')
    stripes(s, 270, 70, 185, 125)
    s.add('</g>')
    clip_def(s, "r2", '<circle cx="362" cy="132" r="48"/>')
    stripes(s, 270, 70, 185, 125, "r2")
    s.text(362, 205, "(the grey part is hidden, not deleted)", "s", "middle")
    # 3: a ring
    s.defs.append('<clipPath id="r3"><path clip-rule="evenodd" d="M547 132 a50 50 0 1 0 100 0 a50 50 0 1 0 -100 0 Z M573 132 a24 24 0 1 0 48 0 a24 24 0 1 0 -48 0 Z"/></clipPath>')
    photo(s, 505, 70, 185, 125, "skyR3", "r3")
    s.text(597, 205, "(the middle is empty)", "s", "middle")
    # 4: two cuts
    clip_def(s, "r4a", '<rect x="800" y="80" width="105" height="105" rx="10"/>')
    clip_def(s, "r4b", '<ellipse cx="832" cy="132" rx="64" ry="48"/>')
    s.add('<g clip-path="url(#r4a)"><g clip-path="url(#r4b)">')
    stripes(s, 760, 70, 160, 125, step=12)
    s.add('</g></g>')
    s.add('<ellipse cx="832" cy="132" rx="64" ry="48" fill="none" stroke="%s" stroke-width="1" stroke-dasharray="4 3"/>' % ACCENT)
    s.add('<rect x="800" y="80" width="105" height="105" rx="10" fill="none" stroke="%s" stroke-width="1" stroke-dasharray="4 3"/>' % ACCENT)
    s.text(832, 205, "only the overlap of both is left", "s", "middle")
    s.save("mask-rules")


if __name__ == "__main__":
    mask_steps()
    mask_examples()
    mask_rules()

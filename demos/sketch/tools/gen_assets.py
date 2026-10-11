"""Generates sketch's icon and cursor art as plain .svg files.

    python gen_assets.py            (run from anywhere; writes under ../Resources)

Output:
    Resources/icons/<name>.svg      24x24 toolbar/tool icons
    Resources/cursors/<name>.svg    32x32 cursors
    Resources/cursors/cursors.json  hotspot (in 32x32 pixels) for each cursor

Everything is written with plain presentation attributes and explicit colors (no CSS, no
currentColor) because newui rasterizes these through svgandme (see renderSvgFile(), svgimage.h),
which is the safest subset. A cursor's hotspot is not part of an SVG, so it is recorded in
cursors.json and passed to Cursor::loadPath(path, hotspotX, hotspotY, 32) by the app.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "Resources")

INK = "#3a3a3a"       # icon strokes
ACCENT = "#0078d4"    # "this tool does X to the selection" highlights

# ---------------------------------------------------------------------------------------------
# Icons: (name, inner markup). Drawn on a 24x24 grid, 1.5px round strokes, no fill unless noted.
# ---------------------------------------------------------------------------------------------
ICONS = {
    # file / edit
    "new": '<path d="M6 3h8l4 4v14H6z"/><path d="M14 3v4h4"/>',
    "open": '<path d="M3 7h6l2 2h10v10H3z"/>',
    "save": '<path d="M5 4h12l3 3v13H5z"/><path d="M8 4v6h8V4M8 20v-6h8v6"/>',
    "undo": '<path d="M9 7 4 12l5 5"/><path d="M4 12h10a6 6 0 0 1 0 8"/>',
    "redo": '<path d="m15 7 5 5-5 5"/><path d="M20 12H10a6 6 0 0 0 0 8"/>',
    "cut": '<circle cx="7" cy="17" r="3"/><circle cx="17" cy="17" r="3"/><path d="M9 15 18 4M15 15 6 4"/>',
    "copy": '<rect x="8" y="8" width="12" height="12" rx="1"/><path d="M16 8V4H4v12h4"/>',
    "paste": '<rect x="5" y="5" width="14" height="16" rx="1"/><path d="M9 5V3h6v2"/>',
    "delete": '<path d="M5 7h14M10 7V4h4v3M7 7l1 13h8l1-13"/>',
    # object
    "group": '<rect x="3" y="3" width="10" height="10" stroke-dasharray="2 2"/><rect x="11" y="11" width="10" height="10"/>',
    "ungroup": '<rect x="3" y="3" width="8" height="8"/><rect x="13" y="13" width="8" height="8"/>',
    "bring-to-front": '<rect x="4" y="9" width="11" height="11"/><rect x="9" y="4" width="11" height="11" fill="%s" fill-opacity="0.25"/>' % INK,
    "send-to-back": '<rect x="9" y="4" width="11" height="11"/><rect x="4" y="9" width="11" height="11" fill="%s" fill-opacity="0.25"/>' % INK,
    "align-left": '<path d="M4 3v18"/><rect x="7" y="6" width="12" height="4"/><rect x="7" y="14" width="7" height="4"/>',
    "align-center": '<path d="M12 3v18"/><rect x="5" y="6" width="14" height="4"/><rect x="8" y="14" width="8" height="4"/>',
    "align-right": '<path d="M20 3v18"/><rect x="5" y="6" width="12" height="4"/><rect x="10" y="14" width="7" height="4"/>',
    "snap-to-grid": '<path d="M4 4h16v16H4zM4 12h16M12 4v16"/>',
    # tools
    # fill types / gradient
    "fill-none": '<rect x="4" y="4" width="16" height="16"/><path d="M4 20 20 4" stroke="#c0392b"/>',
    "fill-solid": '<rect x="4" y="4" width="16" height="16" fill="%s"/>' % INK,
    "fill-linear": '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="%s"/></linearGradient></defs><rect x="4" y="4" width="16" height="16" fill="url(#g)"/>' % INK,
    "fill-radial": '<defs><radialGradient id="g" cx="0.5" cy="0.5" r="0.5"><stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="%s"/></radialGradient></defs><rect x="4" y="4" width="16" height="16" fill="url(#g)"/>' % INK,
    # tools - solid glyphs in the style of a classic vector-editor toolbox (filled forms, one ink
    # color, a hollow variant for the "direct" tool). Original artwork; not copied from any product.
    "tool-select": '<path d="M6 3v17l4.2-3.9 2.9 6.1 2.6-1.2-2.9-6H19z" fill="%(i)s" stroke="none"/>' % {"i": INK},
    "tool-direct-select": '<path d="M6 3v17l4.2-3.9 2.9 6.1 2.6-1.2-2.9-6H19z" fill="#ffffff" stroke="%(i)s" stroke-width="1.6"/>' % {"i": INK},
    "tool-rectangle": '<rect x="3" y="6" width="18" height="12" rx="1" fill="%(i)s" stroke="none"/>' % {"i": INK},
    "tool-ellipse": '<ellipse cx="12" cy="12" rx="9" ry="6.5" fill="%(i)s" stroke="none"/>' % {"i": INK},
    "tool-line": '<path d="M5 19 19 5" stroke-width="2.4"/><circle cx="5" cy="19" r="1.8" fill="%(i)s" stroke="none"/><circle cx="19" cy="5" r="1.8" fill="%(i)s" stroke="none"/>' % {"i": INK},
    "tool-pen": '<path d="M12 2.5 17.5 10 14.8 15.5H9.2L6.5 10z" fill="%(i)s" stroke="none"/><path d="M12 9v6" stroke="#ffffff" stroke-width="1.4"/><circle cx="12" cy="8.2" r="1.3" fill="#ffffff" stroke="none"/><rect x="8" y="17.5" width="8" height="3.5" rx="1" fill="%(i)s" stroke="none"/>' % {"i": INK},
    "tool-pencil": '<path d="M4 20 5.2 15.2 16.6 3.8a1.8 1.8 0 0 1 2.6 0l1 1a1.8 1.8 0 0 1 0 2.6L8.8 18.8z" fill="%(i)s" stroke="none"/><path d="M14.5 6 18 9.5" stroke="#ffffff" stroke-width="1.3"/>' % {"i": INK},
    "tool-text": '<path d="M5 4h14v4.2h-1.8V6H13v12h2.6V20H8.4v-2H11V6H6.8v2.2H5z" fill="%(i)s" stroke="none"/>' % {"i": INK},
    "tool-hand": '<path d="M8 12.5V5.2a1.5 1.5 0 0 1 3 0V11m0-1V4.2a1.5 1.5 0 0 1 3 0V10m0-3.2a1.5 1.5 0 0 1 3 0V16a6.2 6.2 0 0 1-6.2 6.2h-.8a6 6 0 0 1-5-2.7l-3-4.8a1.5 1.5 0 0 1 2.5-1.6L8 15.3z" fill="%(i)s" stroke="none"/>' % {"i": INK},
    "tool-zoom": '<circle cx="10" cy="10" r="6.2" fill="none" stroke-width="2.2"/><path d="m15 15 6 6" stroke-width="3"/><path d="M10 7.2v5.6M7.2 10h5.6" stroke-width="1.6"/>',
    "tool-gradient": '<defs><linearGradient id="g" x1="0" y1="0" x2="1" y2="0"><stop offset="0" stop-color="#ffffff"/><stop offset="1" stop-color="%s"/></linearGradient></defs><rect x="3" y="7" width="18" height="10" fill="url(#g)"/><path d="M6 20h12M15 18l3 2-3 2"/>' % INK,
    "stop-add": '<path d="M12 5v14M5 12h14"/>',
    "stop-delete": '<path d="M6 12h12"/>',
    "gradient-reverse": '<path d="M4 8h14M14 4l4 4-4 4M20 16H6M10 12l-4 4 4 4"/>',
    # appearance panel
    "fx": '<path d="M4 18 10 6M4 6l6 12"/><path d="M13 6h7M13 12h5M13 6v12"/>',
    "new-fill": '<rect x="4" y="4" width="16" height="16" fill="%s"/><path d="M12 8v8M8 12h8" stroke="#ffffff"/>' % INK,
    "new-stroke": '<rect x="4" y="4" width="16" height="16" stroke-width="3"/><path d="M12 9v6M9 12h6"/>',
    "clear-appearance": '<rect x="4" y="4" width="16" height="16"/><path d="M4 4 20 20"/>',
    "duplicate-item": '<rect x="4" y="4" width="10" height="10"/><rect x="10" y="10" width="10" height="10"/>',
    "drag-handle": '<path d="M9 6h.01M15 6h.01M9 12h.01M15 12h.01M9 18h.01M15 18h.01" stroke-width="2.5"/>',
    "graphic-style-new": '<rect x="4" y="4" width="16" height="16" rx="2"/><path d="M12 8v8M8 12h8"/>',
    "align-stroke-center": '<rect x="6" y="6" width="12" height="12" stroke-dasharray="1 2" stroke-width="1"/><rect x="4" y="4" width="16" height="16" stroke-width="3" stroke-opacity="0.5"/>',
    "align-stroke-inside": '<rect x="4" y="4" width="16" height="16" stroke-width="1"/><rect x="5" y="5" width="14" height="14" stroke-width="3" stroke-opacity="0.5"/>',
    "align-stroke-outside": '<rect x="6" y="6" width="12" height="12" stroke-width="1"/><rect x="4" y="4" width="16" height="16" stroke-width="3" stroke-opacity="0.5"/>',
    "cap-butt": '<path d="M5 12h12" stroke-width="7" stroke-linecap="butt"/>',
    "cap-round": '<path d="M7 12h10" stroke-width="7" stroke-linecap="round"/>',
    "cap-square": '<path d="M6 12h10" stroke-width="7" stroke-linecap="square"/>',
    "join-miter": '<path d="M5 19V8h11" stroke-width="4" stroke-linejoin="miter" stroke-linecap="butt"/>',
    "join-round": '<path d="M5 19V8h11" stroke-width="4" stroke-linejoin="round" stroke-linecap="butt"/>',
    "join-bevel": '<path d="M5 19V8h11" stroke-width="4" stroke-linejoin="bevel" stroke-linecap="butt"/>',
    "arrow-none": '<path d="M4 12h16"/>',
    "arrow-simple": '<path d="M4 12h16M14 7l6 5-6 5"/>',
    "arrow-filled": '<path d="M4 12h10"/><path d="M13 7l7 5-7 5z" fill="%s"/>' % INK,
    "arrow-dot": '<path d="M4 12h12"/><circle cx="18" cy="12" r="2.5" fill="%s"/>' % INK,
    "profile-uniform": '<path d="M3 9h18v6H3z"/>',
    "profile-taper": '<path d="M3 9c6 0 12 2 18 3-6 1-12 3-18 3z"/>',
    "profile-bulge": '<path d="M3 12c4-6 14-6 18 0-4 6-14 6-18 0z"/>',
    # new-document dialog
    "orientation-portrait": '<rect x="7" y="3" width="10" height="18" rx="1"/>',
    "orientation-landscape": '<rect x="3" y="7" width="18" height="10" rx="1"/>',
    "link-aspect": '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1"/>',
    "unlink-aspect": '<path d="M10 14a4 4 0 0 0 5.7 0l3-3a4 4 0 0 0-5.7-5.7l-1 1M14 10a4 4 0 0 0-5.7 0l-3 3a4 4 0 0 0 5.7 5.7l1-1M4 4l16 16"/>',
    # layers panel
    "eye": '<path d="M2 12s4-7 10-7 10 7 10 7-4 7-10 7S2 12 2 12z"/><circle cx="12" cy="12" r="3"/>',
    "lock": '<rect x="5" y="11" width="14" height="10" rx="1"/><path d="M8 11V8a4 4 0 0 1 8 0v3"/>',
    "layer-add": '<path d="M12 5v14M5 12h14"/>',
    "layer-delete": '<path d="M5 7h14M10 7V4h4v3M7 7l1 13h8l1-13"/>',
}


HALO = "#ffffff"
HALO_EXTRA = 2.0   # how much wider than the glyph's own stroke the white outline is, in px


def halo_markup(inner):
    """The white outline drawn underneath an icon: the same shapes, every paint turned white and
    every stroke widened, so the glyph keeps a clear edge on any background (dark panels, selected
    rows, busy canvases). Gradient definitions are dropped - the outline is flat white - and
    translucency/dashes are removed so the outline is solid."""
    import re
    h = re.sub(r"<defs>.*?</defs>", "", inner, flags=re.S)
    h = re.sub(r'\s(?:fill|stroke)-opacity="[^"]*"', "", h)
    h = re.sub(r'\sstroke-dasharray="[^"]*"', "", h)
    h = re.sub(r'stroke-width="([\d.]+)"', lambda m: 'stroke-width="%g"' % (float(m.group(1)) + HALO_EXTRA), h)
    h = re.sub(r'\sfill="(?!none)[^"]*"', ' fill="%s"' % HALO, h)
    h = re.sub(r'\sstroke="[^"]*"', ' stroke="%s"' % HALO, h)
    # An element with no stroke-width of its own gets the group's, widened the same amount.
    return '<g stroke="%s" stroke-width="%g">%s</g>' % (HALO, 1.5 + HALO_EXTRA, h)


def icon_svg(inner):
    return (
        '<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" '
        'fill="none" stroke="%s" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round">'
        "%s%s</svg>\n" % (INK, halo_markup(inner), inner)
    )


# ---------------------------------------------------------------------------------------------
# Cursors: 32x32. Each is drawn twice - a thick white halo underneath, then the black glyph on top -
# so it stays visible over both the dark canvas surround and white artwork. (name, markup, hotspot)
# ---------------------------------------------------------------------------------------------
def halo(glyph, fill="none"):
    """glyph is path markup using stroke only; returns white-outlined + black version."""
    return (
        '<g fill="%s" stroke="#ffffff" stroke-width="4" stroke-linejoin="round" stroke-linecap="round">%s</g>'
        '<g fill="%s" stroke="#000000" stroke-width="1.5" stroke-linejoin="round" stroke-linecap="round">%s</g>'
        % (fill, glyph, fill, glyph)
    )


ARROW = '<path d="M7 3 7 24 12.5 19 16.5 28 20 26.5 16 17.5 23 17.5z"/>'
CROSS = '<path d="M16 6v8M16 18v8M6 16h8M18 16h8"/>'

CURSORS = {
    # name: (markup, hotspotX, hotspotY)
    "select": (halo(ARROW, "#000000"), 7, 3),
    "direct-select": (halo(ARROW, "#ffffff"), 7, 3),
    "crosshair": (halo(CROSS), 16, 16),
    "pen": (halo('<path d="M6 27 9 18 22 5 27 10 14 23z"/><path d="M6 27 12 21"/>', "#ffffff") +
            '<circle cx="6" cy="27" r="1.2" fill="#000000"/>', 6, 27),
    "pencil": (halo('<path d="M5 27 7 19 21 5 27 11 13 25z"/><path d="M19 7 25 13"/>', "#ffffff"), 5, 27),
    "text": (halo('<path d="M11 6h10M11 26h10M16 6v20"/>'), 16, 16),
    "hand": (halo('<path d="M11 17V8a1.8 1.8 0 0 1 3.6 0v6m0-1V6a1.8 1.8 0 0 1 3.6 0v7m0-3a1.8 1.8 0 0 1 3.6 0v9a7 7 0 0 1-7 7h-1a7 7 0 0 1-6-3.5L6 19a1.8 1.8 0 0 1 3.2-1.6L11 19"/>', "#ffffff"), 16, 16),
    "hand-grab": (halo('<path d="M8 17a1.8 1.8 0 0 1 3.6 0 1.8 1.8 0 0 1 3.6-1 1.8 1.8 0 0 1 3.6 0 1.8 1.8 0 0 1 3.6 1.5V21a7 7 0 0 1-7 7h-1a7 7 0 0 1-6-3.5z"/>', "#ffffff"), 16, 16),
    "zoom-in": (halo('<circle cx="13" cy="13" r="8"/><path d="M19 19l8 8M13 9v8M9 13h8"/>'), 13, 13),
    "zoom-out": (halo('<circle cx="13" cy="13" r="8"/><path d="M19 19l8 8M9 13h8"/>'), 13, 13),
    "move": (halo('<path d="M16 4v24M4 16h24M16 4l-3.5 3.5M16 4l3.5 3.5M16 28l-3.5-3.5M16 28l3.5-3.5M4 16l3.5-3.5M4 16l3.5 3.5M28 16l-3.5-3.5M28 16l-3.5 3.5"/>'), 16, 16),
    "resize-ns": (halo('<path d="M16 4v24M16 4l-4 4M16 4l4 4M16 28l-4-4M16 28l4-4"/>'), 16, 16),
    "resize-we": (halo('<path d="M4 16h24M4 16l4-4M4 16l4 4M28 16l-4-4M28 16l-4 4"/>'), 16, 16),
    "resize-nwse": (halo('<path d="M7 7l18 18M7 7h6M7 7v6M25 25h-6M25 25v-6"/>'), 16, 16),
    "resize-nesw": (halo('<path d="M25 7 7 25M25 7h-6M25 7v6M7 25h6M7 25v-6"/>'), 16, 16),
    "gradient": (halo('<path d="M6 26 26 6"/><circle cx="6" cy="26" r="3"/><circle cx="26" cy="6" r="3"/>'), 6, 26),
    "rotate": (halo('<path d="M24 12a9 9 0 1 0 1 8"/><path d="M24 5v7h-7"/>'), 16, 16),
}


def cursor_svg(inner):
    return '<svg xmlns="http://www.w3.org/2000/svg" width="32" height="32" viewBox="0 0 32 32">%s</svg>\n' % inner


def main():
    icons_dir = os.path.join(OUT, "icons")
    cursors_dir = os.path.join(OUT, "cursors")
    os.makedirs(icons_dir, exist_ok=True)
    os.makedirs(cursors_dir, exist_ok=True)

    for name, inner in ICONS.items():
        with open(os.path.join(icons_dir, name + ".svg"), "w", encoding="utf-8", newline="\n") as f:
            f.write(icon_svg(inner))

    hotspots = {}
    for name, (markup, hx, hy) in CURSORS.items():
        with open(os.path.join(cursors_dir, name + ".svg"), "w", encoding="utf-8", newline="\n") as f:
            f.write(cursor_svg(markup))
        hotspots[name] = {"x": hx, "y": hy, "size": 32}
    with open(os.path.join(cursors_dir, "cursors.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(hotspots, f, indent=2)
        f.write("\n")

    print("wrote %d icons, %d cursors" % (len(ICONS), len(CURSORS)))


if __name__ == "__main__":
    main()

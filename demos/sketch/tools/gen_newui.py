"""Generates Resources/sketch.newui - sketch's whole window - from the structure of
mockup/sketch-mockup.html.

    python gen_newui.py             (writes ../Resources/sketch.newui)

The output is a JSON5 .newui document in the format described by the newui-view-json5 skill
(D:\\code\\newui\\.claude\\skills\\newui-view-json5): a Frame whose RootView holds the menu bar,
toolbars, canvas, side panels and status bar. Only properties that differ from a fresh instance are
written (the format's omission rule), except `visible: true` on every container, which a bare
SubView needs to be shown at all.

Heights of stacked rows are summed here (each helper returns a node carrying a private "_h"), so a
panel is sized to its content instead of hard-coding pixels twice. A panel's width is the sidebar's.
Keep this script and the mockup in step: a region's `data-newui` attribute in the mockup names the
control it becomes here.
"""
import os

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "..", "Resources", "sketch.newui")

SIDEBAR_W = 284          # scroll view width
PANEL_W = 268            # content width (sidebar minus the scrollbar)
BODY_PAD = 8
ROW_GAP = 6


# ---------------------------------------------------------------------------------------------
# JSON5 writer: unquoted keys, double-quoted strings, 2-space indent.
# ---------------------------------------------------------------------------------------------
def _scalar(v):
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, (int, float)):
        return repr(v) if isinstance(v, float) else str(v)
    return '"%s"' % str(v).replace("\\", "\\\\").replace('"', '\\"')


def dump(v, indent=0):
    pad = "  " * indent
    if isinstance(v, dict):
        items = [(k, x) for k, x in v.items() if not k.startswith("_")]
        if not items:
            return "{}"
        # Short leaf objects (Point/Size/Font...) stay on one line.
        if all(not isinstance(x, (dict, list)) for _, x in items) and len(items) <= 6:
            return "{ " + ", ".join("%s: %s" % (k, _scalar(x)) for k, x in items) + " }"
        lines = ["%s  %s: %s" % (pad, k, dump(x, indent + 1)) for k, x in items]
        return "{\n" + ",\n".join(lines) + "\n" + pad + "}"
    if isinstance(v, list):
        if not v:
            return "[]"
        if all(not isinstance(x, (dict, list)) for x in v):
            return "[" + ", ".join(_scalar(x) for x in v) + "]"
        lines = ["%s  %s" % (pad, dump(x, indent + 1)) for x in v]
        return "[\n" + ",\n".join(lines) + "\n" + pad + "]"
    return _scalar(v)


# ---------------------------------------------------------------------------------------------
# Node helpers
# ---------------------------------------------------------------------------------------------
def size(w, h):
    return {"type": "Size", "width": w, "height": h}


def rect(x, y, w, h):
    return {"type": "Rect", "pos": {"type": "Point", "x": x, "y": y}, "size": size(w, h)}


def node(type_, name=None, w=None, h=None, weight=None, visible=None, **props):
    """A View of `type_`. w/h set a fixed desired size (0 = let the layout decide that axis);
    weight is the FlexLayout share of leftover space along the parent's main axis."""
    n = {"type": type_}
    if name:
        n["name"] = name
    if visible is not None:
        n["visible"] = visible
    if w is not None or h is not None:
        n["desiredSize"] = size(w or 0, h or 0)
        n["desiredSizeOverride"] = True
    if weight is not None:
        n["layoutParams"] = {"type": "FlexLayoutParams", "weight": weight}
    n.update(props)
    n["_w"] = w
    n["_h"] = h
    return n


def flex(orientation, spacing=0, padding=0, cross="Stretch", main="Start"):
    lay = {"type": "FlexLayout", "orientation": orientation}
    if main != "Start":
        lay["mainAxisAlignment"] = main
    if cross != "Stretch":
        lay["crossAxisAlignment"] = cross
    if spacing:
        lay["spacing"] = spacing
    if padding:
        lay["padding"] = padding
    return lay


def vbox(name, children, spacing=0, padding=0, w=None, weight=None, cross="Stretch"):
    h = 2 * padding + sum((c["_h"] or 0) for c in children) + spacing * max(len(children) - 1, 0)
    return node("SubView", name, w=w, h=h, weight=weight, visible=True,
                layout=flex("Vertical", spacing, padding, cross), childViews=children)


def hbox(name, children, spacing=ROW_GAP, h=24, weight=None, cross="Center", padding=0):
    return node("SubView", name, h=h, weight=weight, visible=True,
                layout=flex("Horizontal", spacing, padding, cross), childViews=children)


def label(text, w=None, h=20, align="Left", name=None, bold=False, weight=None):
    style = {"type": "LabelStyle", "text": text, "textAlignment": align}
    if bold:
        style["font"] = {"type": "Font", "name": "Segoe UI", "size": 12, "bold": True}
    return node("Label", name, w=w, h=h, weight=weight, text=text, textAlignment=align, style=style)


def field(name, text, w=None, h=24, weight=None):
    return node("TextField", name, w=w, h=h, weight=weight, text=text)


def tbtn(name, icon, checked=False, toggle=False, s=28):
    extra = {}
    if toggle or checked:
        extra["toggleButton"] = True
    if checked:
        extra["checked"] = True
    return node("ToolbarButton", name, w=s, h=s, icon="icons/%s.svg" % icon, **extra)


def tsep(in_horizontal_toolbar=True):
    """A divider. ToolbarSeparator::horizontal should match the owning Toolbar's orientation (it sizes
    itself from that), so True in a horizontal toolbar and False in a vertical one."""
    # An explicit cross-axis size: the toolbars centre their children, which would otherwise
    # collapse the divider to nothing.
    # `horizontal` must be read before desiredSize: its setter resets the desired size itself.
    n = node("ToolbarSeparator", w=9 if in_horizontal_toolbar else 28, h=22 if in_horizontal_toolbar else 9)
    ordered = {"type": n.pop("type"), "horizontal": in_horizontal_toolbar}
    ordered.update(n)
    return ordered


def toolbar(name, orientation, children, w=None, h=None, weight=None):
    return node("Toolbar", name, w=w, h=h, weight=weight, orientation=orientation, visible=True,
                layout=flex(orientation, 2, 3 if orientation == "Horizontal" else 0,
                            cross="Center" if orientation == "Horizontal" else "Center"),
                childViews=children)


def dropdown(name, items, w=None, h=24, selected=0, weight=None):
    return node("DropDownList", name, w=w, h=h, weight=weight, selectedIndex=selected,
                model={"type": "StringListModel", "items": items})


def segmented(name, segments, selected=0, h=24, w=None):
    # In a row that centres its children (cross axis), a SegmentedControl with no explicit width
    # collapses to 0 - it only sizes itself to its text when the layout stretches it.
    return node("SegmentedControl", name, w=w, h=h, selectedIndex=selected, segments=segments)


def listview(name, items, h, row=22, weight=None, selected=None):
    extra = {"selectedIndex": selected} if selected is not None else {}
    return node("ListView", name, h=h, weight=weight, rowHeight=row,
                model={"type": "StringListModel", "items": items}, **extra)


def toggle(name, checked=False):
    extra = {"checked": True} if checked else {}
    return node("Toggle", name, w=20, h=20, **extra)


def slider_row(name, caption, value, colored=None):
    return hbox(name + "Row", [
        label(caption, w=16, name=name + "Caption", align="Center"),
        node("Slider", name, weight=1, h=24, value=value, integer=True, horizontal=True),
        field(name + "Value", str(value), w=44),
    ], h=24)


def color_well(name, color, w=18, h=18):
    return node("SubView", name, w=w, h=h, visible=True, style={
        "type": "ViewStyle", "borderWidth": 1,
        "backgroundFill": {"type": "Fill", "kind": "Color", "color": color}})


def spacer(weight=1):
    return node("SubView", None, weight=weight, visible=True)


def panel(name, title, body_children, spacing=ROW_GAP, header_right=None):
    header = label(title, h=22, name=name + "Header", bold=True)
    header["style"]["backgroundFill"] = {"type": "Fill", "kind": "Color", "color": "ControlBackground"}
    body = vbox(name + "Body", body_children, spacing=spacing, padding=BODY_PAD)
    return vbox(name, [header, body], w=PANEL_W)


# ---------------------------------------------------------------------------------------------
# Menus
# ---------------------------------------------------------------------------------------------
def mi(text, shortcut=None, checked=False, children=None):
    m = {"type": "MenuItem", "text": text}
    if shortcut:
        m["shortcutText"] = shortcut
    if checked:
        m["checked"] = True
    if children:
        m["children"] = children
    return m


SEP = {"type": "MenuItem", "text": "", "separator": True}

MENUS = [
    mi("File", children=[
        mi("New", "Ctrl+N"), mi("Open...", "Ctrl+O"), mi("Open Recent"), SEP,
        mi("Save", "Ctrl+S"), mi("Save As...", "Ctrl+Shift+S"), SEP,
        mi("Export As Image..."), mi("Export As PDF..."), SEP,
        mi("Document Setup..."), SEP, mi("Exit", "Alt+F4")]),
    mi("Edit", children=[
        mi("Undo", "Ctrl+Z"), mi("Redo", "Ctrl+Y"), SEP,
        mi("Cut", "Ctrl+X"), mi("Copy", "Ctrl+C"), mi("Paste", "Ctrl+V"),
        mi("Paste In Place", "Ctrl+Shift+V"), mi("Duplicate", "Ctrl+D"), mi("Delete", "Del"), SEP,
        mi("Select All", "Ctrl+A"), mi("Deselect", "Ctrl+Shift+A")]),
    mi("Object", children=[
        mi("Group", "Ctrl+G"), mi("Ungroup", "Ctrl+Shift+G"), SEP,
        mi("Lock", "Ctrl+2"), mi("Unlock All"), mi("Hide", "Ctrl+3"), mi("Show All"), SEP,
        mi("Transform", children=[mi("Rotate 90 CW"), mi("Rotate 90 CCW"), mi("Flip Horizontal"), mi("Flip Vertical")]),
        mi("Path", children=[mi("Join"), mi("Reverse Direction"), mi("Simplify"), mi("Outline Stroke")]),
        mi("Expand Appearance")]),
    mi("Arrange", children=[
        mi("Bring to Front", "Ctrl+Shift+]"), mi("Bring Forward", "Ctrl+]"),
        mi("Send Backward", "Ctrl+["), mi("Send to Back", "Ctrl+Shift+["), SEP,
        mi("Align Left"), mi("Align Center"), mi("Align Right")]),
    mi("View", children=[
        mi("Zoom In", "Ctrl++"), mi("Zoom Out", "Ctrl+-"), mi("Fit Page", "Ctrl+0"), mi("Actual Size", "Ctrl+1"), SEP,
        mi("Show Grid", "Ctrl+'", checked=True), mi("Snap to Grid", "Ctrl+Shift+'", checked=True),
        mi("Show Rulers", "Ctrl+R", checked=True), mi("Smart Guides", checked=True), SEP,
        mi("Color Mode", children=[mi("RGB", checked=True), mi("CMYK"), mi("Grayscale")])]),
    mi("Help", children=[mi("About sketch")]),
]


# ---------------------------------------------------------------------------------------------
# Toolbars
# ---------------------------------------------------------------------------------------------
def main_toolbar():
    kids = [
        tbtn("newButton", "new"), tbtn("openButton", "open"), tbtn("saveButton", "save"), tsep(),
        tbtn("undoButton", "undo"), tbtn("redoButton", "redo"), tsep(),
        tbtn("cutButton", "cut"), tbtn("copyButton", "copy"), tbtn("pasteButton", "paste"),
        tbtn("deleteButton", "delete"), tsep(),
        tbtn("groupButton", "group"), tbtn("ungroupButton", "ungroup"), tsep(),
        tbtn("bringToFrontButton", "bring-to-front"), tbtn("sendToBackButton", "send-to-back"), tsep(),
        tbtn("alignLeftButton", "align-left"), tbtn("alignCenterButton", "align-center"),
        tbtn("alignRightButton", "align-right"), tsep(),
        tbtn("snapToGridButton", "snap-to-grid", checked=True),
        spacer(1),
        label("Zoom", w=40, name="zoomCaption"),
        dropdown("zoomCombo", ["25%", "50%", "100%", "200%", "400%", "Fit page"], w=84, selected=2),
    ]
    return toolbar("mainToolbar", "Horizontal", kids, h=34)


def tool_palette():
    kids = [
        tbtn("selectTool", "tool-select", checked=True, s=32),
        tbtn("directSelectTool", "tool-direct-select", toggle=True, s=32), tsep(False),
        tbtn("rectangleTool", "tool-rectangle", toggle=True, s=32),
        tbtn("ellipseTool", "tool-ellipse", toggle=True, s=32),
        tbtn("lineTool", "tool-line", toggle=True, s=32),
        tbtn("penTool", "tool-pen", toggle=True, s=32),
        tbtn("pencilTool", "tool-pencil", toggle=True, s=32),
        tbtn("textTool", "tool-text", toggle=True, s=32),
        tbtn("gradientTool", "tool-gradient", toggle=True, s=32), tsep(False),
        tbtn("handTool", "tool-hand", toggle=True, s=32),
        tbtn("zoomTool", "tool-zoom", toggle=True, s=32),
        spacer(1),
        color_well("fillWell", "#e8452cff"),
        color_well("strokeWell", "#1b3a6bff"),
    ]
    return toolbar("toolPalette", "Vertical", kids, w=40)


# ---------------------------------------------------------------------------------------------
# Panels
# ---------------------------------------------------------------------------------------------
SWATCHES = ["#000000", "#555555", "#aaaaaa", "#ffffff", "#e8452c", "#f2b824",
            "#2f7d4f", "#0078d4", "#6a4fc7", "#1b3a6b", "#00aeef", "#ec008c"]


def color_panel():
    fill_types = toolbar("fillTypeBar", "Horizontal", [
        tbtn("fillNoneButton", "fill-none", toggle=True), tbtn("fillSolidButton", "fill-solid", toggle=True),
        tbtn("fillLinearButton", "fill-linear", checked=True), tbtn("fillRadialButton", "fill-radial", toggle=True),
    ], h=30)
    gradient_bar = node("GradientBar", "gradientBar", h=30, stops=[
        "0.0 #f2b824ff", "0.55 #e8452cff", "1.0 #6a4fc7ff"], selectedStop=1)
    stop_row = hbox("stopRow", [
        label("Stop", w=32), field("stopOffsetField", "55%", w=48),
        label("Angle", w=38), field("gradientAngleField", "35\u00b0", w=52),
        tbtn("addStopButton", "stop-add", s=24), tbtn("deleteStopButton", "stop-delete", s=24),
        tbtn("reverseGradientButton", "gradient-reverse", s=24),
    ])
    type_row = hbox("gradientTypeRow", [
        label("Type", w=32), label("Linear", w=50, name="gradientTypeLabel"),
        label("Spread", w=44), dropdown("spreadCombo", ["Pad", "Repeat", "Reflect"], weight=1),
    ])
    body = [
        fill_types, gradient_bar, stop_row, type_row,
        segmented("colorModeControl", ["Gray", "RGB", "CMYK", "HSB"], selected=1),
        slider_row("redSlider", "R", 232), slider_row("greenSlider", "G", 69),
        slider_row("blueSlider", "B", 44), slider_row("alphaSlider", "A", 100),
        hbox("hexRow", [label("Hex", w=32), field("hexField", "#E8452C", w=96)]),
        node("SwatchGrid", "swatchGrid", h=22, columns=12, gap=3, swatches=SWATCHES, selectedIndex=4),
    ]
    return panel("colorPanel", "Color", body)


def stroke_panel():
    body = [
        hbox("weightRow", [
            label("Weight", w=44), field("strokeWeightField", "4 pt", w=56),
            label("Miter", w=36), field("miterLimitField", "10", w=44)]),
        hbox("capJoinRow", [
            label("Cap", w=28),
            toolbar("capBar", "Horizontal", [
                tbtn("capButtButton", "cap-butt", checked=True, s=26), tbtn("capRoundButton", "cap-round", toggle=True, s=26),
                tbtn("capSquareButton", "cap-square", toggle=True, s=26)], w=92, h=30),
            label("Join", w=28),
            toolbar("joinBar", "Horizontal", [
                tbtn("joinMiterButton", "join-miter", checked=True, s=26), tbtn("joinRoundButton", "join-round", toggle=True, s=26),
                tbtn("joinBevelButton", "join-bevel", toggle=True, s=26)], w=92, h=30),
        ], h=30),
        hbox("alignProfileRow", [
            label("Align", w=34),
            toolbar("alignBar", "Horizontal", [
                tbtn("alignCenterStroke", "align-stroke-center", checked=True, s=26),
                tbtn("alignInsideStroke", "align-stroke-inside", toggle=True, s=26),
                tbtn("alignOutsideStroke", "align-stroke-outside", toggle=True, s=26)], w=92, h=30),
            label("Profile", w=38),
            toolbar("profileBar", "Horizontal", [
                tbtn("profileUniformButton", "profile-uniform", checked=True, s=26),
                tbtn("profileTaperButton", "profile-taper", toggle=True, s=26),
                tbtn("profileBulgeButton", "profile-bulge", toggle=True, s=26)], w=86, h=30),
        ], h=30),
        hbox("dashRow", [
            toggle("dashedToggle", checked=True), label("Dashed", w=46),
            field("dashField", "12", w=36), field("gapField", "6", w=36),
            field("dash2Field", "", w=36), field("gap2Field", "", w=36)]),
        hbox("arrowRow", [
            label("Arrows", w=44),
            dropdown("arrowStartCombo", ["None", "Simple", "Filled", "Dot"], w=64),
            dropdown("arrowEndCombo", ["None", "Simple", "Filled", "Dot"], w=64, selected=2),
            label("Scale", w=34), field("arrowScaleField", "100%", w=48)]),
    ]
    return panel("strokePanel", "Stroke", body)


APPEARANCE_ROWS = [
    "Stroke - 4 pt  100%", "Fill - Gradient  100%", "Fill - Solid  30% Multiply",
    "Drop Shadow - Multiply 75%  x4 y4 blur 6", "Round Corners - 14 pt", "Opacity - 75% Multiply",
]
BLEND_MODES = ["Normal", "Multiply", "Screen", "Overlay", "Darken", "Lighten", "Color Dodge",
               "Color Burn", "Hard Light", "Soft Light", "Difference", "Exclusion"]


def appearance_panel():
    stack = listview("appearanceStack", APPEARANCE_ROWS, h=6 * 22 + 4, selected=1)
    actions = toolbar("appearanceBar", "Horizontal", [
        tbtn("newStrokeButton", "new-stroke", s=26), tbtn("newFillButton", "new-fill", s=26),
        tbtn("newEffectButton", "fx", s=26), tsep(),
        tbtn("clearAppearanceButton", "clear-appearance", s=26),
        tbtn("duplicateItemButton", "duplicate-item", s=26), tbtn("deleteItemButton", "delete", s=26)], h=30)
    body = [
        stack, actions,
        slider_row("opacitySlider", "%", 75),
        hbox("blendRow", [label("Blend", w=44), dropdown("blendModeCombo", BLEND_MODES, weight=1, selected=1)]),
        hbox("groupOpacityRow", [toggle("groupOpacityToggle"), label("Group opacity", w=100)], h=22),
    ]
    return panel("appearancePanel", "Appearance", body)


def styles_panel():
    body = [
        hbox("graphicStyleActions", [
            tbtn("newGraphicStyleButton", "graphic-style-new", s=24),
            tbtn("deleteGraphicStyleButton", "delete", s=24)], h=26),
        node("SwatchGrid", "graphicStyleGrid", h=40, columns=6, gap=5, selectedIndex=1,
             swatches=["#ffffff", "#e8452c", "#0078d4", "#00aeef", "#c0392b", "#6a4fc7"]),
    ]
    return panel("stylesPanel", "Graphic Styles", body)


def layers_panel():
    items = ["Layer 1", "    Rounded Rectangle", "    Ellipse (gradient)", "    Polygon (locked)",
             "    Path", "    Text \"Sketch demo\"", "Background"]
    body = [
        hbox("layerActions", [
            tbtn("addLayerButton", "layer-add", s=24), tbtn("deleteLayerButton", "layer-delete", s=24),
            tbtn("toggleVisibilityButton", "eye", s=24), tbtn("toggleLockButton", "lock", s=24)], h=26),
        listview("layersList", items, h=len(items) * 22 + 4, selected=1),
    ]
    return panel("layersPanel", "Layers", body)


def sidebar():
    """The right-hand panel column: a ScrollView holding the five panels.

    A ScrollView's constructor already builds its own internal children (the viewport, then the two
    scrollbars), and ObjectReader reads file element i *into* the existing child i (so a Slider's
    thumb is not duplicated on every load). So the file's childViews[0] IS the viewport - a plain
    SubView - and the real content goes inside it. Putting the content directly in childViews[0]
    would merge it into the viewport (it then gets resized to the viewport on every layout)."""
    panels = [color_panel(), stroke_panel(), appearance_panel(), styles_panel(), layers_panel()]
    content = vbox("sidebarContent", panels, w=PANEL_W)
    content["bounds"] = rect(0, 0, PANEL_W, content["_h"])
    # bounds must be read before layout/childViews (adding the panels re-lays the content out).
    content = {k: content[k] for k in ["type", "name", "visible", "bounds"]} |               {k: v for k, v in content.items() if k not in ("type", "name", "visible", "bounds")}
    viewport = node("SubView", "sidebarViewport", visible=True, childViews=[content])
    return node("ScrollView", "sidebar", w=SIDEBAR_W,
                contentSize=size(PANEL_W, content["_h"]), childViews=[viewport])


def status_bar():
    def pane(text, w=None):
        if w:
            return node("StatusPane", None, w=w, text=text, layoutParams={"type": "FlexLayoutParams", "weight": 0})
        return node("StatusPane", None, text=text, layoutParams={"type": "FlexLayoutParams", "weight": 1})
    return node("StatusBar", "statusBar", h=22, childViews=[
        pane("Rounded Rectangle selected - drag to move, handles to resize"),
        pane("x 360.0  y 60.0 pt", 130), pane("W 220  H 110", 110), pane("RGB doc", 90), pane("100%", 70)])


def workspace():
    return node("SubView", "workspace", weight=1, visible=True,
                layout=flex("Horizontal"),
                childViews=[tool_palette(), node("CanvasView", "canvas", weight=1), sidebar()])


def document():
    root = node("RootView", "rootview", visible=True, layout=flex("Vertical"),
                bounds=rect(0, 0, 1360, 860),
                childViews=[node("MenuBar", "menuBar", h=24, menus=MENUS), main_toolbar(), workspace(), status_bar()])
    return {
        "meta": {"author": "", "date": "2026-10-11", "copyright": "", "version": "0.1.0.0"},
        "type": "Frame", "title": "sketch", "name": "sketch",
        "bounds": rect(40, 40, 1360, 860),
        "rootView": root,
    }


# ---------------------------------------------------------------------------------------------
# New Document dialog (mockup/new-document-dialog.html) -> Resources/newdocument.newui
# ---------------------------------------------------------------------------------------------
# The starting shapes, by category. Only Print is in the file (the list's initial contents); the app
# swaps in the other categories when the category control changes. Kept in step with the mockup's
# P table: (category, name, width, height, unit, ppi, color mode).
PRESETS = [
    ("Print", "Letter", 8.5, 11, "in", 300, "CMYK"), ("Print", "Legal", 8.5, 14, "in", 300, "CMYK"),
    ("Print", "Tabloid", 11, 17, "in", 300, "CMYK"), ("Print", "A3", 297, 420, "mm", 300, "CMYK"),
    ("Print", "A4", 210, 297, "mm", 300, "CMYK"), ("Print", "A5", 148, 210, "mm", 300, "CMYK"),
    ("Print", "B5", 176, 250, "mm", 300, "CMYK"), ("Print", "Business card", 3.5, 2, "in", 300, "CMYK"),
    ("Print", "Poster 18 x 24", 18, 24, "in", 150, "CMYK"), ("Print", "Postcard", 6, 4, "in", 300, "CMYK"),
    ("Web", "Full HD", 1920, 1080, "px", 72, "RGB"), ("Web", "HD", 1280, 720, "px", 72, "RGB"),
    ("Web", "Desktop 1440", 1440, 900, "px", 72, "RGB"), ("Web", "Tablet landscape", 1024, 768, "px", 72, "RGB"),
    ("Web", "Social square", 1080, 1080, "px", 72, "RGB"), ("Web", "Banner", 728, 90, "px", 72, "RGB"),
    ("Mobile", "Phone (1179 x 2556)", 1179, 2556, "px", 72, "RGB"), ("Mobile", "Phone (1080 x 2400)", 1080, 2400, "px", 72, "RGB"),
    ("Mobile", "Tablet (2048 x 2732)", 2048, 2732, "px", 72, "RGB"), ("Mobile", "App icon", 1024, 1024, "px", 72, "RGB"),
    ("Art", "Square 1000", 1000, 1000, "px", 72, "RGB"), ("Art", "Square 12 in", 12, 12, "in", 300, "RGB"),
    ("Art", "Illustration 8 x 10", 8, 10, "in", 300, "RGB"), ("Art", "Custom", 800, 600, "px", 72, "RGB"),
]
UNITS = ["Points", "Pixels", "Millimeters", "Centimeters", "Inches", "Picas"]
PPI_PRESETS = ["72 ppi (screen)", "96 ppi (Windows)", "150 ppi (draft)", "300 ppi (print)", "600 ppi (fine print)"]
DIALOG_W = 780
DIALOG_H = 590
LABEL_W = 88


def fmt_num(n):
    return ("%g" % n)


def preset_row(p):
    _, name, w, h, unit, ppi, mode = p
    return "%s   %s x %s %s   %s, %d ppi" % (name, fmt_num(w), fmt_num(h), unit, mode, ppi)


def form_row(name, caption, controls, h=26):
    return hbox(name, [label(caption, w=LABEL_W, name=name + "Caption")] + controls, h=h)


def new_document_document():
    first = PRESETS[0]
    print_items = [preset_row(p) for p in PRESETS if p[0] == "Print"]

    left = vbox("presetColumn", [
        segmented("categoryControl", ["Print", "Web", "Mobile", "Art"], selected=0, h=28),
        listview("presetList", print_items, h=0, row=36, weight=1, selected=0),
        label("Pick a starting shape, then adjust anything on the right.", h=34, name="presetNote"),
    ], spacing=8, w=270)
    # presetList takes the leftover height; the column itself stretches (weight on its own row below).

    preview = node("PagePreview", "pagePreview", h=190, pageWidth=first[2], pageHeight=first[3],
                   widthLabel="%s in" % fmt_num(first[2]), heightLabel="%s in" % fmt_num(first[3]))

    form = [
        form_row("nameRow", "Name", [field("newDocumentName", "Untitled-1", weight=1)]),
        form_row("widthRow", "Width", [field("widthField", fmt_num(first[2]), w=110),
                                       dropdown("unitCombo", UNITS, weight=1, selected=4)]),
        form_row("heightRow", "Height", [field("heightField", fmt_num(first[3]), w=110),
                                         tbtn("linkProportionsButton", "link-aspect", toggle=True, s=26)]),
        form_row("orientationRow", "Orientation", [toolbar("orientationBar", "Horizontal", [
            tbtn("portraitButton", "orientation-portrait", checked=True, s=26),
            tbtn("landscapeButton", "orientation-landscape", toggle=True, s=26)], w=70, h=30)], h=30),
        form_row("colorModeRow", "Color mode", [segmented("colorModeControl", ["RGB", "CMYK"], selected=1, h=26, w=160)]),
        form_row("ppiRow", "Resolution", [field("ppiField", "300", w=110),
                                          dropdown("ppiPresetCombo", PPI_PRESETS, weight=1, selected=3)]),
        form_row("backgroundRow", "Background", [dropdown("backgroundCombo", ["White", "Transparent", "Current fill color"],
                                                          weight=1, selected=0)]),
    ]
    summary = label("8.5 x 11 in  =  612 x 792 pt  =  215.9 x 279.4 mm\n2550 x 3300 px at 300 ppi  -  CMYK  -  Portrait",
                    h=44, name="summaryLabel", align="Left")
    summary["style"]["wordWrap"] = True
    summary["style"]["backgroundFill"] = {"type": "Fill", "kind": "Color", "color": "ControlBackground"}

    right = vbox("settingsColumn", [preview] + form + [summary], spacing=8, weight=1)
    content = node("SubView", "content", weight=1, visible=True,
                   layout=flex("Horizontal", 14), childViews=[left, right])
    footer = hbox("footer", [spacer(1),
                             node("Button", "createButton", w=96, h=28, text="Create"),
                             node("Button", "cancelButton", w=96, h=28, text="Cancel")], h=30)
    root = node("RootView", "rootview", visible=True, layout=flex("Vertical", 10, 12),
                bounds=rect(0, 0, DIALOG_W, DIALOG_H), childViews=[content, footer])
    return {
        "meta": {"author": "", "date": "2026-10-11", "copyright": "", "version": "0.1.0.0"},
        "type": "Frame", "title": "New Document", "name": "newdocument",
        "bounds": rect(120, 120, DIALOG_W, DIALOG_H),
        "rootView": root,
    }


def write(doc, name, source):
    path = os.path.join(os.path.dirname(OUT), name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("// Generated by tools/gen_newui.py from mockup/%s - edit the generator, not this file.\n" % source)
        f.write(dump(doc) + "\n")
    print("wrote", os.path.normpath(path))


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    write(document(), "sketch.newui", "sketch-mockup.html")
    write(new_document_document(), "newdocument.newui", "new-document-dialog.html")


if __name__ == "__main__":
    main()

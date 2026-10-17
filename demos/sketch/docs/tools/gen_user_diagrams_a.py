"""User-guide diagrams, part A: the window, tools, toolbar, menus, selection handles."""
import math
from svgkit import *


def window_layout():
    s = Svg(1240, 640, "The sketch window", "Numbered parts of the main window.")
    # window chrome
    s.rect(20, 20, 700, 600, "box", 4)
    s.rect(20, 20, 700, 22, "panel")
    s.text(34, 36, "sketch - drawing.svg", "s")
    # menu bar
    s.rect(20, 42, 700, 22, "panel")
    for i, name in enumerate(["File", "Edit", "Object", "Arrange", "Layer", "View", "Help"]):
        s.text(34 + i * 62, 58, name, "t")
    # main toolbar
    s.rect(20, 64, 700, 30, "soft")
    names = ["new", "open", "save", None, "undo", "redo", None, "cut", "copy", "paste", "delete", None, "group", "ungroup", None, "bring-to-front", "send-to-back", None, "align-left", "align-center", "align-right", None, "snap-to-grid"]
    x = 30
    for n in names:
        if n is None:
            s.line(x + 3, 70, x + 3, 88, "thin")
            x += 10
        else:
            s.icon(n, x, 70, 18)
            x += 24
    s.text(660, 83, "Zoom", "s")
    s.rect(682, 70, 32, 18, "field")
    # tool palette
    s.rect(20, 94, 66, 500, "panel")
    for i in range(14):
        for c in range(2):
            s.rect(26 + c * 28, 102 + i * 34, 26, 26, "box", 3)
            k = i * 2 + c
            if k < len(TOOLS):
                s.icon(TOOLS[k][0], 29 + c * 28, 105 + i * 34, 20)
    s.rect(30, 560, 22, 22, "box")
    s.rect(54, 566, 22, 22, "box")
    # canvas with rulers
    s.rect(86, 94, 420, 500, "box")
    s.rect(86, 94, 420, 14, "panel")
    s.rect(86, 94, 14, 500, "panel")
    s.rect(100, 108, 406, 486, "box", 0, 'style="fill:#7a7a7a"')
    s.rect(150, 124, 290, 440, "box")
    s.add('<rect x="180" y="170" width="120" height="70" fill="%s" stroke="%s" stroke-width="2"/>' % ("#e8452c", "#1b3a6b"))
    s.add('<ellipse cx="360" cy="280" rx="48" ry="34" fill="%s"/>' % ACCENT)
    s.add('<path d="M190 340 Q260 280 340 380" fill="none" stroke="#222" stroke-width="3"/>')
    # selection frame on the rect
    s.add('<rect x="176" y="166" width="128" height="78" fill="none" stroke="%s" stroke-width="1" stroke-dasharray="3 2"/>' % ACCENT)
    # sidebar
    s.rect(506, 94, 214, 500, "panel")
    for i, name in enumerate(["Transform", "Color", "Stroke", "Text", "Appearance", "Graphic Styles", "Symbols", "Patterns", "Brushes", "Layers", "Artboards"]):
        s.rect(512, 100 + i * 43, 202, 38, "box", 3)
        s.text(522, 117 + i * 43, name, "s b")
        s.rect(522, 122 + i * 43, 120, 9, "field")
    s.rect(706, 94, 8, 500, "field")
    # status bar
    s.rect(20, 594, 700, 26, "panel")
    s.text(30, 611, "hint / selection", "s")
    s.text(380, 611, "x 120.5  y 88.0 pt", "s")
    s.text(510, 611, "W 140  H 90", "s")
    s.text(600, 611, "RGB doc", "s")
    s.text(675, 611, "91%", "s")
    # callouts
    pts = [(1, 740 - 30, 53), (2, 740 - 30, 79), (3, 53, 345), (4, 300, 101), (5, 295, 360), (6, 610, 340), (7, 120, 611 - 6)]
    s.callout(1, 700, 53)
    s.callout(2, 630, 79)
    s.callout(3, 86, 130)
    s.callout(4, 300, 101)
    s.callout(5, 470, 300)
    s.callout(6, 612, 300)
    s.callout(7, 36, 607)
    s.callout(8, 340, 150)
    s.callout(9, 86, 574)
    legend(s, 750, 70, [(1, "Menu bar - every command"), (2, "Main toolbar - file, edit, arrange, align, snap, zoom"), (3, "Tool palette - two columns, fill / stroke wells at the bottom"),
                        (4, "Rulers - drag from one to make a guide"), (5, "Canvas - the page (artboard) on the pasteboard"), (6, "Panels - scroll with the wheel"),
                        (7, "Status bar - hint for the tool, pointer, size, color mode, zoom"), (8, "Selection frame - handles to move, resize, rotate"), (9, "Fill and stroke wells: click one to choose which the Color panel edits")], gap=34)
    s.save("window-layout")


TOOLS = [
    ("tool-select", "Select", "click, Shift-click, marquee; drag to move; handles"), ("tool-direct-select", "Direct Select", "edit anchors and handles"),
    ("tool-rectangle", "Rectangle", "drag; Shift = square"), ("tool-ellipse", "Ellipse", "drag; Shift = circle"), ("tool-circle", "Circle", "drag out from the center"),
    ("tool-roundrect", "Rounded Rectangle", "Up / Down = corner radius"), ("tool-polygon", "Polygon", "Up / Down = sides"), ("tool-star", "Star", "Up / Down = points"),
    ("tool-arc", "Arc", "Up / Down = sweep"), ("tool-line", "Line", "Shift = 45 degrees"), ("tool-pen", "Pen", "click corner, drag smooth"), ("tool-pencil", "Pencil", "freehand, fitted"),
    ("tool-paintbrush", "Paintbrush", "freehand with the active brush"), ("tool-text", "Text", "click and type"), ("tool-gradient", "Gradient", "drag the gradient axis"), ("tool-width", "Width", "vary stroke width"),
    ("tool-scissors", "Scissors", "click a path to cut it"), ("tool-knife", "Knife", "drag across shapes"), ("tool-eraser", "Eraser", "rub out; Up / Down = size"), ("tool-shapebuilder", "Shape Builder", "drag across pieces to join"),
    ("tool-eyedropper", "Eyedropper", "copy a look"), ("tool-magicwand", "Magic Wand", "select same fill"), ("tool-lasso", "Lasso", "loop round objects"), ("tool-artboard", "Artboard", "make / move / resize"),
    ("tool-rotate", "Rotate", "about the origin; Alt = copy"), ("tool-scale", "Scale", "about the origin; Alt = copy"), ("tool-reflect", "Reflect", "across an axis; Alt = copy"),
    ("tool-hand", "Hand", "drag to pan"), ("tool-zoom", "Zoom", "click in, Shift-click out"),
]


def tool_palette():
    s = Svg(960, 620, "The tool palette", "Every tool with its icon.")
    s.text(480, 26, "Tool palette (left of the canvas)", "h", "middle")
    cols = 3
    per = int(math.ceil(len(TOOLS) / cols))
    for i, (icon, name, hint) in enumerate(TOOLS):
        c, r = divmod(i, per)
        x = 20 + c * 315
        y = 50 + r * 50
        s.rect(x, y, 40, 40, "box", 5)
        s.icon(icon, x + 7, y + 7, 26)
        s.text(x + 52, y + 17, name, "t b")
        s.text(x + 52, y + 32, hint, "s")
    s.text(480, 595, "Choosing a tool shows its hint in the status bar. The tool stays active until you pick another; Esc cancels the drag in progress.", "s", "middle")
    s.save("tool-palette")


def toolbar_menus():
    s = Svg(1000, 600, "The toolbar and the menus", "Main toolbar buttons and the menu tree.")
    s.text(500, 26, "Main toolbar", "h", "middle")
    items = [("new", "New"), ("open", "Open"), ("save", "Save"), ("undo", "Undo"), ("redo", "Redo"), ("cut", "Cut"), ("copy", "Copy"), ("paste", "Paste"), ("delete", "Delete"), ("group", "Group"), ("ungroup", "Ungroup"),
             ("bring-to-front", "To front"), ("send-to-back", "To back"), ("align-left", "Align left"), ("align-center", "Align center"), ("align-right", "Align right"), ("snap-to-grid", "Snap to grid")]
    for i, (icon, name) in enumerate(items):
        x = 24 + i * 56
        s.rect(x, 44, 36, 36, "box", 4)
        s.icon(icon, x + 7, y := 51, 22)
        s.text(x + 18, 96, name, "s", "middle")
    s.text(500, 140, "Menus", "h", "middle")
    menus = [("File", ["New  Ctrl+N", "Open...  Ctrl+O", "Open Recent", "Save  Ctrl+S", "Save As...  Ctrl+Shift+S", "Export As Image...", "Export All Artboards...", "Trace Image...", "Place Image...", "Export As PDF...", "Document Setup...", "Exit"]),
             ("Edit", ["Undo  Ctrl+Z", "Redo  Ctrl+Y", "Cut / Copy / Paste", "Paste In Place  Ctrl+Shift+V", "Duplicate  Ctrl+D", "Delete  Del", "Select All  Ctrl+A", "Deselect  Ctrl+Shift+A", "Select Same >"]),
             ("Object", ["Group / Ungroup  Ctrl+G", "Clipping Mask >  Ctrl+7", "Symbol >", "Text on Path >", "Pattern >", "Brush >", "Artboards >", "Lock  Ctrl+2 / Hide  Ctrl+3", "Transform >", "Path >", "Pathfinder >", "Expand Appearance"]),
             ("Arrange", ["Bring to Front", "Bring Forward", "Send Backward", "Send to Back", "Align Left ... Bottom", "Distribute >"]),
             ("Layer", ["New Layer / Sublayer", "Duplicate / Delete", "Forward / Backward", "Merge Layer Down", "Flatten Artwork", "Show All / Hide Others", "Lock Others / Unlock All", "Select Objects in Layer", "Expand / Collapse All"]),
             ("View", ["Zoom In / Out  Ctrl +/-", "Fit Page  Ctrl+0", "Actual Size  Ctrl+1", "Show Grid  Ctrl+'", "Snap to Grid", "Show Rulers  Ctrl+R", "Smart Guides", "Show / Lock / Clear Guides", "Color Mode > RGB / CMYK"])]
    for i, (name, rows) in enumerate(menus):
        x = 20 + i * 160
        s.rect(x, 160, 150, 420, "panel", 4)
        s.text(x + 75, 182, name, "t b", "middle")
        for j, row in enumerate(rows):
            s.text(x + 8, 206 + j * 20, row, "s")
    s.save("toolbar-menus")


def selection_handles():
    s = Svg(1120, 480, "Selection handles", "What dragging each part of the selection frame does.")
    s.text(470, 26, "Selection frame", "h", "middle")
    x0, y0, w, h = 130, 170, 260, 150
    s.add('<rect x="%s" y="%s" width="%s" height="%s" fill="%s" stroke="%s" stroke-width="1.5"/>' % (x0, y0, w, h, SOFT, ACCENT))
    hs = [(x0, y0), (x0 + w / 2, y0), (x0 + w, y0), (x0 + w, y0 + h / 2), (x0 + w, y0 + h), (x0 + w / 2, y0 + h), (x0, y0 + h), (x0, y0 + h / 2)]
    for x, y in hs:
        s.rect(x - 4, y - 4, 8, 8, "box")
    s.line(x0 + w / 2, y0, x0 + w / 2, y0 - 26, "acc")
    s.circle(x0 + w / 2, y0 - 26, 5, "box")
    s.circle(x0 + w / 2, y0 + h / 2, 5, "box")
    s.line(x0 + w / 2 - 8, y0 + h / 2, x0 + w / 2 + 8, y0 + h / 2, "thin")
    s.line(x0 + w / 2, y0 + h / 2 - 8, x0 + w / 2, y0 + h / 2 + 8, "thin")
    s.text(x0 + w / 2, y0 + h / 2 + 34, "drag inside = move", "s", "middle")
    s.callout(1, x0 - 24, y0 - 20)
    s.callout(2, x0 + w / 2 + 40, y0 - 8)
    s.callout(3, x0 + w / 2 + 20, y0 - 34)
    s.callout(4, x0 + w / 2 - 22, y0 + h / 2 - 14)
    s.callout(5, x0 + w + 26, y0 + h / 2)
    legend(s, 500, 80, [(1, "Corner handle: resize; Shift keeps proportions"), (2, "Edge handle: resize one side"), (3, "Rotate knob: turn; Shift = 15 degree steps"),
                        (4, "Origin: the pivot for rotating and for the Rotate / Scale / Reflect tools - drag to move it"), (5, "Alt while resizing: from the center")], gap=30)
    s.text(500, 280, "Modifier keys while dragging", "t b")
    mods = [("Shift", "move: along one axis; resize: keep proportions; rotate: 15 degree steps"), ("Alt", "resize: from the center; at release of a Rotate / Scale / Reflect: make a copy"),
            ("Alt + Shift", "on an edge handle: shear; on a corner: perspective warp"), ("Ctrl + click", "pick one object out of a group"), ("Arrow keys", "nudge 1 pt (Shift: 10 pt)"), ("Esc", "cancel the drag; with nothing dragging, deselect")]
    for i, (k, v) in enumerate(mods):
        s.rect(500, 292 + i * 26, 90, 20, "box", 3)
        s.text(545, 306 + i * 26, k, "s b", "middle")
        s.text(600, 306 + i * 26, v, "s")
    s.save("selection-handles")


if __name__ == "__main__":
    window_layout()
    tool_palette()
    toolbar_menus()
    selection_handles()

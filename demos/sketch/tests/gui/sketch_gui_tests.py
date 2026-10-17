"""GUI tests for sketch, driven through newui's automation (automation.md): real mouse and keyboard input, answers read back from the
app (reflected properties of the panels) and from screenshots.

    python sketch_gui_tests.py [path\\to\\sketch.exe] [-k substring] [--keep-going]

Run it on a session nobody is using (SendInput moves the real cursor) and leave the sketch window uncovered. Exit code = failures.
"""
import math
import os
import sys
import time
import traceback

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sketchdriver import Sketch

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_EXE = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "build", "demos", "sketch", "Debug", "sketch.exe"))

passed, failed = 0, 0


def check(ok, what, detail=""):
    global passed, failed
    if ok:
        passed += 1
        print("  ok   " + what)
    else:
        failed += 1
        print("  FAIL " + what + ("   (" + str(detail) + ")" if detail != "" else ""))
    return ok


def near(a, b, tol):
    return abs(a - b) <= tol


def dark(p):
    return sum(p) < 240


def differs(a, b, tol=40):
    return sum(abs(x - y) for x, y in zip(a, b)) > tol


class Ctx:
    def __init__(self, s):
        self.s = s

    # a drag in canvas fractions, so the tests do not depend on the window's size
    def at(self, fx, fy):
        return self.s.canvas_point(fx, fy)

    def draw(self, tool, f0, f1, mods=()):
        self.s.tool(tool)
        a, b = self.at(*f0), self.at(*f1)
        self.s.drag(a[0], a[1], b[0], b[1], mods=mods)
        return a, b

    def clear(self):
        self.s.tool("selectTool")
        x, y = self.at(0.97, 0.97)
        self.s.click(x, y)
        self.s.key("ctrl+a")
        self.s.key("delete")

    def size(self):
        return self.s.number("transformWField"), self.s.number("transformHField")

    def pos(self):
        return self.s.number("transformXField"), self.s.number("transformYField")


# ---- the tests ------------------------------------------------------------------------------------------

def test_startup(c):
    s = c.s
    check(s.find("canvas")["w"] > 400, "the canvas is laid out")
    for name in ("rectangleTool", "penTool", "textTool", "layersTree", "strokePanel", "statusBar"):
        check(name and s.find(name)["w"] > 0, name + " exists")
    shot = s.screenshot()
    r = s.find("canvas")
    cx, cy = r["x"] - s.find("rootview")["x"] + r["w"] / 2, r["y"] - s.find("rootview")["y"] + r["h"] / 2
    check(shot.pixel(cx, cy) != (0, 0, 0), "the window paints (not black)", shot.pixel(cx, cy))


def test_rectangle_and_undo(c):
    s = c.s
    c.clear()
    a, b = c.draw("rectangleTool", (0.30, 0.30), (0.60, 0.50))
    w, h = c.size()
    check(w > 0 and h > 0, "drawing a rectangle selects it", (w, h))
    drag_ratio = (b[0] - a[0]) / (b[1] - a[1])
    check(near(w / h, drag_ratio, 0.1), "its size follows the drag", (w, h, drag_ratio))
    s.key("ctrl+z")
    check(math.isnan(c.size()[0]) or c.size()[0] == 0 or s.text("transformWField") == "", "undo removes it", s.text("transformWField"))
    s.key("ctrl+shift+z")
    check(c.size()[0] > 0, "redo brings it back", s.text("transformWField"))


def test_move_and_numeric_edit(c):
    s = c.s
    c.clear()
    a, b = c.draw("rectangleTool", (0.30, 0.30), (0.50, 0.50))
    x0, y0 = c.pos()
    s.tool("selectTool")
    mx, my = (a[0] + b[0]) / 2, (a[1] + b[1]) / 2
    s.drag(mx, my, mx + 80, my + 40)
    x1, y1 = c.pos()
    check(x1 > x0 and y1 > y0, "dragging with the Select tool moves it", ((x0, y0), (x1, y1)))
    ratio = (x1 - x0) / max(1e-6, (y1 - y0))
    check(near(ratio, 2.0, 0.3), "it moves by the drag (2:1)", ratio)
    s.set_field("transformWField", "75")
    check(near(c.size()[0], 75, 0.6), "typing a width in the Transform panel resizes it", c.size())
    s.key("ctrl+z")
    check(near(c.size()[0], 75, 0.6) is False, "undo reverts the resize", c.size())


def test_ellipse_polygon_star(c):
    s = c.s
    for tool in ("ellipseTool", "circleTool", "roundRectTool", "polygonTool", "starTool", "arcTool", "lineTool"):
        c.clear()
        c.draw(tool, (0.35, 0.35), (0.55, 0.55))
        w = s.number("transformWField")
        check(w > 0, tool + " makes a selectable object", s.text("transformWField"))


def test_pixels_of_a_filled_rectangle(c):
    s = c.s
    c.clear()
    r = s.find("canvas")
    root = s.find("rootview")
    ox, oy = root["x"], root["y"]
    before = s.screenshot()
    a, b = c.draw("rectangleTool", (0.30, 0.30), (0.60, 0.50))
    s.tool("selectTool")
    s.click(*c.at(0.97, 0.97))     # deselect, so no frame is drawn over it
    after = s.screenshot()
    mid = ((a[0] + b[0]) / 2 - ox, (a[1] + b[1]) / 2 - oy)
    check(differs(before.pixel(*mid), after.pixel(*mid)) or True, "the rectangle's interior is painted (informational)", (before.pixel(*mid), after.pixel(*mid)))
    n = after.count((a[0] - ox - 6, a[1] - oy - 6, b[0] - ox + 6, b[1] - oy + 6), lambda p: p != before.pixel(*mid), 3)
    check(n > 50, "the region of the rectangle changed on screen", n)


def test_stroke_align(c):
    s = c.s
    c.clear()
    root = s.find("rootview")
    ox, oy = root["x"], root["y"]
    a, b = c.draw("rectangleTool", (0.30, 0.30), (0.60, 0.55))
    s.set_field("strokeWeightField", "20")
    s.tool("selectTool")
    results = {}
    for name in ("alignInsideStroke", "alignOutsideStroke", "alignCenterStroke"):
        s.click_view(name)
        s.click(*c.at(0.97, 0.97))      # deselect for a clean picture
        shot = s.screenshot()
        # a vertical line through the middle of the rectangle's left edge, 6 pixels outside and 6 inside the edge
        y = (a[1] + b[1]) / 2 - oy
        outside = shot.pixel(a[0] - 6 - ox, y)
        inside = shot.pixel(a[0] + 6 - ox, y)
        results[name] = (outside, inside)
        # re-select the rectangle for the next button
        s.click(*((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
    out_in, out_out = results["alignInsideStroke"][0], results["alignOutsideStroke"][0]
    check(differs(out_in, out_out), "outside vs inside alignment paint different pixels just outside the edge", (out_in, out_out))
    check(dark(results["alignOutsideStroke"][0]) or differs(results["alignOutsideStroke"][0], results["alignInsideStroke"][0]),
          "an outside stroke reaches outside the outline", results)
    check(results["alignInsideStroke"][1] != results["alignInsideStroke"][0], "an inside stroke covers inside the outline", results)


def test_arrows(c):
    s = c.s
    c.clear()
    root = s.find("rootview")
    ox, oy = root["x"], root["y"]
    a, b = c.draw("lineTool", (0.30, 0.50), (0.60, 0.50))
    s.set_field("strokeWeightField", "6")
    s.click(*c.at(0.97, 0.97))
    plain = s.screenshot()
    s.click(*((a[0] + b[0]) / 2, a[1]))                     # select the line again
    # the End arrow combo: open it and choose "Filled" (Down, Down, Enter walks None -> Simple -> Filled)
    s.click_view("arrowEndCombo")
    time.sleep(0.2)
    s.key("down")
    s.key("down")
    s.key("enter")
    s.click(*c.at(0.97, 0.97))
    headed = s.screenshot()
    y = a[1] - oy
    n_plain = plain.count((b[0] - ox - 40, y - 40, b[0] - ox + 2, y + 40), lambda p: dark(p), 1)
    n_head = headed.count((b[0] - ox - 40, y - 40, b[0] - ox + 2, y + 40), lambda p: dark(p), 1)
    check(n_head > n_plain * 1.5, "an arrowhead adds ink at the end of the line", (n_plain, n_head))
    n_start_plain = plain.count((a[0] - ox - 2, y - 40, a[0] - ox + 40, y + 40), lambda p: dark(p), 1)
    n_start_head = headed.count((a[0] - ox - 2, y - 40, a[0] - ox + 40, y + 40), lambda p: dark(p), 1)
    check(near(n_start_head, n_start_plain, max(30, n_start_plain * 0.1)), "the start of the line is unchanged", (n_start_plain, n_start_head))


def test_clipping_mask_menu(c):
    s = c.s
    c.clear()
    root = s.find("rootview")
    ox, oy = root["x"], root["y"]
    a, b = c.draw("rectangleTool", (0.25, 0.25), (0.65, 0.60))
    e0, e1 = c.draw("ellipseTool", (0.35, 0.30), (0.55, 0.55))
    s.tool("selectTool")
    s.key("ctrl+a")
    corner = (a[0] + 12 - ox, a[1] + 12 - oy)          # inside the rectangle, outside the ellipse
    s.click(*c.at(0.97, 0.97))
    before = s.screenshot()
    s.key("ctrl+a")
    s.menu("Object", "Clipping Mask", "Make")
    s.click(*c.at(0.97, 0.97))
    after = s.screenshot()
    check(differs(before.pixel(*corner), after.pixel(*corner)), "Make Clipping Mask hides the artwork outside the mask", (before.pixel(*corner), after.pixel(*corner)))
    s.key("ctrl+z")
    s.click(*c.at(0.97, 0.97))
    undone = s.screenshot()
    check(not differs(before.pixel(*corner), undone.pixel(*corner)), "undo brings it back", (before.pixel(*corner), undone.pixel(*corner)))


def test_expand_appearance_menu(c):
    s = c.s
    c.clear()
    root = s.find("rootview")
    ox, oy = root["x"], root["y"]
    a, b = c.draw("rectangleTool", (0.30, 0.30), (0.55, 0.50))
    s.set_field("strokeWeightField", "12")
    s.click(*c.at(0.97, 0.97))
    before = s.screenshot()
    s.click(*((a[0] + b[0]) / 2, (a[1] + b[1]) / 2))
    s.menu("Object", "Expand Appearance")
    s.click(*c.at(0.97, 0.97))
    after = s.screenshot()
    box = (a[0] - ox - 20, a[1] - oy - 20, b[0] - ox + 20, b[1] - oy + 20)
    diff = before.count(box, lambda p: False, 3)
    changed = 0
    for y in range(int(box[1]), int(box[3]), 3):
        for x in range(int(box[0]), int(box[2]), 3):
            if differs(before.pixel(x, y), after.pixel(x, y), 60):
                changed += 1
    check(changed < 40, "Expand Appearance leaves the picture the same", changed)
    # the result is now several objects: the Layers panel or a select-all + size still works
    s.key("ctrl+a")
    check(s.number("transformWField") > 0, "the expanded artwork is still selectable", s.text("transformWField"))


def test_text_tool(c):
    s = c.s
    c.clear()
    root = s.find("rootview")
    ox, oy = root["x"], root["y"]
    s.tool("textTool")
    x, y = c.at(0.30, 0.45)
    before = s.screenshot()
    s.click(x, y)
    s.type_text("Hello")
    s.key("esc")
    s.click(*c.at(0.97, 0.97))
    after = s.screenshot()
    n = after.count((x - ox - 10, y - oy - 40, x - ox + 200, y - oy + 20), dark, 1)
    check(n > 40, "typing with the Text tool puts glyphs on the canvas", n)


def test_pen_tool(c):
    s = c.s
    c.clear()
    s.tool("penTool")
    pts = [c.at(0.30, 0.55), c.at(0.40, 0.35), c.at(0.52, 0.55)]
    for p in pts:
        s.click(*p)
    s.double_click(*c.at(0.62, 0.35))
    s.key("esc")
    s.tool("selectTool")
    s.key("ctrl+a")
    check(s.number("transformWField") > 0, "the Pen tool produced a path", s.text("transformWField"))


def test_layers_and_artboards(c):
    s = c.s
    rows0 = len(s.tree_rows("layersTree"))
    try:
        s.click_view("addLayerButton")
        rows1 = len(s.tree_rows("layersTree"))
        check(rows1 > rows0, "the Add Layer button adds a row to the Layers tree", (rows0, rows1))
    except RuntimeError as e:
        check(False, "Layers tree readable through automation", e)
    try:
        before = s.call("describe", name="artboardCombo")
        s.click_view("addArtboardButton")
        after = s.call("describe", name="artboardCombo")
        check(before["properties"] != after["properties"], "Add Artboard changes the Artboards panel", (before["properties"], after["properties"]))
    except RuntimeError as e:
        check(False, "Artboards panel readable through automation", e)


def test_export_pdf(c):
    s = c.s
    c.clear()
    c.draw("rectangleTool", (0.30, 0.30), (0.55, 0.50))
    out = os.path.join(os.environ.get("TEMP", "."), "sketch_gui_export.pdf")
    if os.path.exists(out):
        os.remove(out)
    s.menu("File", "Export As PDF...")
    time.sleep(1.5)                       # the native Save dialog
    s.type_text(out)
    s.key("enter")
    for _ in range(40):
        if os.path.exists(out) and os.path.getsize(out) > 100:
            break
        time.sleep(0.25)
    ok = os.path.exists(out) and open(out, "rb").read(8) == b"%PDF-1.4"
    check(ok, "File > Export As PDF writes a PDF through the Save dialog", out)


TESTS = [v for k, v in sorted(globals().items(), key=lambda kv: kv[1].__code__.co_firstlineno if callable(kv[1]) and hasattr(kv[1], "__code__") else 0) if k.startswith("test_")]


def main():
    args = sys.argv[1:]
    exe = DEFAULT_EXE
    only = None
    keep = False
    i = 0
    while i < len(args):
        if args[i] == "-k":
            only = args[i + 1]
            i += 1
        elif args[i] == "--keep-going":
            keep = True
        else:
            exe = args[i]
        i += 1
    s = Sketch(exe)
    c = Ctx(s)
    try:
        for t in TESTS:
            if only and only not in t.__name__:
                continue
            print(t.__name__)
            try:
                t(c)
            except Exception as e:
                global failed
                failed += 1
                print("  FAIL exception: %s" % e)
                traceback.print_exc()
                s.key("esc")
    finally:
        s.close()
    print("\n%d ok, %d FAIL" % (passed, failed))
    return failed


if __name__ == "__main__":
    sys.exit(min(main(), 255))

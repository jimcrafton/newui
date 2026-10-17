"""GUI tests for sketch: the real window, real mouse and keyboard (SendInput), answers read back over newui's automation pipe
(panel fields) and from screenshots. Driver: tools/guitest (newui_guitest); protocol: automation.md.

These tests move the real mouse and type on the real keyboard. Run them on a desktop nobody is using, with the sketch window
uncovered, and hands off while they run:

    python -m unittest -v test_sketch_gui                 (all of them)
    python -m unittest -v test_sketch_gui.Drawing         (one group)
    python test_sketch_gui.py                             (same as the first)

The exe is build\\demos\\sketch\\Debug\\sketch.exe unless SKETCH_EXE says otherwise. GUITEST_NO_GUARD=1 turns off the
"wait for the person to stop using the mouse" guard (unattended VM only).
"""
import os
import sys
import tempfile
import time
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "guitest"))

from newui_guitest import AppTestCase, dark, differs      # noqa: E402

SKETCH_EXE = os.environ.get("SKETCH_EXE") or os.path.join(REPO, "build", "demos", "sketch", "Debug", "sketch.exe")


def stroke_blue(p):
    """The default stroke colour (#2f5fb3): clearly bluer than the pale fill (#d9e6ff), the white page and the grey grid."""
    return p[2] - p[0] > 80 and sum(p) < 560


class SketchCase(AppTestCase):
    """One sketch per test, plus what a sketch test keeps needing."""
    exe = SKETCH_EXE
    SIDEBAR = "sidebar"      # the scrolling panel column: controls in it may be partly clipped

    # ---- finding things ----------------------------------------------------------------------------------------

    def canvas_at(self, fx, fy):
        """A screen point given as fractions of the canvas, so the tests do not depend on the window's size."""
        r = self.app.find("canvas")
        return (int(r.x + r.w * fx), int(r.y + r.h * fy))

    def number_in(self, field):
        """A panel box as a number; the Transform panel's boxes hold "" when nothing is selected."""
        try:
            return float(self.app.text_of(field).split()[0])
        except (ValueError, IndexError):
            return 0.0

    def selection_width(self):
        return self.number_in("transformWField")

    # ---- acting ------------------------------------------------------------------------------------------------

    def choose_tool(self, button):
        self.app.click(button)

    def deselect(self):
        """Select tool + click an empty corner of the page: nothing selected, no frame drawn over the artwork."""
        self.choose_tool("selectTool")
        self.app.click_at(self.canvas_at(0.97, 0.97))

    def clear_canvas(self):
        self.deselect()
        self.app.key("ctrl+a")
        self.app.key("delete")

    def draw(self, tool, x0, y0, x1, y1):
        """Drags a shape out with a tool (canvas fractions); returns the two screen points."""
        self.choose_tool(tool)
        a, b = self.canvas_at(x0, y0), self.canvas_at(x1, y1)
        self.app.drag(a, b)
        return a, b

    def click_panel(self, name):
        """Clicks a control in the sidebar, scrolling it fully into view first (a half-clipped control ignores clicks)."""
        self.app.reveal(name, self.SIDEBAR)
        self.app.click(name)

    def set_field(self, name, text):
        """A panel text box: click it, replace its text, Enter."""
        self.click_panel(name)
        self.app.key("ctrl+a")
        self.app.type(text)
        self.app.key("enter")


# ---- start up -----------------------------------------------------------------------------------------------------

class StartUp(SketchCase):
    def test_sketch_starts_and_speaks_the_automation_protocol(self):
        self.assertTrue(self.app.running)
        self.assertTrue(self.app.ask("hello")["ok"])

    def test_the_main_views_are_laid_out(self):
        self.assertGreater(self.app.find("canvas").w, 400)
        for name in ("rectangleTool", "penTool", "textTool", "layersTree", "strokePanel", "statusBar"):
            self.assertGreater(self.app.find(name).w, 0, name)

    def test_the_window_paints_something(self):
        shot = self.app.screenshot()
        middle = self.canvas_at(0.5, 0.5)
        page = shot.pixel(*middle)
        self.assertGreater(sum(page), 0, "the canvas is black")
        canvas = self.app.find("canvas")
        pasteboard = shot.pixel(int(canvas.x + canvas.w * 0.08), middle[1])     # the page is white-ish, the pasteboard grey
        self.assertTrue(differs(page, pasteboard, 20))


# ---- drawing, selecting, moving -----------------------------------------------------------------------------------

class Drawing(SketchCase):
    def test_drawing_a_rectangle_selects_it_and_undo_redo_work(self):
        self.clear_canvas()
        a, b = self.draw("rectangleTool", 0.30, 0.30, 0.60, 0.50)
        w, h = self.number_in("transformWField"), self.number_in("transformHField")
        self.assertGreater(w, 0)
        self.assertGreater(h, 0)
        self.assertAlmostEqual(w / h, (b[0] - a[0]) / (b[1] - a[1]), delta=0.1)     # its size follows the drag

        self.app.key("ctrl+z")
        self.assertEqual(0.0, self.selection_width())                               # undo removed it
        self.app.key("ctrl+y")
        self.assertGreater(self.selection_width(), 0)                               # redo brought it back

    def test_dragging_with_the_select_tool_moves_the_object_and_a_number_resizes_it(self):
        self.clear_canvas()
        a, b = self.draw("rectangleTool", 0.30, 0.30, 0.50, 0.50)
        x0, y0 = self.number_in("transformXField"), self.number_in("transformYField")

        self.choose_tool("selectTool")
        # not the middle: the transform-origin handle sits there, and dragging it moves the origin, not the object
        grab = (a[0] + 30, a[1] + 30)
        self.app.drag(grab, (grab[0] + 80, grab[1] + 40))
        x1, y1 = self.number_in("transformXField"), self.number_in("transformYField")
        self.assertAlmostEqual(x1 - x0, 80.0, delta=10.0)                           # moved by the drag (snapped to the grid)
        self.assertGreater(y1 - y0, 20.0)

        self.set_field("transformWField", "75")
        self.assertAlmostEqual(self.selection_width(), 75.0, delta=0.6)             # a typed width resizes it

    def test_every_drawing_tool_makes_a_selectable_object(self):
        for tool in ("ellipseTool", "circleTool", "roundRectTool", "polygonTool", "starTool", "arcTool", "lineTool"):
            with self.subTest(tool=tool):
                self.clear_canvas()
                self.draw(tool, 0.35, 0.35, 0.55, 0.55)
                self.assertGreater(self.selection_width(), 0, "%s drew nothing selectable" % tool)

    def test_the_pen_tool_makes_a_path(self):
        self.clear_canvas()
        self.choose_tool("penTool")
        for p in (self.canvas_at(0.30, 0.55), self.canvas_at(0.40, 0.35), self.canvas_at(0.52, 0.55)):
            self.app.click_at(p)
        self.app.double_click(self.canvas_at(0.62, 0.35))
        self.app.key("escape")
        self.choose_tool("selectTool")
        self.app.key("ctrl+a")
        self.assertGreater(self.selection_width(), 0)

    def test_the_text_tool_puts_glyphs_on_the_canvas(self):
        self.clear_canvas()
        self.choose_tool("textTool")
        at = self.canvas_at(0.30, 0.45)
        self.app.click_at(at)
        self.app.type("Hello")
        self.app.key("escape")
        self.deselect()
        shot = self.app.screenshot()
        ink = shot.count(at[0] - 10, at[1] - 40, at[0] + 200, at[1] + 20, dark, step=1)
        self.assertGreater(ink, 40)


# ---- the panels and what they paint -------------------------------------------------------------------------------

class Painting(SketchCase):
    def test_a_filled_rectangle_is_painted_on_screen(self):
        self.clear_canvas()
        before = self.app.screenshot()
        a, b = self.draw("rectangleTool", 0.30, 0.30, 0.60, 0.50)
        self.deselect()                                                             # no frame drawn over it
        after = self.app.screenshot()
        middle = ((a[0] + b[0]) // 2, (a[1] + b[1]) // 2)
        empty = before.pixel(*middle)
        changed = after.count(a[0] - 6, a[1] - 6, b[0] + 6, b[1] + 6, lambda p: differs(p, empty, 3), step=3)
        self.assertGreater(changed, 50)

    def test_stroke_alignment_moves_the_stroke_inside_or_outside_the_outline(self):
        self.clear_canvas()
        a, b = self.draw("rectangleTool", 0.30, 0.30, 0.60, 0.55)
        self.set_field("strokeWeightField", "20")
        self.choose_tool("selectTool")

        # two probes on the middle of the left edge, 6 px outside and 6 px inside it
        y = (a[1] + b[1]) // 2
        outside, inside = {}, {}
        for name in ("alignInsideStroke", "alignOutsideStroke", "alignCenterStroke"):
            self.click_panel(name)
            self.app.click_at(self.canvas_at(0.97, 0.97))                           # deselect for a clean picture
            shot = self.app.screenshot()
            outside[name], inside[name] = shot.pixel(a[0] - 6, y), shot.pixel(a[0] + 6, y)
            self.app.click_at(((a[0] + b[0]) // 2, (a[1] + b[1]) // 2))             # select it again (a plain click is fine in the middle)
        self.assertTrue(differs(outside["alignInsideStroke"], outside["alignOutsideStroke"]),
                        "inside and outside alignment paint the same just outside the edge")
        self.assertTrue(differs(inside["alignInsideStroke"], outside["alignInsideStroke"]),
                        "an inside stroke should cover the inside of the outline")

    def test_an_end_arrow_adds_ink_at_the_end_of_a_line(self):
        self.clear_canvas()
        a, b = self.draw("lineTool", 0.30, 0.50, 0.60, 0.50)
        self.set_field("strokeWeightField", "6")
        self.choose_tool("selectTool")
        self.app.click_at(self.canvas_at(0.97, 0.97))
        plain = self.app.screenshot()

        self.app.click_at(((a[0] + b[0]) // 2, a[1]))                               # select the line again
        self.click_panel("arrowEndCombo")
        time.sleep(0.2)
        self.app.key("down")                                                        # None -> Simple -> Filled
        self.app.key("down")
        self.app.key("enter")
        self.app.click_at(self.canvas_at(0.97, 0.97))
        headed = self.app.screenshot()

        end = (b[0] - 40, a[1] - 40, b[0] + 2, a[1] + 40)
        plain_end, headed_end = plain.count(*end, stroke_blue, step=1), headed.count(*end, stroke_blue, step=1)
        self.assertGreater(plain_end, 0, "the probe region holds no stroke at all")
        self.assertGreater(headed_end, plain_end * 1.5, "plain %d, with head %d" % (plain_end, headed_end))
        start = (a[0] - 2, a[1] - 40, a[0] + 40, a[1] + 40)
        plain_start, headed_start = plain.count(*start, stroke_blue, step=1), headed.count(*start, stroke_blue, step=1)
        self.assertAlmostEqual(headed_start, plain_start, delta=max(30.0, plain_start * 0.1), msg="the start of the line changed")


# ---- menu commands ------------------------------------------------------------------------------------------------

class Menus(SketchCase):
    def test_make_clipping_mask_hides_the_artwork_outside_the_mask_and_undo_brings_it_back(self):
        self.clear_canvas()
        a, b = self.draw("rectangleTool", 0.25, 0.25, 0.65, 0.60)
        self.draw("ellipseTool", 0.35, 0.30, 0.55, 0.55)
        self.deselect()
        corner = (a[0] + 12, a[1] + 12)                                             # inside the rectangle, outside the ellipse
        before = self.app.screenshot()

        self.app.key("ctrl+a")
        self.app.choose_menu("menuBar", "Object", "Clipping Mask", "Make")
        self.deselect()
        after = self.app.screenshot()
        self.assertTrue(differs(before.pixel(*corner), after.pixel(*corner)))

        self.app.key("ctrl+z")
        self.deselect()
        undone = self.app.screenshot()
        self.assertFalse(differs(before.pixel(*corner), undone.pixel(*corner)))

    def test_expand_appearance_leaves_the_picture_the_same(self):
        self.clear_canvas()
        a, b = self.draw("rectangleTool", 0.30, 0.30, 0.55, 0.50)
        self.set_field("strokeWeightField", "12")
        self.deselect()
        before = self.app.screenshot()

        self.app.click_at((a[0] + 30, a[1] + 30))                                   # select it again
        self.app.choose_menu("menuBar", "Object", "Expand Appearance")
        self.deselect()
        after = self.app.screenshot()

        changed = before.changed(after, a[0] - 20, a[1] - 20, b[0] + 20, b[1] + 20, tolerance=60, step=3)
        self.assertLess(changed, 40)
        self.app.key("ctrl+a")
        self.assertGreater(self.selection_width(), 0, "the expanded artwork is gone")


# ---- panels -------------------------------------------------------------------------------------------------------

class Panels(SketchCase):
    # layersTree is a custom view that automation cannot list, and the artboard combo's model has no plain value: the
    # name boxes show which layer / artboard is active.
    def test_the_add_layer_button_makes_a_new_active_layer(self):
        self.app.reveal("layerNameField", self.SIDEBAR)
        before = self.app.text_of("layerNameField")
        self.assertTrue(before)
        self.click_panel("addLayerButton")
        self.app.reveal("layerNameField", self.SIDEBAR)
        self.assertNotEqual(before, self.app.text_of("layerNameField"), "the new layer should become the active one")

    def test_the_add_artboard_button_makes_a_new_active_artboard(self):
        self.app.reveal("artboardNameField", self.SIDEBAR)
        before = self.app.text_of("artboardNameField")
        self.assertTrue(before)
        self.click_panel("addArtboardButton")
        self.app.reveal("artboardNameField", self.SIDEBAR)
        self.assertNotEqual(before, self.app.text_of("artboardNameField"), "the new artboard should become the active one")


# ---- files --------------------------------------------------------------------------------------------------------

class Files(SketchCase):
    def test_export_as_pdf_writes_a_pdf_through_the_save_dialog(self):
        self.clear_canvas()
        self.draw("rectangleTool", 0.30, 0.30, 0.55, 0.50)
        out = os.path.join(tempfile.gettempdir(), "sketch_guitest_export.pdf")
        if os.path.exists(out):
            os.remove(out)
        self.addCleanup(lambda: os.path.exists(out) and os.remove(out))

        # File > Export As PDF opens the native Save dialog: a modal loop, so the click must not wait for idle
        entries = self.app.open_menu("menuBar", "File")
        item = next((e for e in entries if e.text == "Export As PDF..."), None)
        self.assertIsNotNone(item, "no 'Export As PDF...' in the File menu")
        self.app.click_at(item.box.center, False, False)

        self.app.wait_for_dialog()               # only type into it once it is the foreground window
        time.sleep(0.5)
        self.app.type_raw(out)
        self.app.key_raw("enter")

        self.app.wait_until(lambda: os.path.exists(out) and os.path.getsize(out) > 100, "the PDF to be written", timeout=20)
        with open(out, "rb") as f:
            self.assertEqual(b"%PDF-1.4", f.read(8))


if __name__ == "__main__":
    unittest.main(verbosity=2)

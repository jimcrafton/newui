"""Tests of the parts of newui_guitest that need no app and no desktop: key chords, the BMP reader, rectangles and the
Win32 structure layouts. Safe to run anywhere (no input is sent, nothing is started):

    python -m unittest discover -s D:\\code\\newui-demos\\tools\\guitest\\tests -v
"""
import ctypes
import os
import struct
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from newui_guitest import _win32 as w                       # noqa: E402
from newui_guitest.app import Rect                          # noqa: E402
from newui_guitest.keys import VK, parse_chord              # noqa: E402
from newui_guitest.screenshot import ScreenShot, dark, differs  # noqa: E402


def make_bmp(width, height, pixels, bpp=32, bottom_up=True):
    """pixels[y][x] = (r, g, b); y = 0 is the TOP row of the picture."""
    stride = ((width * bpp + 31) // 32) * 4
    rows = []
    for y in range(height):
        row = bytearray(stride)
        for x in range(width):
            r, g, b = pixels[y][x]
            i = x * (bpp // 8)
            row[i:i + 3] = bytes((b, g, r))
        rows.append(bytes(row))
    if bottom_up:
        rows.reverse()
    data = b"".join(rows)
    header = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    info = struct.pack("<IiiHHIIiiII", 40, width, height if bottom_up else -height, 1, bpp, 0, len(data), 0, 0, 0, 0)
    return header + info + data


class KeyChords(unittest.TestCase):
    def test_a_bare_key(self):
        self.assertEqual(([], VK["enter"]), parse_chord("enter"))

    def test_modifiers_are_held_and_the_last_part_is_the_key(self):
        self.assertEqual(([VK["ctrl"], VK["shift"]], ord("Z")), parse_chord("ctrl+shift+z"))

    def test_case_and_spaces_do_not_matter(self):
        self.assertEqual(parse_chord("ctrl+z"), parse_chord(" Ctrl + Z "))

    def test_a_modifier_can_be_the_pressed_key(self):
        self.assertEqual(([], VK["shift"]), parse_chord("shift"))

    def test_an_integer_is_a_virtual_key_code(self):
        self.assertEqual(([], 0x1B), parse_chord(0x1B))

    def test_function_and_digit_keys(self):
        self.assertEqual(0x74, VK["f5"])
        self.assertEqual(ord("7"), VK["7"])

    def test_unknown_names_are_rejected(self):
        with self.assertRaises(ValueError):
            parse_chord("ctrl+banana")
        with self.assertRaises(ValueError):
            parse_chord("")


class Pictures(unittest.TestCase):
    PIXELS = [[(255, 0, 0), (0, 255, 0)],
              [(0, 0, 255), (10, 20, 30)]]

    def test_a_bottom_up_32_bit_bmp(self):
        shot = ScreenShot(make_bmp(2, 2, self.PIXELS))
        self.assertEqual((255, 0, 0), shot.pixel(0, 0))
        self.assertEqual((0, 255, 0), shot.pixel(1, 0))
        self.assertEqual((0, 0, 255), shot.pixel(0, 1))
        self.assertEqual((10, 20, 30), shot.pixel(1, 1))

    def test_a_top_down_24_bit_bmp_with_row_padding(self):
        shot = ScreenShot(make_bmp(2, 2, self.PIXELS, bpp=24, bottom_up=False))
        self.assertEqual((255, 0, 0), shot.pixel(0, 0))
        self.assertEqual((10, 20, 30), shot.pixel(1, 1))

    def test_pixels_are_addressed_in_screen_coordinates(self):
        shot = ScreenShot(make_bmp(2, 2, self.PIXELS), origin=(100, 50))
        self.assertEqual((10, 20, 30), shot.pixel(101, 51))
        self.assertEqual(ScreenShot.WHITE, shot.pixel(0, 0))          # outside the picture
        self.assertEqual(ScreenShot.WHITE, shot.pixel(102, 50))

    def test_count_and_changed(self):
        a = ScreenShot(make_bmp(2, 2, self.PIXELS))
        b = ScreenShot(make_bmp(2, 2, [[(255, 0, 0), (0, 255, 0)], [(0, 0, 255), (255, 255, 255)]]))
        self.assertEqual(1, a.count(0, 0, 2, 2, lambda p: p == (255, 0, 0), step=1))
        self.assertEqual(1, a.changed(b, 0, 0, 2, 2, step=1))

    def test_not_a_bmp_is_an_error(self):
        with self.assertRaises(ValueError):
            ScreenShot(b"definitely not a picture, but long enough to pass the size check............")

    def test_pixel_comparisons(self):
        self.assertTrue(differs((0, 0, 0), (255, 255, 255)))
        self.assertFalse(differs((100, 100, 100), (101, 100, 100)))
        self.assertTrue(dark((0, 0, 0)))
        self.assertFalse(dark((47, 95, 179)))     # a mid blue is not "dark": count the colour the app really uses


class Rectangles(unittest.TestCase):
    def test_center(self):
        self.assertEqual((15, 25), Rect(10, 20, 10, 10).center)

    def test_fully_inside(self):
        outer = Rect(0, 0, 100, 100)
        self.assertTrue(Rect(10, 10, 20, 20).inside(outer))
        self.assertFalse(Rect(10, 90, 20, 20).inside(outer))     # half clipped: a click on its centre would miss
        self.assertFalse(Rect(-5, 10, 20, 20).inside(outer))


@unittest.skipUnless(sys.platform == "win32" and ctypes.sizeof(ctypes.c_void_p) == 8, "x64 Windows layouts")
class Win32Layouts(unittest.TestCase):
    """SendInput rejects an INPUT of the wrong size, and CreateProcess / SetInformationJobObject read these structs
    blindly: a wrong field is a silent failure, so the sizes are pinned here."""

    def test_input_is_40_bytes(self):
        self.assertEqual(40, ctypes.sizeof(w.INPUT))

    def test_mouse_and_keyboard_input(self):
        self.assertEqual(32, ctypes.sizeof(w.MOUSEINPUT))
        self.assertEqual(24, ctypes.sizeof(w.KEYBDINPUT))

    def test_process_structures(self):
        self.assertEqual(104, ctypes.sizeof(w.STARTUPINFOW))
        self.assertEqual(24, ctypes.sizeof(w.PROCESS_INFORMATION))

    def test_job_limit_structures(self):
        self.assertEqual(64, ctypes.sizeof(w.JOBOBJECT_BASIC_LIMIT_INFORMATION))
        self.assertEqual(144, ctypes.sizeof(w.JOBOBJECT_EXTENDED_LIMIT_INFORMATION))

    def test_last_input_info(self):
        self.assertEqual(8, ctypes.sizeof(w.LASTINPUTINFO))

    def test_the_real_machine_has_a_virtual_desktop(self):
        self.assertGreater(w.GetSystemMetrics(w.SM_CXVIRTUALSCREEN), 0)


if __name__ == "__main__":
    unittest.main()

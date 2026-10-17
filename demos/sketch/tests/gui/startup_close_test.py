"""Smallest GUI test: sketch starts, is visible, in front and answering over the automation pipe; then the mouse clicks the window's
close (X) button and sketch must exit. Nothing else is touched: every step is checked first and the test stops if one fails.

    python startup_close_test.py [path\\to\\sketch.exe]

Leave the mouse alone while it runs (about 10 seconds).
"""
import ctypes
import ctypes.wintypes as wt
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from sketchdriver import Sketch, user32

HERE = os.path.dirname(os.path.abspath(__file__))
EXE = sys.argv[1] if len(sys.argv) > 1 else os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "build", "demos", "sketch", "Debug", "sketch.exe"))

results = []


def check(ok, what, detail=""):
    results.append(ok)
    print(("  ok   " if ok else "  FAIL ") + what + (("   (%s)" % (detail,)) if detail != "" else ""))
    return ok


def window_rect(hwnd):
    r = wt.RECT()
    # the visible frame, without the invisible resize border Windows adds (DWMWA_EXTENDED_FRAME_BOUNDS = 9)
    if ctypes.windll.dwmapi.DwmGetWindowAttribute(hwnd, 9, ctypes.byref(r), ctypes.sizeof(r)) != 0:
        user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right, r.bottom


def main():
    s = Sketch(EXE)       # starts sketch, says hello over the pipe, asks for the foreground
    try:
        root = s.find("rootview")
        check(root["visible"] and root["w"] > 100 and root["h"] > 100, "the window is visible and laid out", (root["w"], root["h"]))
        hwnd = s.hwnd()
        check(hwnd is not None, "sketch has its own visible top-level window")
        if hwnd is None:
            return 1
        title = ctypes.create_unicode_buffer(256)
        user32.GetWindowTextW(hwnd, title, 256)
        print("  window: %r" % title.value)
        check(user32.GetForegroundWindow() == hwnd, "sketch is the foreground window")
        check(bool(user32.IsWindowVisible(hwnd)) and not user32.IsIconic(hwnd), "it is shown and not minimized")
        left, top, right, bottom = window_rect(hwnd)
        print("  window rect: %d,%d - %d,%d" % (left, top, right, bottom))
        # the close button is the right-most caption button: about 46 x 32 device-independent pixels in the title bar
        dpi = user32.GetDpiForWindow(hwnd) or 96
        scale = dpi / 96.0
        x, y = right - int(23 * scale), top + int(15 * scale)
        under = user32.GetAncestor(user32.WindowFromPoint(wt.POINT(x, y)), 2)
        check(left <= x < right and top <= y < bottom and under == hwnd, "the X button's point is on sketch's title bar and nothing covers it", (x, y, under, hwnd))
        if not all(results):
            print("\nnot clicking: a check failed")
            return 1
        s.move(x, y)
        time.sleep(0.4)
        if not check(user32.GetForegroundWindow() == hwnd, "still the foreground window right before the click"):
            return 1
        s.click_at_raw(x, y)
        for _ in range(60):
            if s.proc.poll() is not None:
                break
            time.sleep(0.25)
        check(s.proc.poll() is not None, "sketch exits after the click on X", s.proc.poll())
    finally:
        s.close()
    print("\n%d ok, %d FAIL" % (sum(results), len(results) - sum(results)))
    return len(results) - sum(results)


if __name__ == "__main__":
    sys.exit(main())

"""A driver for sketch.exe's GUI automation (see automation.md at the repository root).

It starts sketch with `--automation <random pipe>`, talks JSON lines to it over the named pipe and sends REAL input with SendInput, so
the window must be in the foreground and nobody should touch the mouse while a test runs. Windows only; standard library only.
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import struct
import subprocess
import time
import uuid

user32 = ctypes.windll.user32
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)   # per-monitor: screen pixels must match the app's
except Exception:
    pass

INPUT_MOUSE, INPUT_KEYBOARD = 0, 1
MOUSEEVENTF_MOVE, MOUSEEVENTF_LEFTDOWN, MOUSEEVENTF_LEFTUP = 0x1, 0x2, 0x4
MOUSEEVENTF_RIGHTDOWN, MOUSEEVENTF_RIGHTUP, MOUSEEVENTF_WHEEL = 0x8, 0x10, 0x800
MOUSEEVENTF_ABSOLUTE, MOUSEEVENTF_VIRTUALDESK = 0x8000, 0x4000
KEYEVENTF_KEYUP, KEYEVENTF_UNICODE = 0x2, 0x4

VK = {"ctrl": 0x11, "shift": 0x10, "alt": 0x12, "enter": 0x0D, "esc": 0x1B, "tab": 0x09, "delete": 0x2E, "back": 0x08,
      "left": 0x25, "up": 0x26, "right": 0x27, "down": 0x28, "space": 0x20, "f2": 0x71}
for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789":
    VK[c.lower()] = ord(c)


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wt.LONG), ("dy", wt.LONG), ("mouseData", wt.DWORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wt.WORD), ("wScan", wt.WORD), ("dwFlags", wt.DWORD), ("time", wt.DWORD), ("dwExtraInfo", ctypes.c_size_t)]


class _U(ctypes.Union):
    _fields_ = [("mi", MOUSEINPUT), ("ki", KEYBDINPUT)]


class INPUT(ctypes.Structure):
    _anonymous_ = ("u",)
    _fields_ = [("type", wt.DWORD), ("u", _U)]


def _send(*inputs):
    arr = (INPUT * len(inputs))(*inputs)
    user32.SendInput(len(inputs), arr, ctypes.sizeof(INPUT))


class Shot:
    """A screenshot (BMP) read into memory: pixel(x, y) -> (r, g, b) in the window's own pixels."""

    def __init__(self, path):
        data = open(path, "rb").read()
        off = struct.unpack_from("<I", data, 10)[0]
        self.w, self.h, planes, bpp = struct.unpack_from("<iiHH", data, 18)
        self.bottom_up = self.h > 0
        self.h = abs(self.h)
        self.bpp = bpp
        self.data = data
        self.off = off
        self.stride = ((self.w * bpp + 31) // 32) * 4

    def pixel(self, x, y):
        x, y = int(x), int(y)
        row = (self.h - 1 - y) if self.bottom_up else y
        i = self.off + row * self.stride + x * (self.bpp // 8)
        b, g, r = self.data[i], self.data[i + 1], self.data[i + 2]
        return (r, g, b)

    def count(self, rect, predicate, step=2):
        x0, y0, x1, y1 = [int(v) for v in rect]
        n = 0
        for y in range(y0, y1, step):
            for x in range(x0, x1, step):
                if predicate(self.pixel(x, y)):
                    n += 1
        return n


class Sketch:
    def __init__(self, exe, args=(), workdir=None):
        self.pipe_name = "sketch_gui_" + uuid.uuid4().hex[:12]
        self.proc = subprocess.Popen([exe, "--automation", self.pipe_name] + list(args), cwd=workdir or os.path.dirname(exe))
        self._buf = b""
        self._id = 0
        self.pipe = None
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                self.pipe = open("\\\\.\\pipe\\" + self.pipe_name, "r+b", buffering=0)
                break
            except OSError:
                time.sleep(0.2)
        if self.pipe is None:
            raise RuntimeError("sketch did not open its automation pipe")
        hello = self.call("hello")
        if hello.get("protocol") != 1:
            raise RuntimeError("unknown automation protocol %r" % hello)
        self.idle()
        self.foreground()
        self.shots = 0

    # ---- the pipe -------------------------------------------------------------------------------------------
    def call(self, cmd, **kw):
        self._id += 1
        msg = dict(kw, cmd=cmd, id=self._id)
        self.pipe.write((json.dumps(msg) + "\n").encode("utf-8"))
        while b"\n" not in self._buf:
            chunk = self.pipe.read(65536)
            if not chunk:
                raise RuntimeError("the automation pipe closed (did sketch crash?)")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        reply = json.loads(line.decode("utf-8"))
        if not reply.get("ok"):
            raise RuntimeError("%s %s: %s" % (cmd, kw, reply.get("error")))
        return reply

    def idle(self):
        self.call("idle")

    def find(self, name):
        return self.call("findView", name=name)

    def center(self, name):
        r = self.find(name)
        return r["x"] + r["w"] / 2.0, r["y"] + r["h"] / 2.0

    def get(self, name, prop):
        return self.call("get", name=name, property=prop)["value"]

    def text(self, name):
        return str(self.get(name, "text"))

    def number(self, name):
        """The leading number of a field's text ("12.5 pt" -> 12.5)."""
        t = self.text(name).strip()
        n = ""
        for ch in t:
            if ch in "+-.0123456789":
                n += ch
            else:
                break
        return float(n) if n not in ("", "+", "-", ".") else float("nan")

    def screenshot(self, path=None):
        self.shots += 1
        path = path or os.path.join(os.environ.get("TEMP", "."), "sketch_gui_%s.bmp" % self.pipe_name)
        self.idle()
        self.call("screenshot", name="rootview", path=path)
        return Shot(path)

    def tree_rows(self, name, limit=200):
        return self.call("tree", name=name, limit=limit).get("rows", [])

    # ---- input ----------------------------------------------------------------------------------------------
    def hwnd(self):
        """sketch's own top-level window, found by its process id (never by what happens to be on screen at some point)."""
        found = []
        EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wt.HWND, wt.LPARAM)

        def visit(h, _):
            pid = wt.DWORD()
            user32.GetWindowThreadProcessId(h, ctypes.byref(pid))
            if pid.value == self.proc.pid and user32.IsWindowVisible(h) and user32.GetWindow(h, 4) == 0:   # GW_OWNER: top-level only
                found.append(h)
            return True
        user32.EnumWindows(EnumProc(visit), 0)
        return found[0] if found else None

    def foreground(self):
        h = self.hwnd()
        if h is None:
            raise RuntimeError("sketch has no visible window")
        user32.ShowWindow(h, 9)              # SW_RESTORE: not minimized
        user32.keybd_event(0x12, 0, 0, 0)    # the Alt trick that lets SetForegroundWindow succeed
        user32.SetForegroundWindow(h)
        user32.keybd_event(0x12, 0, KEYEVENTF_KEYUP, 0)
        time.sleep(0.5)

    def _abs(self, x, y):
        vx, vy = user32.GetSystemMetrics(76), user32.GetSystemMetrics(77)
        vw, vh = user32.GetSystemMetrics(78), user32.GetSystemMetrics(79)
        return int((x - vx) * 65535 / (vw - 1)), int((y - vy) * 65535 / (vh - 1))

    def move(self, x, y):
        ax, ay = self._abs(x, y)
        i = INPUT(type=INPUT_MOUSE)
        i.mi = MOUSEINPUT(ax, ay, 0, MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK, 0, 0)
        _send(i)
        time.sleep(0.03)

    def _button(self, flag):
        i = INPUT(type=INPUT_MOUSE)
        i.mi = MOUSEINPUT(0, 0, 0, flag, 0, 0)
        _send(i)
        time.sleep(0.03)

    def click(self, x, y, right=False, mods=()):
        self.move(x, y)
        for m in mods:
            self.key_down(m)
        self._button(MOUSEEVENTF_RIGHTDOWN if right else MOUSEEVENTF_LEFTDOWN)
        self._button(MOUSEEVENTF_RIGHTUP if right else MOUSEEVENTF_LEFTUP)
        for m in reversed(mods):
            self.key_up(m)
        self.idle()

    def click_at_raw(self, x, y):
        """A left click where the cursor already is (no idle wait: the click may close the app)."""
        self.move(x, y)
        self._button(MOUSEEVENTF_LEFTDOWN)
        self._button(MOUSEEVENTF_LEFTUP)

    def double_click(self, x, y):
        self.click(x, y)
        self.click(x, y)

    def drag(self, x0, y0, x1, y1, steps=12, mods=()):
        self.move(x0, y0)
        for m in mods:
            self.key_down(m)
        self._button(MOUSEEVENTF_LEFTDOWN)
        for k in range(1, steps + 1):
            self.move(x0 + (x1 - x0) * k / steps, y0 + (y1 - y0) * k / steps)
        self._button(MOUSEEVENTF_LEFTUP)
        for m in reversed(mods):
            self.key_up(m)
        self.idle()

    def drag_path(self, points, mods=()):
        self.move(*points[0])
        for m in mods:
            self.key_down(m)
        self._button(MOUSEEVENTF_LEFTDOWN)
        for p in points[1:]:
            self.move(*p)
        self._button(MOUSEEVENTF_LEFTUP)
        for m in reversed(mods):
            self.key_up(m)
        self.idle()

    def key_down(self, name):
        i = INPUT(type=INPUT_KEYBOARD)
        i.ki = KEYBDINPUT(VK[name], 0, 0, 0, 0)
        _send(i)

    def key_up(self, name):
        i = INPUT(type=INPUT_KEYBOARD)
        i.ki = KEYBDINPUT(VK[name], 0, KEYEVENTF_KEYUP, 0, 0)
        _send(i)

    def key(self, chord):
        """key("ctrl+z"), key("delete"), key("enter")."""
        names = chord.lower().split("+")
        for n in names:
            self.key_down(n)
            time.sleep(0.01)
        for n in reversed(names):
            self.key_up(n)
        time.sleep(0.03)
        self.idle()

    def type_text(self, text):
        for ch in text:
            d = INPUT(type=INPUT_KEYBOARD)
            d.ki = KEYBDINPUT(0, ord(ch), KEYEVENTF_UNICODE, 0, 0)
            u = INPUT(type=INPUT_KEYBOARD)
            u.ki = KEYBDINPUT(0, ord(ch), KEYEVENTF_UNICODE | KEYEVENTF_KEYUP, 0, 0)
            _send(d, u)
            time.sleep(0.01)
        self.idle()

    # ---- widgets --------------------------------------------------------------------------------------------
    def click_view(self, name, **kw):
        x, y = self.center(name)
        self.click(x, y, **kw)

    def set_field(self, name, text):
        """Click a TextField, replace its text and press Enter."""
        self.click_view(name)
        self.key("ctrl+a")
        self.type_text(text)
        self.key("enter")

    def menu(self, *path):
        """Opens menu-bar menus by their texts: menu("Object", "Clipping Mask", "Make")."""
        bar = self.call("menuBar", name="menuBar")
        top = [m for m in bar["menus"] if m["text"].replace("&", "") == path[0]][0]
        self.click(top["x"] + top["w"] / 2, top["y"] + top["h"] / 2)
        for depth, label in enumerate(path[1:], 1):
            items = None
            for _ in range(40):
                try:
                    items = self.call("popupMenu")["items"]
                    break
                except RuntimeError:
                    time.sleep(0.05)
            if items is None:
                raise RuntimeError("the menu did not open at " + label)
            item = [i for i in items if i["text"] == label]
            if not item:
                raise RuntimeError("no menu item %r among %r" % (label, [i["text"] for i in items]))
            item = item[0]
            self.click(item["x"] + min(item["w"] / 2, 60), item["y"] + item["h"] / 2)
            time.sleep(0.15)
        self.idle()

    def canvas_point(self, fx, fy):
        r = self.find("canvas")
        return r["x"] + r["w"] * fx, r["y"] + r["h"] * fy

    def tool(self, name):
        self.click_view(name)

    def close(self):
        try:
            if self.pipe:
                self.pipe.close()
        except Exception:
            pass
        if self.proc.poll() is None:
            self.proc.kill()
        self.proc.wait()

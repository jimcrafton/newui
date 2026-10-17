"""Drive a newui app from outside: real input through SendInput, questions answered over the app's automation pipe
(protocol: automation.md at the newui repository root). Windows only, standard library only.

    with App(r"D:\\build\\myapp.exe") as app:
        app.click("filterInput")
        app.type("Box")
        assert app.text_of("filterInput") == "Box"

Everything that sends input: waits until the person at the machine is idle, brings the app's window to the front, checks
that the target point really belongs to the app, and ends with wait_idle(). Errors are GuiTestError with a message that
says why (no such view, covered, no foreground, someone is using the mouse ...).
"""
import ctypes
import json
import os
import subprocess
import tempfile
import time
import uuid
from ctypes import wintypes
from dataclasses import dataclass, field
from typing import Callable, List, Optional, Tuple

from . import _win32 as w
from .keys import EXTENDED, parse_chord
from .screenshot import ScreenShot

w.make_dpi_aware()

Point = Tuple[int, int]


class GuiTestError(RuntimeError):
    """The driver could not do what the test asked (or the app refused). The message says why."""


class AutomationError(GuiTestError):
    """The app answered {"ok": false, "error": ...}."""


class UserBusyError(GuiTestError):
    """A person used the mouse or keyboard: the driver does not inject input over them. Rerun when the machine is idle."""


@dataclass
class Rect:
    x: float = 0
    y: float = 0
    w: float = 0
    h: float = 0
    visible: bool = False

    @property
    def center(self) -> Point:
        return (int(self.x + self.w / 2), int(self.y + self.h / 2))

    def inside(self, outer: "Rect") -> bool:
        """Fully inside `outer` (not even partly clipped)."""
        return self.y >= outer.y and self.y + self.h <= outer.y + outer.h and \
            self.x >= outer.x and self.x + self.w <= outer.x + outer.w


@dataclass
class MenuEntry:
    text: str
    separator: bool = False
    enabled: bool = True
    checked: bool = False
    has_submenu: bool = False
    box: Rect = field(default_factory=Rect)


@dataclass
class TreeRow:
    text: str
    depth: int
    expanded: bool
    has_children: bool
    box: Rect


class App:
    CONNECT_TIMEOUT = 20.0     # seconds to wait for the pipe after starting the app
    REPLY_TIMEOUT = 30.0       # seconds one request may go unanswered (a modal loop, or a hang)
    QUIET_PERIOD = 1.5         # the person must have left the mouse and keyboard alone this long before input is injected
    QUIET_TIMEOUT = 30.0       # ... and the driver waits this long for that before giving up
    # Turn the guard off only for an unattended session (a VM or a build machine): the runner's --no-guard does it.
    user_input_guard = True

    def __init__(self, exe: str, args=(), cwd: Optional[str] = None):
        exe = os.path.abspath(exe)
        if not os.path.isfile(exe):
            raise GuiTestError("no such executable: %s" % exe)
        self.exe = exe
        self.pipe_name = "newui_guitest_%d_%s" % (os.getpid(), uuid.uuid4().hex[:12])
        self._pipe = w.INVALID_HANDLE_VALUE
        self._process = None
        self._job = None
        self.pid = 0
        self._own_input = 0            # GetLastInputInfo tick right after our last injection (or the baseline)
        self._have_own_input = False
        self._buffer = b""

        command = subprocess.list2cmdline([exe] + [str(a) for a in args] + ["--automation", self.pipe_name])

        # The job object kills the app when this process or the App goes away, however the test ends.
        self._job = w.CreateJobObjectW(None, None)
        limits = w.JOBOBJECT_EXTENDED_LIMIT_INFORMATION()
        limits.BasicLimitInformation.LimitFlags = w.JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
        w.SetInformationJobObject(self._job, w.JobObjectExtendedLimitInformation, ctypes.byref(limits), ctypes.sizeof(limits))

        startup = w.STARTUPINFOW()
        startup.cb = ctypes.sizeof(startup)
        info = w.PROCESS_INFORMATION()
        buffer = ctypes.create_unicode_buffer(command)
        if not w.CreateProcessW(None, buffer, None, None, False, w.CREATE_SUSPENDED, None, cwd,
                                ctypes.byref(startup), ctypes.byref(info)):
            err = ctypes.get_last_error()
            w.CloseHandle(self._job)
            raise GuiTestError("could not start %s (error %d)" % (exe, err))
        w.AssignProcessToJobObject(self._job, info.hProcess)
        w.ResumeThread(info.hThread)
        w.CloseHandle(info.hThread)
        self._process = info.hProcess
        self.pid = info.dwProcessId

        try:
            self._connect()
            hello = self.ask("hello")
            if hello.get("protocol") != 1:
                raise GuiTestError("unexpected automation protocol: %r" % (hello,))
        except BaseException:
            self.close()
            raise

    # ---- lifetime ----------------------------------------------------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def __del__(self):
        try:
            self.close()
        except Exception:
            pass

    def close(self):
        """Closes the pipe and kills the app (via the job object). Safe to call twice."""
        if self._pipe != w.INVALID_HANDLE_VALUE:
            w.CloseHandle(self._pipe)
            self._pipe = w.INVALID_HANDLE_VALUE
        if self._job is not None:
            w.CloseHandle(self._job)       # kills the app
            self._job = None
        if self._process is not None:
            w.WaitForSingleObject(self._process, 5000)
            w.CloseHandle(self._process)
            self._process = None

    @property
    def running(self) -> bool:
        return self._process is not None and w.WaitForSingleObject(self._process, 0) == w.WAIT_TIMEOUT

    def _connect(self):
        path = "\\\\.\\pipe\\" + self.pipe_name
        deadline = time.monotonic() + self.CONNECT_TIMEOUT
        while True:
            handle = w.CreateFileW(path, w.GENERIC_READ | w.GENERIC_WRITE, 0, None, w.OPEN_EXISTING, 0, None)
            if handle != w.INVALID_HANDLE_VALUE:
                self._pipe = handle
                return
            if not self.running:
                raise GuiTestError("%s exited before opening its automation pipe (is it a newui app with the automation server built in?)" % self.exe)
            if time.monotonic() > deadline:
                raise GuiTestError("%s: timed out waiting for its automation pipe. This driver only works with apps built on the newui "
                                   "framework that start newui's automation server (--automation <pipe>)" % self.exe)
            time.sleep(0.05)

    # ---- the pipe ----------------------------------------------------------------------------------------------

    def ask(self, cmd: str, check: bool = True, timeout: Optional[float] = None, **fields) -> dict:
        """One request, one reply (a dict). ask("findView", name="canvas"). With check=False an {"ok": false} reply is
        returned instead of raised. Polls the pipe instead of blocking, so a modal loop in the app is a timeout, not a hang."""
        request = dict(fields, cmd=cmd)
        line = (json.dumps(request) + "\n").encode("utf-8")
        written = wintypes.DWORD(0)
        if not w.WriteFile(self._pipe, line, len(line), ctypes.byref(written), None):
            raise GuiTestError("the app's automation pipe is closed")
        deadline = time.monotonic() + (timeout if timeout is not None else self.REPLY_TIMEOUT)
        while b"\n" not in self._buffer:
            available = wintypes.DWORD(0)
            if not w.PeekNamedPipe(self._pipe, None, 0, None, ctypes.byref(available), None):
                raise GuiTestError("the app's automation pipe closed while waiting for a reply to %s" % cmd)
            if available.value == 0:
                if time.monotonic() > deadline:
                    raise GuiTestError("the app did not answer %r within %d s (a modal loop, or a hang?)" %
                                       (cmd, int(timeout if timeout is not None else self.REPLY_TIMEOUT)))
                time.sleep(0.002)
                continue
            chunk = ctypes.create_string_buffer(available.value)
            got = wintypes.DWORD(0)
            if not w.ReadFile(self._pipe, chunk, available.value, ctypes.byref(got), None) or got.value == 0:
                raise GuiTestError("the app's automation pipe closed while waiting for a reply to %s" % cmd)
            self._buffer += chunk.raw[:got.value]
        reply_line, _, self._buffer = self._buffer.partition(b"\n")
        reply = json.loads(reply_line.decode("utf-8"))
        if check and not reply.get("ok"):
            raise AutomationError("%s: %s" % (cmd, reply.get("error", reply)))
        return reply

    def wait_idle(self):
        """Blocks until the app has nothing pending (use instead of sleeping after input). Does NOT mean background work
        is done, and cannot be answered while a native menu or dialog is open."""
        self.ask("idle")

    # ---- queries -----------------------------------------------------------------------------------------------

    def find(self, name: str) -> Rect:
        """Screen rectangle of a named view (the whole bounds, even a part clipped away by a scrolling parent)."""
        r = self.ask("findView", name=name)
        return Rect(r["x"], r["y"], r["w"], r["h"], bool(r.get("visible")))

    def text_of(self, name: str) -> str:
        return self.ask("getText", name=name).get("text", "")

    def focused_name(self) -> str:
        return self.ask("focused").get("name", "")

    def prop(self, name: str, prop: str):
        """One reflected property of a view, as a Python value (number, bool or str)."""
        return self.ask("get", name=name, property=prop).get("value")

    def describe(self, name: str) -> dict:
        """{"type": ..., "properties": {...}} - every reflected property with a plain value."""
        return self.ask("describe", name=name)

    def scroll_info(self, name: str) -> dict:
        """ScrollView bars: vValue/vMin/vMax/vPage/vVisible and the thumb's screen rect vx/vy/vw/vh (same with h)."""
        return self.ask("scrollInfo", name=name)

    def segment(self, name: str, index: int) -> dict:
        return self.ask("segment", name=name, index=index)

    def caret(self, name: str) -> dict:
        return self.ask("caret", name=name)

    def tree(self, name: str, limit: int = 50) -> List[TreeRow]:
        reply = self.ask("tree", name=name, limit=limit)
        return [TreeRow(r.get("text", ""), r.get("depth", 0), bool(r.get("expanded")), bool(r.get("hasChildren")),
                        Rect(r["x"], r["y"], r["w"], r["h"], True)) for r in reply.get("rows", [])]

    def find_row(self, tree: str, text: str, limit: int = 200) -> TreeRow:
        for row in self.tree(tree, limit):
            if text in row.text:
                return row
        raise GuiTestError("no row containing %r in %s" % (text, tree))

    def screenshot(self, root: str = "rootview") -> ScreenShot:
        """A ScreenShot of a RootView rendered by the app itself. Pixels are addressed in SCREEN coordinates."""
        path = os.path.join(tempfile.gettempdir(), "newui_guitest_%s.bmp" % uuid.uuid4().hex[:12])
        try:
            self.ask("screenshot", name=root, path=path)
            origin = self.find(root)
            return ScreenShot.from_file(path, (int(origin.x), int(origin.y)))
        finally:
            try:
                os.remove(path)
            except OSError:
                pass

    def wait_until(self, condition: Callable[[], object], what: str = "the condition", timeout: float = 30.0,
                   interval: float = 0.1):
        """Polls condition() until it returns something truthy (returned). For background work, which idle does not cover."""
        deadline = time.monotonic() + timeout
        while True:
            value = condition()
            if value:
                return value
            if time.monotonic() > deadline:
                raise GuiTestError("timed out after %d s waiting for %s" % (int(timeout), what))
            time.sleep(interval)

    # ---- the person at the keyboard ----------------------------------------------------------------------------

    @staticmethod
    def _last_input_tick() -> int:
        info = w.LASTINPUTINFO()
        info.cbSize = ctypes.sizeof(info)
        w.GetLastInputInfo(ctypes.byref(info))
        return info.dwTime

    def _wait_quiet(self):
        """Start of every input operation: wait until nobody has touched the mouse or keyboard for QUIET_PERIOD, then
        take the baseline against which outside input is detected."""
        if not self.user_input_guard:
            return
        deadline = time.monotonic() + self.QUIET_TIMEOUT
        while True:
            last = self._last_input_tick()
            ours = self._have_own_input and last == self._own_input     # our own injection is not a person
            recent = ((w.GetTickCount() - last) & 0xFFFFFFFF) < self.QUIET_PERIOD * 1000
            if ours or not recent:
                break
            if time.monotonic() > deadline:
                raise UserBusyError("someone is using the mouse or keyboard; the GUI test will not inject input over them")
            time.sleep(0.1)
        self._own_input = self._last_input_tick()
        self._have_own_input = True

    def _require_no_interruption(self):
        if self.user_input_guard and self._have_own_input and self._last_input_tick() != self._own_input:
            raise UserBusyError("mouse or keyboard input from outside the test was detected; stopping")

    # ---- window handling ---------------------------------------------------------------------------------------

    def _main_window(self):
        found = []

        def visit(hwnd, _):
            pid = wintypes.DWORD(0)
            w.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
            # The app's frame is owned by a hidden helper window, so "visible and titled" picks it out (not "no owner").
            if pid.value == self.pid and w.IsWindowVisible(hwnd) and w.GetWindowTextLengthW(hwnd) > 0:
                found.append(hwnd)
                return False
            return True

        w.EnumWindows(w.WNDENUMPROC(visit), 0)
        return found[0] if found else None

    def bring_to_front(self):
        self._wait_quiet()
        window = None
        for _ in range(100):            # the pipe opens before the frame is shown
            window = self._main_window()
            if window:
                break
            time.sleep(0.05)
        if not window:
            raise GuiTestError("the app has no visible window")
        if w.IsIconic(window):
            w.ShowWindow(window, w.SW_RESTORE)
        # Windows only lets the foreground thread hand over the foreground: attach to its input queue for the call.
        current = w.GetForegroundWindow()
        theirs = w.GetWindowThreadProcessId(current, None) if current else 0
        ours = w.GetCurrentThreadId()
        if theirs and theirs != ours:
            w.AttachThreadInput(ours, theirs, True)
        w.SetForegroundWindow(window)
        if theirs and theirs != ours:
            w.AttachThreadInput(ours, theirs, False)

        def in_front():
            return w.GetAncestor(w.GetForegroundWindow(), w.GA_ROOT) == window

        for _ in range(3):
            if in_front():
                break
            for _ in range(20):
                if in_front():
                    break
                time.sleep(0.025)
            if not in_front():
                # The foreground lock sometimes refuses; minimizing and restoring the window gets past it.
                w.ShowWindow(window, w.SW_MINIMIZE)
                w.ShowWindow(window, w.SW_RESTORE)
                w.SetForegroundWindow(window)
        if not in_front():
            raise GuiTestError("could not bring the app to the foreground")

    def _require_ours_at(self, point: Point):
        hit = w.WindowFromPoint(wintypes.POINT(int(point[0]), int(point[1])))
        pid = wintypes.DWORD(0)
        if hit:
            w.GetWindowThreadProcessId(hit, ctypes.byref(pid))
        if pid.value != self.pid:
            raise GuiTestError("another window covers (%d,%d)" % (point[0], point[1]))

    def foreground_class(self) -> Tuple[str, bool]:
        """(window class of the foreground window, whether it belongs to the app). A native file dialog is "#32770"."""
        window = w.GetForegroundWindow()
        if not window:
            return "", False
        name = ctypes.create_unicode_buffer(128)
        w.GetClassNameW(window, name, 128)
        pid = wintypes.DWORD(0)
        w.GetWindowThreadProcessId(window, ctypes.byref(pid))
        return name.value, pid.value == self.pid

    # ---- raw input ---------------------------------------------------------------------------------------------

    def _send(self, inputs):
        self._require_no_interruption()
        array = (w.INPUT * len(inputs))(*inputs)
        if w.SendInput(len(inputs), array, ctypes.sizeof(w.INPUT)) != len(inputs):
            raise GuiTestError("SendInput was blocked (error %d)" % ctypes.get_last_error())
        self._own_input = self._last_input_tick()
        self._have_own_input = True

    @staticmethod
    def _mouse(flags, data=0, dx=0, dy=0):
        i = w.INPUT()
        i.type = w.INPUT_MOUSE
        i.mi.dx, i.mi.dy, i.mi.mouseData, i.mi.dwFlags = dx, dy, data & 0xFFFFFFFF, flags
        return i

    @staticmethod
    def _key(vk, up=False, unicode_char=None):
        i = w.INPUT()
        i.type = w.INPUT_KEYBOARD
        if unicode_char is not None:
            i.ki.wScan = ord(unicode_char)
            i.ki.dwFlags = w.KEYEVENTF_UNICODE | (w.KEYEVENTF_KEYUP if up else 0)
        else:
            i.ki.wVk = vk
            i.ki.wScan = w.MapVirtualKeyW(vk, w.MAPVK_VK_TO_VSC)
            i.ki.dwFlags = (w.KEYEVENTF_KEYUP if up else 0) | (w.KEYEVENTF_EXTENDEDKEY if vk in EXTENDED else 0)
        return i

    def _move_to(self, point: Point):
        left, top = w.GetSystemMetrics(w.SM_XVIRTUALSCREEN), w.GetSystemMetrics(w.SM_YVIRTUALSCREEN)
        width, height = w.GetSystemMetrics(w.SM_CXVIRTUALSCREEN), w.GetSystemMetrics(w.SM_CYVIRTUALSCREEN)
        dx = int((point[0] - left) * 65535.0 / (width - 1) + 0.5)
        dy = int((point[1] - top) * 65535.0 / (height - 1) + 0.5)
        self._send([self._mouse(w.MOUSEEVENTF_MOVE | w.MOUSEEVENTF_ABSOLUTE | w.MOUSEEVENTF_VIRTUALDESK, 0, dx, dy)])

    def _point(self, target) -> Point:
        return target if isinstance(target, tuple) else self.find(target).center

    # ---- input -------------------------------------------------------------------------------------------------

    def click_at(self, point: Point, to_front: bool = True, settle: bool = True, button: str = "left"):
        """A real click at a screen point. to_front=False inside an open menu (taking the foreground would close it);
        settle=False when the click opens a modal loop (a menu, a dialog) that idle cannot answer in."""
        if to_front:
            self.bring_to_front()
        else:
            self._wait_quiet()
        self._require_ours_at(point)
        self._move_to(point)
        down, up = (w.MOUSEEVENTF_RIGHTDOWN, w.MOUSEEVENTF_RIGHTUP) if button == "right" else \
            (w.MOUSEEVENTF_LEFTDOWN, w.MOUSEEVENTF_LEFTUP)
        self._send([self._mouse(down), self._mouse(up)])
        if settle:
            self.wait_idle()

    def click(self, target, **kw):
        """Click the centre of a named view (or a screen point given as an (x, y) tuple)."""
        self.click_at(self._point(target), **kw)

    def double_click(self, target):
        """Both clicks in one batch, so they are a real double click."""
        point = self._point(target)
        self.bring_to_front()
        self._require_ours_at(point)
        self._move_to(point)
        self._send([self._mouse(w.MOUSEEVENTF_LEFTDOWN), self._mouse(w.MOUSEEVENTF_LEFTUP),
                    self._mouse(w.MOUSEEVENTF_LEFTDOWN), self._mouse(w.MOUSEEVENTF_LEFTUP)])
        self.wait_idle()

    def type(self, text: str):
        """Types characters (layout independent: one Unicode key event pair per character)."""
        self.bring_to_front()
        for c in text:
            self._send([self._key(0, unicode_char=c), self._key(0, up=True, unicode_char=c)])
        self.wait_idle()

    def key(self, chord):
        """key("ctrl+z"), key("ctrl+shift+s"), key("delete"), key("enter"), or a virtual-key code. One batch."""
        modifiers, vk = parse_chord(chord)
        self.bring_to_front()
        events = [self._key(m) for m in modifiers] + [self._key(vk), self._key(vk, up=True)] + \
                 [self._key(m, up=True) for m in reversed(modifiers)]
        self._send(events)
        self.wait_idle()

    def wheel(self, target, notches: int):
        """Positive = up (away from the user)."""
        point = self._point(target)
        self.bring_to_front()
        self._require_ours_at(point)
        self._move_to(point)
        self._send([self._mouse(w.MOUSEEVENTF_WHEEL, notches * w.WHEEL_DELTA)])
        self.wait_idle()

    def drag(self, start, end, steps: int = 10):
        """Left button down at `start`, several moves, up at `end` (points, or a view name for the start)."""
        a = self._point(start)
        b = end if isinstance(end, tuple) else self.find(end).center
        self.bring_to_front()
        self._require_ours_at(a)
        self._move_to(a)
        self._send([self._mouse(w.MOUSEEVENTF_LEFTDOWN)])
        try:
            for i in range(1, steps + 1):       # a real drag passes through points; some controls need the moves
                self._move_to((a[0] + (b[0] - a[0]) * i // steps, a[1] + (b[1] - a[1]) * i // steps))
                self.wait_idle()
        except BaseException:
            # never leave the button held, whatever stopped the drag
            arr = (w.INPUT * 1)(self._mouse(w.MOUSEEVENTF_LEFTUP))
            w.SendInput(1, arr, ctypes.sizeof(w.INPUT))
            raise
        self._send([self._mouse(w.MOUSEEVENTF_LEFTUP)])
        self.wait_idle()

    def drag_by(self, name: str, dx: int, dy: int):
        start = self.find(name).center
        self.drag(start, (start[0] + dx, start[1] + dy))

    # ---- controls ----------------------------------------------------------------------------------------------

    def click_segment(self, name: str, index: int):
        s = self.segment(name, index)
        self.click_at((int(s["x"] + s["w"] / 2), int(s["y"] + s["h"] / 2)))

    def click_row(self, tree: str, text: str):
        row = self.find_row(tree, text)       # a bit in from the left: the toggle sits at the indent, the label further right
        self.click_at((int(row.box.x + row.box.w * 0.6), row.box.center[1]))

    def double_click_row(self, tree: str, text: str):
        row = self.find_row(tree, text)
        self.double_click((int(row.box.x + row.box.w * 0.6), row.box.center[1]))

    def toggle_row(self, tree: str, text: str, indent_per_level: int = 16):
        """Opens or closes a row by clicking its toggle triangle (at the row's depth indent)."""
        row = self.find_row(tree, text)
        self.click_at((int(row.box.x + 12 + indent_per_level * (row.depth - 1)), row.box.center[1]))

    def reveal(self, name: str, scroller: str, step: int = 2, attempts: int = 40):
        """Scrolls `scroller` (a named ScrollView) with the wheel until `name` is fully inside its visible part. A click on
        the clipped edge of a control, or below the visible area, silently does nothing, so call this before clicking
        anything inside a scrolling panel."""
        for _ in range(attempts):
            r, s = self.find(name), self.find(scroller)
            if r.inside(s):
                return
            self.wheel(scroller, step if r.y < s.y else -step)
        raise GuiTestError("could not scroll %r into view in %r" % (name, scroller))

    # ---- native menus (modal loops) ----------------------------------------------------------------------------

    def _popup_entries(self) -> Optional[List[MenuEntry]]:
        reply = self.ask("popupMenu", check=False)
        if not reply.get("ok"):
            return None
        return [MenuEntry(i.get("text", ""), bool(i.get("separator")), bool(i.get("enabled", True)), bool(i.get("checked")),
                          bool(i.get("hasSubmenu")), Rect(i["x"], i["y"], i["w"], i["h"], True))
                for i in reply.get("items", [])]

    def open_menu(self, bar: str, menu: str) -> List[MenuEntry]:
        """Opens a menu bar's top-level menu by clicking its button and returns the dropdown's items. While it is open
        the app is in a modal loop: only popupMenu is answered (and the methods here)."""
        for button in self.ask("menuBar", name=bar).get("menus", []):
            if button.get("text", "").replace("&", "") != menu:
                continue
            self.click_at((int(button["x"] + button["w"] / 2), int(button["y"] + button["h"] / 2)), True, False)
            entries = self.wait_until(self._popup_entries, "the %r menu to open" % menu, timeout=5.0, interval=0.05)
            return entries
        raise GuiTestError("no menu named %r in %s" % (menu, bar))

    def choose_menu(self, bar: str, *path: str):
        """choose_menu("menuBar", "Object", "Clipping Mask", "Make"): opens the menu and clicks through the submenus."""
        top, rest = path[0], path[1:]
        if not rest:
            raise GuiTestError("choose_menu needs a menu and an item")
        entries = self.open_menu(bar, top)
        for level, label in enumerate(rest):
            entry = next((e for e in entries if not e.separator and e.text == label), None)
            if entry is None:
                self.close_menu()
                raise GuiTestError("no item %r among %r" % (label, [e.text for e in entries if not e.separator]))
            if not entry.enabled:
                self.close_menu()
                raise GuiTestError("the menu item %r is disabled" % label)
            self.click_at(entry.box.center, False, False)       # inside an open menu: no foreground change, no idle
            if level == len(rest) - 1:
                self.wait_idle()
                return
            nxt = rest[level + 1]
            try:
                # The popup still shows the parent menu for a moment: wait until the next label is on offer.
                entries = self.wait_until(lambda: self._offered(nxt), "the submenu of %r" % label, timeout=5.0, interval=0.05)
            except GuiTestError:
                self.close_menu()
                raise

    def _offered(self, label: str) -> Optional[List[MenuEntry]]:
        entries = self._popup_entries()
        return entries if entries and any(e.text == label for e in entries) else None

    def close_menu(self):
        """Escape, once per open menu level, until no menu is left; then waits for idle."""
        for _ in range(8):
            self._wait_quiet()      # no bring_to_front: the menu is open and taking the foreground would close it anyway
            self._send([self._key(0x1B), self._key(0x1B, up=True)])
            time.sleep(0.1)
            if self._popup_entries() is None:
                break
        self.wait_idle()

    # ---- native dialogs ----------------------------------------------------------------------------------------
    # A native file dialog is another top-level window of the app. The input methods above bring the MAIN window to the
    # front first, which would pull it over the dialog: use these while a dialog is up.

    def wait_for_dialog(self, timeout: float = 6.0) -> str:
        """Waits until a standard dialog ("#32770") belonging to the app is the foreground window; returns its class."""
        def dialog():
            cls, ours = self.foreground_class()
            return cls if cls == "#32770" and ours else None
        return self.wait_until(dialog, "a dialog to come to the front", timeout=timeout, interval=0.1)

    def type_raw(self, text: str):
        """Types into whatever has the focus WITHOUT touching the foreground (for native dialogs)."""
        self._wait_quiet()
        for c in text:
            self._send([self._key(0, unicode_char=c), self._key(0, up=True, unicode_char=c)])
            time.sleep(0.01)

    def key_raw(self, chord):
        """A key chord to whatever has the focus WITHOUT touching the foreground (for native dialogs)."""
        modifiers, vk = parse_chord(chord)
        self._wait_quiet()
        self._send([self._key(m) for m in modifiers] + [self._key(vk), self._key(vk, up=True)] +
                   [self._key(m, up=True) for m in reversed(modifiers)])

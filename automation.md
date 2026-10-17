# GUI automation

An opt-in, in-process server that lets a separate driver process test a newui app the way a user
meets it: the driver sends real input with `SendInput` and asks the app, over a named pipe, where
things are and what they show.

The server answers questions; it does not inject input. Input comes from the driver (Win32
`SendInput`), so focus, capture, scrollbars and DPI go through the real message pump.

## It is off unless the app writes code to turn it on

Three separate things must all be true before anything listens:

1. **The app links `newui_automation`.** It is its own library, not part of `newui`. An app that
   does not link it has no automation code at all.
2. **The app calls `Automation::expose(rootView)`** for each window a driver may see. Views in
   windows that were not exposed cannot be found.
3. **The app calls `Automation::start(pipeName, runLoop)`** itself, typically from a
   `--automation <pipe>` command-line option.

Wrap the app-side code in an `#ifdef` of your own and gate the link in CMake, so a shipping build
contains none of it.

## Setting it up in an app

CMake:

```cmake
option(MYAPP_GUI_AUTOMATION "Build with newui's automation server" ON)
if(MYAPP_GUI_AUTOMATION)
    target_link_libraries(myapp PRIVATE newui_automation)
    target_compile_definitions(myapp PRIVATE MYAPP_GUI_AUTOMATION)
endif()
```

C++:

```cpp
#ifdef MYAPP_GUI_AUTOMATION
#include <newui/automation.h>
#endif

// after the frame/root view exist, before app.run():
#ifdef MYAPP_GUI_AUTOMATION
if (!automationPipe.empty()) {
    newui::Automation::expose(frame.rootView());
    newui::Automation::start(automationPipe, app.runLoop());
}
#endif

// after app.run() returns:
#ifdef MYAPP_GUI_AUTOMATION
newui::Automation::stop();
#endif
```

`expose()` may be called any time, for any number of windows (a popup or tool window is exposed
the same way). An exposed window is forgotten automatically when it is destroyed; `unexpose()`
removes it earlier.

Views are found by **name** (`View::setName()`, or the name in a `.newui` file), so give the views a
test needs a name. A view without one cannot be addressed.

## Enabling and disabling

| To | Do |
|----|----|
| Turn it off for good | Don't link `newui_automation` and don't compile the app-side code (build with the app's option OFF). Nothing is left in the binary. |
| Keep it compiled in but idle | Run the app without `--automation`. No pipe is created, no thread started. |
| Turn it on for a run | Start the app with `--automation <pipe-name>`. |
| Stop it mid-run | `Automation::stop()` (also called at exit). |

In this repo, the testharness uses the CMake option `CPPTOOLS_GUI_AUTOMATION` (default ON) and the
`CPPTOOLS_GUI_AUTOMATION` define. Configure with `-DCPPTOOLS_GUI_AUTOMATION=OFF` to build it
without.

## Security

- The pipe is `\\.\pipe\<pipe-name>`, created with `PIPE_REJECT_REMOTE_CLIENTS` (no network
  clients) and a protected ACL that grants access to the current user's SID only.
- It is created with `FILE_FLAG_FIRST_PIPE_INSTANCE`, so `start()` fails rather than joining a pipe
  another process already made.
- Everything the app exposes can be read through it (view tree, text, screenshots), which is why it
  is opt-in at build time. Use a random pipe name per run (the driver should generate one and pass
  it on the command line) and never ship a build with it on.
- One client at a time.

## Protocol (version 1)

One JSON object per line in each direction (UTF-8, `\n` terminated). Every reply has `"ok"`; a
request's `"id"` (a number) is echoed back when given. A failure is
`{"ok":false,"error":"..."}`.

| Request | Reply fields |
|---------|--------------|
| `{"cmd":"hello"}` | `protocol` |
| `{"cmd":"scrollInfo","name":"explorerScroll"}` | For a `ScrollView`: per bar (prefix `v` or `h`) `Value`, `Min`, `Max`, `Page`, `Visible`, and the thumb's screen rect `x`, `y`, `w`, `h` (e.g. `vValue`, `vx`). Lets a test aim a scrollbar drag and check the position |
| `{"cmd":"menuBar","name":"testharnessMenuBar"}` | For a `MenuBar`: `menus`, each top-level button as `{"text","x","y","w","h"}` (screen pixels) |
| `{"cmd":"popupMenu"}` | The native popup menu open right now (a menu bar dropdown or context menu): `items`, each `{"text","separator","enabled","checked","hasSubmenu","x","y","w","h"}`; text has the `&` and shortcut removed. Native menus run their own modal loop, so `idle` does not answer while one is open; poll this instead. Error `no menu is open` otherwise |
| `{"cmd":"caret","name":"cppEditorText"}` | For a `TextControl`: the caret's `line` and `column` (1-based) and `offset` |
| `{"cmd":"describe","name":"filterInput"}` | `type` and `properties`: every reflected property of the view that has a plain value (bool, number, string), by name. Works for any control newui reflects, with no per-control code |
| `{"cmd":"get","name":"filterInput","property":"text"}` | `value` of one reflected property |
| `{"cmd":"tree","name":"explorerTree","limit":50}` | For a `TreeView`: `count` (visible rows) and `rows`, the first `limit` (default 50) as `{"depth":n,"text":"...","expanded":b,"hasChildren":b,"x":..,"y":..,"w":..,"h":..}` (the row's screen rect; the text is the model's `value()` for the row's path when it is a string, else empty) |
| `{"cmd":"segment","name":"explorerModes","index":1}` | For a `SegmentedControl`: `count`, `selected` (index), and that segment's `label` and screen rect `x`, `y`, `w`, `h` |
| `{"cmd":"findView","name":"filterEdit"}` | `x`, `y`, `w`, `h` (screen pixels of the view's whole bounds, ready for `SendInput`), `visible` |
| `{"cmd":"focused"}` | `name`, `type` of the focused view (the foreground window wins; empty if none) |
| `{"cmd":"getText","name":"editor"}` | `text` (only for `TextControl`; other views report an error) |
| `{"cmd":"screenshot","name":"mainRoot","path":"C:\\\\shot.bmp"}` | `path`. `name` must be a RootView. Written with Blend2D; use `.bmp` (PNG not verified) |
| `{"cmd":"idle"}` | replies once the run loop has nothing pending: use it instead of sleeping after sending input |

Every request runs on the app's UI thread, so it never races the views.

The protocol version is `Automation::kProtocolVersion`; the driver should send `hello` first and
refuse a version it doesn't know.

## Trying it by hand

Start the app, then talk to it from PowerShell:

```powershell
Start-Process build\Debug\testharness.exe -ArgumentList '--automation','my_pipe'
$c = New-Object System.IO.Pipes.NamedPipeClientStream('.','my_pipe',[System.IO.Pipes.PipeDirection]::InOut)
$c.Connect(5000)
$w = New-Object System.IO.StreamWriter($c); $w.AutoFlush = $true
$r = New-Object System.IO.StreamReader($c)
$w.WriteLine('{"id":1,"cmd":"hello"}'); $r.ReadLine()
# {"id":1,"ok":true,"protocol":1}
```

## Writing a driver

The app side (above) only answers questions. A **driver** is a separate program that starts the app, sends real
input, and asks the app what it shows. It needs nothing from newui: the protocol is the only coupling, so a driver can
be written in any language that can open a Windows named pipe and call `SendInput`. This repo's reference driver is
**Python** (`tools/guitest`, the `newui_guitest` package, standard library only): use it as it is, and write an app's tests
and helpers in Python on top of it. Python is the canonical driver because anything that does not fit a config file
(where the exe is, "draw a rectangle", how to find a dialog) is a few lines of test code rather than a C++ rebuild.
A driver is a test tool, not part of the app: it needs a desktop session nobody is using.

### What a driver has to do

| # | Job | How |
|---|-----|-----|
| 1 | **Be per-monitor DPI aware** | `SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)` before anything else. The app reports physical screen pixels; a DPI-unaware driver sees scaled coordinates and clicks in the wrong place. |
| 2 | **Start the app** | `CreateProcess(exe --automation <pipe>)` with a **random** pipe name per run (the pipe is first-instance, so a clash makes `start()` fail). Create the process suspended, put it in a **job object** with `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE`, then resume it: the app dies with the driver however the test ends (crash, assert, Ctrl+C). |
| 3 | **Connect to the pipe** | `CreateFile("\\\\.\\pipe\\<pipe>", GENERIC_READ \| GENERIC_WRITE, ...)`, retried every ~50 ms for up to ~20 s. Give up early if the process has exited. The pipe opens a moment *after* the process starts, and the window is shown even later. |
| 4 | **Say `hello`** | Send `{"cmd":"hello"}` first and refuse a `protocol` you do not know (now `1`). |
| 5 | **Ask, with a timeout** | One request line, one reply line (`\n` terminated, UTF-8). Do **not** block in `ReadFile`: poll `PeekNamedPipe` and time out (30 s is plenty), because an app inside a modal loop (an open native menu, a file dialog) answers nothing and the test must be able to fail instead of hanging. |
| 6 | **Find the window** | `EnumWindows` for the app's process id, **visible and titled** (`IsWindowVisible && GetWindowTextLength > 0`). Do not require "no owner": newui's frame is owned by a hidden helper window. |
| 7 | **Bring it to the front** | `SetForegroundWindow` only works for the foreground thread: `AttachThreadInput` to the current foreground window's thread around the call, restore a minimized window first, then verify `GetAncestor(GetForegroundWindow(), GA_ROOT) == window`. Throw if it did not take. |
| 8 | **Locate before acting** | Never hard-code coordinates. `findView` gives the screen rectangle of a named view; click its centre. Before every click check that `WindowFromPoint(point)` belongs to the app's process (nothing covers it). |
| 9 | **Send real input** | `SendInput`. Mouse: absolute coordinates are 0..65535 across the *virtual desktop* (`MOUSEEVENTF_ABSOLUTE \| MOUSEEVENTF_VIRTUALDESK`, normalised with `SM_XVIRTUALSCREEN/SM_CXVIRTUALSCREEN`...). Text: `KEYEVENTF_UNICODE`, one down/up pair per character. Shortcuts: `VK_*` down for the modifiers, the key down/up, modifiers up. A drag is down, **several** moves (controls that capture the mouse need them), up. A double click is one batch, so the two clicks are close enough in time. |
| 10 | **Wait with `idle`, never `Sleep`** | End every input call with `{"cmd":"idle"}`. `idle` means the UI loop has nothing pending, **not** that background work (indexing, parsing, loading) is done: for that, poll the state you expect. |
| 11 | **Stay out of the user's way** | Share the real mouse politely (see "Protecting the person at the keyboard"). |
| 12 | **Report why** | Throw an error that says what failed ("no such view 'x'", "another window covers (x,y)", "could not bring the app to the foreground"). Test failures are read by people. |

Everything else (menus, tree rows, scrollbars, segments, dialogs) is a combination of these plus the query commands
in the protocol table.

### Protecting the person at the keyboard

`SendInput` drives the *real* cursor and keyboard. A driver that ignores that will click into whatever window is in
front. The reference driver (`newui_guitest.App`) therefore:

- waits until nobody has touched the mouse or keyboard for 1.5 s (`GetLastInputInfo`) before it injects anything, and gives
  up with an error after 30 s;
- remembers the input clock after each of its own injections, and **aborts the test** if the clock moves for any other
  reason (releasing a held mouse button first);
- is switchable off (`--no-guard` on the command line, or `App.user_input_guard = False`) for a VM or build
  machine with nobody at it.

Treat "another window covers ...", "could not bring the app to the foreground" and "input from outside" as *the person
was busy*, not as a bug in the app: rerun when the machine is idle. Make the first run of any new driver read-only
(`hello`, `findView`, `screenshot`) before it sends any input.

### Sample implementation

A complete test with the Python driver (`tools/guitest`). It clicks a named view with a real click, types, reads the text
back, drives a menu and compares screenshots. It does not say which app it tests: the person running it does (next section).

```python
import sys

sys.path.insert(0, r"D:\code\newui-demos\tools\guitest")
import newui_guitest
from newui_guitest import AppTestCase


class FilterTests(AppTestCase):                      # a fresh app per test, killed however the test ends
    def test_typing_fills_the_filter(self):
        self.app.click("filterInput")                       # find -> covered-point check -> SendInput -> wait for idle
        self.app.type("Box")
        self.assertEqual("Box", self.app.text_of("filterInput"))

    def test_a_menu_command_changes_the_picture(self):
        before = self.app.screenshot()
        self.app.choose_menu("menuBar", "View", "Zoom In")  # nested menus work: choose_menu(bar, "A", "B", "C")
        after = self.app.screenshot()
        r = self.app.find("canvas")
        self.assertGreater(before.changed(after, r.x, r.y, r.x + r.w, r.y + r.h), 0)

    def test_background_work_is_polled_not_slept(self):
        # idle means the UI loop is empty, not that indexing has finished
        self.app.wait_until(lambda: self.app.tree("itemsTree", 10), "the tree to fill")


if __name__ == "__main__":
    sys.exit(newui_guitest.main())                          # parses --exe, --arg, -k, --list ... and runs this file
```

Run it:

```
python test_myapp.py --exe D:\build\myapp.exe
```

`tools/guitest/README.md` has the whole API. A large app subclasses `AppTestCase` once for its own helpers: see
`demos/sketch/tests/gui/test_sketch_gui.py`.

### Launching the tests

The harness is a command line. Everything you need to control is an option, not an environment variable or an edit to the test.
Start in the driver's folder (or put `D:\code\newui-demos\tools\guitest` on `PYTHONPATH`):

```
cd D:\code\newui-demos\tools\guitest

python -m newui_guitest check D:\build\myapp.exe --view canvas        # 1. FIRST: read-only smoke check, sends no input
python -m newui_guitest test D:\code\myapp\tests\gui --exe D:\build\myapp.exe --list     # 2. list the tests, starts nothing
python -m newui_guitest test D:\code\myapp\tests\gui\test_myapp.py --exe D:\build\myapp.exe    # 3. run (drives the real mouse!)
```

A test file can be its own entry point, which is how most people will run it:

```python
import newui_guitest
...
if __name__ == "__main__":
    sys.exit(newui_guitest.main())
```
```
python D:\code\myapp\tests\gui\test_myapp.py --exe D:\build\myapp.exe -k Typing
```

`<tests>` is any mix of a test file, a folder (discovers `test*.py`) or a dotted name (`test_myapp.Typing.test_x`); with none it
discovers in the current folder. A test class may name a default `exe` for when `--exe` is not given.

| Option | Meaning |
|---|---|
| `--exe PATH` | the app to test (overrides the test class's `exe`) |
| `--arg ARG` | an extra command-line argument for the app; repeat for several (`--arg=--demo --arg=--small`) |
| `--cwd DIR` | working directory of the app |
| `-k TEXT` | only tests whose name contains TEXT; repeatable |
| `--list` | print the tests and exit; starts nothing |
| `-f`, `--failfast` / `-q`, `--quiet` | stop at the first failure / one dot per test |
| `--no-guard` | do not wait for the person to leave the mouse alone and do not abort on their input (an unattended VM or build machine only) |
| `--quiet-period S` / `--quiet-timeout S` | how long the mouse and keyboard must be untouched before input is sent (1.5 s) / how long to wait for that (30 s) |
| `--connect-timeout S` / `--reply-timeout S` | wait for the app's pipe after starting it (20 s) / for one reply (30 s); raise for a slow app |

The exit code is 0 when every test passed, 1 on a failure, 2 when no app was given. `python -m newui_guitest test --help` lists them all.
Tests that send input need a desktop nobody is using, the app window uncovered and hands off the mouse and keyboard.

### A driver in another language

The Python driver is the reference; a driver in another language only has to do the twelve jobs in the table above. As a
size guide, this is the smallest useful one in C++ (about 200 lines, Win32 only, no newui dependency). It is deliberately
small: replies are scanned by key rather than parsed (fine for flat replies; use a real JSON parser for arrays such as menu
items and tree rows). Build with `cl /EHsc /std:c++17 mini_driver.cpp user32.lib`.

```cpp
// mini_driver.cpp - the smallest useful driver for a newui app: launch it, talk to its automation pipe, click a named view
// with real input, read a text box back. Win32 only. Build: cl /EHsc /std:c++17 mini_driver.cpp user32.lib
#include <windows.h>

#include <chrono>
#include <stdexcept>
#include <string>

namespace {

// 1. Pixel coordinates from newui are physical: be per-monitor DPI aware before anything else.
struct DpiAwareness {
    DpiAwareness() { ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2); }
} dpiAwareness;

std::wstring widen(const std::string& s) {
    int n = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
    return w;
}

// The value of a string field of a flat reply line, e.g. field(line, "text"). Enough for simple replies; a driver that
// reads arrays (menus, tree rows) needs a real JSON parser or a small scanner like guitest::Reply.
std::string field(const std::string& line, const std::string& key) {
    size_t at = line.find("\"" + key + "\":");
    if (at == std::string::npos) return "";
    at += key.size() + 3;
    if (at < line.size() && line[at] == '"') {
        std::string out;
        for (size_t i = at + 1; i < line.size() && line[i] != '"'; ++i) {
            if (line[i] == '\\' && i + 1 < line.size()) ++i;
            out += line[i];
        }
        return out;
    }
    size_t end = line.find_first_of(",}", at);
    return line.substr(at, end - at);
}

class App {
public:
    explicit App(const std::string& exe) {
        // 2. A random pipe name per run: the app creates \\.\pipe\<name> with FILE_FLAG_FIRST_PIPE_INSTANCE.
        std::string pipeName = "mini_guitest_" + std::to_string(::GetCurrentProcessId()) + "_" + std::to_string(::GetTickCount64());
        std::wstring command = widen("\"" + exe + "\" --automation " + pipeName);

        // 3. A job object that kills the app when the driver goes away, however the test ends.
        job_ = ::CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits = {};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        ::SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));

        STARTUPINFOW startup = { sizeof(startup) };
        PROCESS_INFORMATION info = {};
        if (!::CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, nullptr, &startup, &info)) {
            throw std::runtime_error("could not start " + exe);
        }
        ::AssignProcessToJobObject(job_, info.hProcess);
        ::ResumeThread(info.hThread);
        ::CloseHandle(info.hThread);
        process_ = info.hProcess;
        pid_ = info.dwProcessId;

        // 4. The pipe opens a moment after the process starts: retry until it does (or the app dies).
        std::wstring path = L"\\\\.\\pipe\\" + widen(pipeName);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (pipe_ == INVALID_HANDLE_VALUE) {
            pipe_ = ::CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (pipe_ != INVALID_HANDLE_VALUE) break;
            if (::WaitForSingleObject(process_, 0) != WAIT_TIMEOUT || std::chrono::steady_clock::now() > deadline) {
                throw std::runtime_error("no automation pipe");
            }
            ::Sleep(50);
        }
        // 5. hello first; refuse a protocol you do not know.
        if (field(ask("{\"cmd\":\"hello\"}"), "protocol") != "1") throw std::runtime_error("unknown protocol");
    }

    ~App() {
        if (pipe_ != INVALID_HANDLE_VALUE) ::CloseHandle(pipe_);
        if (job_) ::CloseHandle(job_);          // kills the app
        if (process_) ::CloseHandle(process_);
    }
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // One request line, one reply line. Poll with PeekNamedPipe instead of blocking in ReadFile: an app stuck in a
    // modal loop (an open native menu or dialog) answers nothing, and the driver must be able to time out.
    std::string ask(const std::string& json) {
        std::string line = json + "\n";
        DWORD done = 0;
        if (!::WriteFile(pipe_, line.data(), static_cast<DWORD>(line.size()), &done, nullptr)) throw std::runtime_error("pipe closed");
        std::string answer;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (char c;;) {
            DWORD available = 0;
            if (!::PeekNamedPipe(pipe_, nullptr, 0, nullptr, &available, nullptr)) throw std::runtime_error("pipe closed");
            if (available == 0) {
                if (std::chrono::steady_clock::now() > deadline) throw std::runtime_error("no answer to " + json);
                ::Sleep(2);
                continue;
            }
            ::ReadFile(pipe_, &c, 1, &done, nullptr);
            if (c == '\n') return answer;
            answer += c;
        }
    }

    // Blocks until the app has nothing pending: use this instead of Sleep after sending input.
    void waitIdle() { ask("{\"cmd\":\"idle\"}"); }

    POINT centerOf(const std::string& name) {
        std::string r = ask("{\"cmd\":\"findView\",\"name\":\"" + name + "\"}");
        if (field(r, "ok") != "true") throw std::runtime_error(field(r, "error"));
        return POINT{ static_cast<LONG>(std::stod(field(r, "x")) + std::stod(field(r, "w")) / 2),
                      static_cast<LONG>(std::stod(field(r, "y")) + std::stod(field(r, "h")) / 2) };
    }

    std::string textOf(const std::string& name) { return field(ask("{\"cmd\":\"getText\",\"name\":\"" + name + "\"}"), "text"); }

    // A real click at the centre of a named view.
    void click(const std::string& name) {
        POINT p = centerOf(name);
        bringToFront();
        // Never click blind: the point must belong to the app's own window (nothing else may cover it).
        DWORD pid = 0;
        ::GetWindowThreadProcessId(::WindowFromPoint(p), &pid);
        if (pid != pid_) throw std::runtime_error("another window covers the target");
        // Absolute mouse coordinates are 0..65535 across the whole virtual desktop (all monitors).
        int left = ::GetSystemMetrics(SM_XVIRTUALSCREEN), top = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
        int w = ::GetSystemMetrics(SM_CXVIRTUALSCREEN), h = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
        INPUT in[3] = {};
        in[0].type = in[1].type = in[2].type = INPUT_MOUSE;
        in[0].mi.dx = static_cast<LONG>((p.x - left) * 65535.0 / (w - 1) + 0.5);
        in[0].mi.dy = static_cast<LONG>((p.y - top) * 65535.0 / (h - 1) + 0.5);
        in[0].mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        in[1].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        in[2].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        ::SendInput(3, in, sizeof(INPUT));
        waitIdle();
    }

    // Typing: one KEYEVENTF_UNICODE down/up pair per character (layout independent).
    void type(const std::wstring& text) {
        bringToFront();
        for (wchar_t c : text) {
            INPUT in[2] = {};
            in[0].type = in[1].type = INPUT_KEYBOARD;
            in[0].ki.wScan = in[1].ki.wScan = c;
            in[0].ki.dwFlags = KEYEVENTF_UNICODE;
            in[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
            ::SendInput(2, in, sizeof(INPUT));
        }
        waitIdle();
    }

private:
    // The app's frame is owned by a hidden helper window, so look for "visible and titled", not "no owner".
    HWND mainWindow() {
        struct Search { DWORD pid; HWND found; } s = { pid_, nullptr };
        ::EnumWindows([](HWND w, LPARAM p) -> BOOL {
            auto* s = reinterpret_cast<Search*>(p);
            DWORD pid = 0;
            ::GetWindowThreadProcessId(w, &pid);
            if (pid == s->pid && ::IsWindowVisible(w) && ::GetWindowTextLengthW(w) > 0) { s->found = w; return FALSE; }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&s));
        return s.found;
    }

    void bringToFront() {
        HWND w = nullptr;
        for (int i = 0; i < 100 && !(w = mainWindow()); ++i) ::Sleep(50);   // the frame shows after the pipe opens
        if (!w) throw std::runtime_error("the app has no visible window");
        if (::IsIconic(w)) ::ShowWindow(w, SW_RESTORE);
        // Windows only lets the foreground thread hand the foreground over: attach to its input queue for the call.
        HWND current = ::GetForegroundWindow();
        DWORD theirs = current ? ::GetWindowThreadProcessId(current, nullptr) : 0, ours = ::GetCurrentThreadId();
        if (theirs && theirs != ours) ::AttachThreadInput(ours, theirs, TRUE);
        ::SetForegroundWindow(w);
        if (theirs && theirs != ours) ::AttachThreadInput(ours, theirs, FALSE);
        if (::GetAncestor(::GetForegroundWindow(), GA_ROOT) != w) throw std::runtime_error("could not bring the app to the foreground");
    }

    HANDLE process_ = nullptr, job_ = nullptr, pipe_ = INVALID_HANDLE_VALUE;
    DWORD pid_ = 0;
};

}

int main(int argc, char** argv) {
    try {
        App app(argc > 1 ? argv[1] : "testharness.exe");
        app.click("filterInput");
        app.type(L"Box");
        std::string text = app.textOf("filterInput");
        printf("filterInput = '%s' -> %s\n", text.c_str(), text == "Box" ? "PASS" : "FAIL");
        return text == "Box" ? 0 : 1;
    } catch (const std::exception& e) {
        printf("FAIL: %s\n", e.what());
        return 2;
    }
}
```

### Reference implementation

`tools/guitest/newui_guitest/app.py` is the full version of the sample: an `App` class with typed wrappers for every query
command, tree rows, scrollbars, segments, native menus (`open_menu` / `choose_menu` / `close_menu`), native file dialogs,
`drag`, `wheel`, `key`, `reveal`, `screenshot` (with a BMP reader), the user-input guard and the covered-point check.
`tools/guitest/tests/test_pure.py` tests the parts that need no desktop (key chords, the BMP reader, the Win32 struct
layouts). `demos/sketch/tests/gui/test_sketch_gui.py` shows it used on a real app (16 tests, `unittest`, one app per test).
The driver is also described in the skill that ships with it (see the next section).

### Customizing the driver for an app

There is no config file. Customizing is writing a few lines of Python, from the least to the most change:

1. **Say which app.** Normally nothing in the test: the person running it passes `--exe` and `--arg` (see "Launching the tests"),
   so one test file runs against any build. A test class may name a default for when `--exe` is not given, from a path it can
   compute (for example relative to the test file) rather than from the environment:

   ```python
   class MyCase(AppTestCase):
       exe = os.path.join(REPO, "build", "Debug", "myapp.exe")      # --exe overrides it
       args = ("--demo-data",)                                       # --arg adds to it
   ```

2. **Add the app's vocabulary as helpers** on that subclass. This is where "draw a rectangle", "open the settings tab" or
   "type into the search box" live, built from the driver calls (`click`, `drag`, `type`, `choose_menu`, `text_of`...).
   Put them in one base class and share it between test files (`demos/sketch/tests/gui/test_sketch_gui.py` has `SketchCase`
   with `draw`, `choose_tool`, `set_field`, `canvas_at`...).

3. **Change how the app starts.** Override `launch()` to prepare something first (a temp folder, a document to open) and
   return an `App`; `setUp` and the cleanup that kills it are inherited:

   ```python
   def launch(self):
       self.workdir = tempfile.mkdtemp()
       self.addCleanup(shutil.rmtree, self.workdir, True)
       return App(self.exe, ("--open", os.path.join(self.workdir, "test.doc")), cwd=self.workdir)
   ```

4. **Tune the timing and the guard.** From the command line: `--connect-timeout`, `--reply-timeout`, `--quiet-period`,
   `--quiet-timeout` (a slow app raises the first two) and `--no-guard` for an unattended machine. They set the `App` class attributes
   `CONNECT_TIMEOUT`, `REPLY_TIMEOUT`, `QUIET_PERIOD`, `QUIET_TIMEOUT` and `user_input_guard`, which a test may also set itself.

5. **Reach more than one window.** Each window the app calls `expose()` on is a root. Pass its name to `screenshot("editorRoot")`
   and name its views as usual. A native dialog is not newui: use `wait_for_dialog`, `type_raw` and `key_raw`.

6. **Call a command the driver has no wrapper for.** `app.ask("myCommand", name="x", value=3)` sends any command (see "Adding
   a command to the server") and returns the reply as a dict, raising `AutomationError` when `ok` is false. Wrap it in a
   helper on your test class when it is used more than once.

7. **Extend the driver itself** by subclassing `App` (and returning that from `launch()`), for a behaviour every test of that app
   needs, such as a custom control's click target or a lookup that is not a plain name:

   ```python
   class MyApp(App):
       def click_tab(self, title):                  # a custom tab strip that findView cannot address by name
           bar = self.find("tabStrip")
           self.click_at((bar.x + 40 + 120 * TAB_TITLES.index(title), bar.y + bar.h / 2))
   ```

   A change that every app would want (a new query, a new input kind, a Windows quirk) belongs in `tools/guitest` itself,
   with a case in `tests/test_pure.py` when it can be tested without a desktop.

What cannot be customized from the driver is *what the app reports*: a view must have a name, and a custom control exposes
only its reflected properties. Name the views and add reflected properties (or a server command) in the app.

### Things that will bite you

- **A control that is only partly inside a scrolling parent.** `findView` reports the view's whole bounds, even the
  part clipped away. A click on the clipped edge, or below the parent's visible area, silently does nothing (or hits the
  neighbour). Scroll the parent until the control is fully inside its visible rectangle (`scrollInfo` / the mouse wheel)
  before clicking. A toggle button that is half-clipped looks "dead".
- **Modifier keys and injected chords.** newui reads Ctrl/Shift/Alt for a key event with `GetKeyState` (the state as of
  that message). An older version used `GetAsyncKeyState` (the live state), which made a chord sent in one batch (Ctrl down, Z
  down/up, Ctrl up) arrive as a bare Z, because Ctrl was already released when the message was handled. A driver written
  against a framework that still has that bug has to hold modifiers for a few milliseconds.
- **Focus decides who gets the keys.** After typing into a text box, Ctrl+Z belongs to the box, not to the canvas or the
  document. Click the thing that should receive the shortcut first, or use a menu command.
- **Native menus and dialogs are modal loops.** While a menu is open `idle` does not answer, so a click that opens one must
  not wait for idle (`settle=false`) and the driver must poll `popupMenu` instead. Clicks inside an open menu must not bring the
  window to the front (that closes it). `Escape` closes one level of nesting, not all of them. A native file dialog
  is a separate top-level window of the app: type into it with raw `SendInput` after checking it is the foreground window
  (class `#32770`); a driver helper that first calls "bring the main window to the front" would pull the main window over
  the dialog.
- **A submenu is not open the moment you click its parent.** `popupMenu` keeps reporting the parent's items for a moment:
  poll until the label you want appears.
- **Handles drawn on top of the thing you want to drag.** In an editor, the middle of a selected object may hold a
  handle (sketch's transform-origin marker): grab the object off-centre. This is app behaviour, not a driver bug.
- **Screenshots.** `screenshot` writes a `.bmp` of a RootView rendered by the framework itself (it cannot be occluded, but
  it also is not what the desktop shows). The path in the JSON request needs escaped backslashes, or just use forward slashes.
  Pixel offsets are relative to the root view: subtract the root view's screen position (`findView` of the root view).
  Probe with the colour the app really uses; a "dark pixel" test misses a mid-blue stroke.
- **Names.** A view without a name cannot be found. Names must be unique inside the exposed windows; `findView` returns
  the first match.
- **Timing.** `idle` covers the UI loop only. Poll for background results with a bounded loop (`for 300 x Sleep(100)`) on the
  state you expect, and give the loop a message that says what never happened.
- **Do not ship it.** A build with automation on exposes the whole view tree, every text and screenshots to any
  process of the same user. Keep it behind the CMake option, off in release builds.
- **Never leave input held.** If anything throws between "button down" and "button up", release the button before
  rethrowing, or the person's desktop keeps dragging.

### Adding a command to the server

Commands are an `if (cmd == "...")` chain in `Automation::handle()` (`src/automation.cpp`); there is no plugin
registration. Write a `fooReply(const Request&)`, add the branch, document it in the table above and in the header
comment at the top of `automation.cpp`, add a headless case in `unittests/test_automation.cpp`, then a typed wrapper in the driver.
Replies run on the app's UI thread. The one exception, `popupMenu`, is answered off it so it works while a native
menu's modal loop is running.

## Teaching Claude Code to use it (a skill)

A *skill* is a folder that Claude Code loads when a request matches its description. A skill for a driver lets Claude
write and extend GUI tests without being walked through the protocol each time. The reference skill is
`.claude/skills/newui-gui-driver/`, which points at the Python driver in `tools/guitest`.

### Layout

```
.claude/skills/<skill-name>/
    SKILL.md             required: frontmatter (name, description) + the instructions Claude follows
    reference.md         optional: command and driver API tables, linked from SKILL.md
    templates/           optional: test and CMake templates with placeholders (myapp, MYAPP_GUI_AUTOMATION ...)
        example_tests.py.in
        CMakeLists.txt.in   the app-side CMake snippet
```

### What to do

1. **Write `SKILL.md`.** Start with frontmatter:

   ```markdown
   ---
   name: newui-gui-driver
   description: Create or extend a GUI test driver for a newui (Win32/Blend2D) app using newui's automation server - real SendInput plus a named-pipe query protocol. Use when asked to add a GUI/live-input test, drive the app from outside, set up automation in a new newui app, or add an automation command.
   ---
   ```

   The `description` is what Claude matches against the request, so say both **what** the skill does and **when** to use
   it, with the words a person would use ("GUI test", "drive the app", "automation"). Keep the body short and procedural:
   the steps for a new app (CMake option, `--automation` handling, `expose`/`start`/`stop`, naming views), how to write a test,
   the rules that keep tests reliable (wait with `idle`, poll background work, scroll clipped controls into view), the user-input guard,
   how to add a server command, and the pitfalls from "Things that will bite you" above. Link long tables instead of pasting them.
2. **Put the long material in sibling files.** The command table and driver API go in `reference.md`; link it from `SKILL.md`.
   Keep it a digest and name `automation.md` as the source of truth, so the two do not drift.
3. **Point at the driver, do not copy it.** The Python driver is a package (`tools/guitest`), so the skill names its path and
   tells Claude to put it on `sys.path` (or `PYTHONPATH`) rather than carrying a copy that can drift. Put test templates
   in `templates/`. A skill installed in a project that does not have `tools/guitest` must say where to get it.
4. **Write the safety rules into the skill**, because Claude will otherwise run a test as readily as it writes one:
   - GUI tests move the person's real mouse and type on their keyboard. **Ask before every run that sends input**; build
     freely, but run only after an explicit go-ahead, and say which window must stay uncovered.
   - Make a new driver's first run read-only (`hello`, `findView`, `screenshot`).
   - A failure such as "another window covers", "could not bring the app to the foreground" or "input from outside" means the person
     was active: report it, do not retry in a loop.
   - Do not turn the user-input guard off except on a machine the person says is unattended.
   - Never ship a build with automation enabled.
5. **Install it.** For one project, put the folder in `<project>\.claude\skills\<skill-name>\`. For every project, put it in
   `%USERPROFILE%\.claude\skills\<skill-name>\`. Restart Claude Code (or start a new session) so it is picked up; then it triggers
   from a matching request or by name (`/<skill-name>`). Check it is found by asking for something it covers.
6. **Keep a `README.md` in `.claude/skills/`** that lists the skills, says how to copy one into another project, and where the canonical
   source of any bundled files lives (the reference skill's README does this).
7. **Keep it in sync.** When the protocol, the driver API or a pitfall changes, update `automation.md` first, then the skill's
   `reference.md` and `SKILL.md`. A skill that describes a driver that no longer exists is worse than none.

The skill is guidance, not enforcement. If a rule must hold regardless (for example "no input without asking"), also put it in the
project's `CLAUDE.md` or its permission settings; do not rely on the skill text alone.

## Limits

- Window bounds are screen pixels; with a DPI-unaware driver they will not line up. Make the
  driver per-monitor DPI aware.
- `SendInput` moves the real cursor and needs the target window in the foreground: run tests on a
  session nobody is using, and check the window isn't covered before trusting a click.
- Enabled / visible / checked state is only available through `describe` / `get` (reflected properties of the control);
  `findView` reports `visible` but not whether the view is clipped by a scrolling parent (see "Things that will bite you").
- There is no whole-window tree dump yet; `tree` covers `TreeView` rows only (custom views such as a hand-drawn list
  expose nothing unless they have reflected properties).

## Tests

`unittests/test_automation.cpp` covers each command headlessly and a real pipe round trip:
`build\unittests\Debug\newui_tests.exe --gtest_filter="Automation*"`.

## Code

`include/newui/automation.h` (API), `src/automation.cpp` (server; the protocol is also documented
at the top of the file).

# newui_guitest

> **This works ONLY with apps built on the newui framework.** The driver does not look at the screen or use Windows UI
> Automation: it asks the app, over a named pipe, questions that only newui's own automation server
> (`newui_automation`, linked into the app and started with `--automation <pipe>`) can answer: where a *named* view is, what
> it shows, what is focused. An app that is not built on newui (Win32, Qt, WPF, a browser...) has no such server, so
> `python -m newui_guitest check` fails with "no automation pipe" and nothing else here applies. An app that *is* built on newui
> works only after it links the server and starts it (see "Setting it up in an app" in `D:\code\newui-demos\automation.md`), and only the views it has named
> can be found.
>
> **It is not a general-purpose Win32 (or any other) GUI-testing tool.** `SendInput` is the easy part. What makes it work is the list of newui features below, which the app has to have.

The GUI-test driver for newui apps. It starts an app that was built with newui's automation server, sends **real** mouse
and keyboard input with `SendInput`, and asks the app what it shows over a named pipe. Python 3, standard library only
(ctypes), Windows only. The protocol is described in `D:\code\newui-demos\automation.md`; this driver is the reference
implementation of the "what a driver has to do" table there.

## What the app needs from newui

| newui feature | What the driver gets from it |
|---|---|
| **The automation server** (`newui_automation`, `--automation <pipe>`) | The named pipe the driver talks to. Nothing works without it. |
| **Named views** (`setName()`, names in `.newui` files) | `findView` returns a *named* view's screen rectangle, so no hard-coded coordinates. |
| **Reflection** (`describe` / `get`) | Plain state (numbers, flags, strings, combo values) without scraping pixels. |
| **Self-reporting controls** (`TextControl`, `TreeView`, `ScrollView`, `SegmentedControl`, menu bars) | Text, tree rows, scroll thumbs, segment labels, menu-bar buttons, caret position. |
| **`popupMenu`**, answered off the UI thread | The items of an open native menu while its modal loop runs. |
| **The run loop's `idle`** | "Nothing pending": the driver waits on it instead of sleeping. |
| **Framework-side `screenshot`** (RootView painted by Blend2D) | A picture of what the app draws, which another window cannot cover. |
| **The framework's input path** (`GetKeyState` in `src/utils.cpp`) | Injected Ctrl/Shift chords arrive like a person's. |

Details: `D:\code\newui-demos\automation.md`, "What newui provides that makes this possible".

## Launch it

```
cd D:\code\newui-demos\tools\guitest         # or put this folder on PYTHONPATH

python -m newui_guitest check D:\build\myapp.exe --view canvas       # 1. read-only smoke check, sends no input
python -m newui_guitest test D:\code\myapp\tests\gui --exe D:\build\myapp.exe --list     # 2. list the tests, starts nothing
python -m newui_guitest test D:\code\myapp\tests\gui\test_myapp.py --exe D:\build\myapp.exe   # 3. run them (real mouse!)
python D:\code\myapp\tests\gui\test_myapp.py --exe D:\build\myapp.exe -k Typing   # same, from a test file that calls newui_guitest.main()
python -m unittest discover -s D:\code\newui-demos\tools\guitest\tests -v   # the driver's own tests: no app, no input
```

Options (also `python -m newui_guitest test --help`). Everything you need to control is an option, not an environment variable:

| Option | Meaning |
|---|---|
| `--exe PATH` | the app to test (overrides a test class's `exe`) |
| `--arg ARG` | extra command-line argument for the app; repeat for several |
| `--cwd DIR` | working directory of the app |
| `-k TEXT` | only tests whose name contains TEXT; repeatable |
| `--list` | print the tests and exit; starts nothing |
| `-f` / `-q` | stop at the first failure / terse output |
| `--no-guard` | no wait-for-idle-person and no abort on outside input (unattended VM only) |
| `--quiet-period S`, `--quiet-timeout S` | how long the mouse and keyboard must be untouched (1.5 s), how long to wait for it (30 s) |
| `--connect-timeout S`, `--reply-timeout S` | wait for the app's pipe (20 s), for one reply (30 s) |

Exit code: 0 all passed, 1 a failure, 2 no app given.

**Tests that use `App` move the person's real mouse and type on their keyboard.** Run them on a desktop nobody is using,
with nothing covering the app window. Make the first run of a new app the read-only `check` above.

## Write a test

```python
import sys
sys.path.insert(0, r"D:\code\newui-demos\tools\guitest")      # or install the folder on PYTHONPATH
import newui_guitest
from newui_guitest import AppTestCase

class FilterTests(AppTestCase):
    pass          # run:  python test_myapp.py --exe D:\build\myapp.exe   (a class may set a default `exe`, and `args`)

    def test_typing_fills_the_filter(self):
        self.app.click("filterInput")                    # find by name -> real click at its centre -> wait for idle
        self.app.type("Box")
        self.assertEqual("Box", self.app.text_of("filterInput"))

if __name__ == "__main__":
    sys.exit(newui_guitest.main())       # parses --exe, --arg, -k, ... and runs this file's tests
```

`AppTestCase` starts a fresh app for every test (`self.app`) and kills it however the test ends. A larger app subclasses
it once for its own helpers: see `D:\code\newui-demos\demos\sketch\tests\gui\test_sketch_gui.py` (`SketchCase`).

## API (`newui_guitest.App`)

| Group | Members |
|---|---|
| Lifetime | `App(exe, args=(), cwd=None)`, `close()`, `running`, usable as a context manager. Adds `--automation <random pipe>`; the app is in a kill-on-close job object. |
| Raw | `ask(cmd, check=True, timeout=None, **fields)` returns the reply dict; raises `AutomationError` when `ok` is false. |
| Queries | `wait_idle`, `find(name) -> Rect`, `text_of`, `focused_name`, `prop(name, property)`, `describe`, `scroll_info`, `segment`, `caret`, `tree`, `find_row`, `screenshot(root="rootview") -> ScreenShot`, `wait_until(condition, what, timeout)` |
| Input | `click(target)`, `double_click`, `click_at(point, to_front=True, settle=True, button="left")`, `type(text)`, `key("ctrl+shift+z")`, `wheel(target, notches)` (+ is up), `drag(start, end, steps=10)`, `drag_by(name, dx, dy)`. A target is a view name or an `(x, y)` screen point. Every call ends by waiting for `idle`. |
| Controls | `click_segment`, `click_row`, `double_click_row`, `toggle_row`, `reveal(name, scroller)` (scrolls a half-clipped control into view) |
| Menus | `open_menu(bar, menu)`, `choose_menu(bar, "Object", "Clipping Mask", "Make")` (nested), `close_menu()` |
| Dialogs | `wait_for_dialog()`, `type_raw(text)`, `key_raw(chord)` for a native file dialog, which is a separate top-level window |
| Window | `bring_to_front()`, `foreground_class()` |

`ScreenShot` (from `app.screenshot()`) reads the BMP itself: `pixel(x, y)` in **screen** coordinates, `count(x0, y0, x1, y1, test)`,
`changed(other, ...)`; helpers `differs(a, b)` and `dark(pixel)`. Probe with the colour the app really draws.

Class attributes of `App` (the options above set them): `CONNECT_TIMEOUT` (20 s), `REPLY_TIMEOUT` (30 s), `QUIET_PERIOD` (1.5 s),
`QUIET_TIMEOUT` (30 s), `user_input_guard`.

## Customizing it

No config file: customizing is a few lines of Python. `automation.md` ("Customizing the driver for an app") has examples of each.

1. `--exe` / `--arg` on the command line; a class may also set a default `exe` and `args`.
2. Helpers on that subclass for the app's vocabulary (`draw`, `open_settings`...): share one base class between test files.
3. Override `launch()` to prepare something first (temp folder, a document) and return an `App`.
4. `--connect-timeout` / `--reply-timeout` / `--quiet-period` / `--quiet-timeout`, or `--no-guard` on an unattended machine.
5. `screenshot("otherRoot")` for any window the app `expose()`d; `wait_for_dialog` / `type_raw` / `key_raw` for native dialogs.
6. `app.ask("someCommand", name="x")` for a server command the driver has no wrapper for.
7. Subclass `App` for behaviour every test of one app needs. Something every app would want goes into this package.

## Using the Claude Code skill

A reference copy of the Claude Code skill lives in `D:\code\newui-demos\tools\guitest\skill\newui-gui-driver\`. With it installed,
Claude Code knows how to set up automation in a newui app, write tests against this driver, launch them safely and add
server commands, without being walked through the protocol each time.

**1. Install it.** Claude Code only loads skills from a `.claude\skills\` folder, so copy the folder (PowerShell):

```
# this repository only (.claude\ is not committed, so each clone does this once)
Copy-Item -Recurse -Force D:\code\newui-demos\tools\guitest\skill\newui-gui-driver D:\code\newui-demos\.claude\skills\

# every project of this user
Copy-Item -Recurse -Force D:\code\newui-demos\tools\guitest\skill\newui-gui-driver C:\Users\<you>\.claude\skills\
```

For another repository, copy it into that repository's `.claude\skills\` and fix the absolute paths inside `SKILL.md` and the
templates so they name where `tools\guitest` and `automation.md` really are.

**2. Start a new Claude Code session** (a running one does not see a new skill) and check it is found: ask "which skills do you have?"
or type `/newui-gui-driver`.

**3. Ask for what you want.** The skill triggers on requests such as:

- "Add a GUI test for the Layers panel to the sketch tests."
- "Set up GUI automation in myapp so we can drive it from Python."
- "The export test fails with 'another window covers': what does that mean?"
- "Add an automation command that returns the number of selected objects, and a test that uses it."

What Claude then does, from the skill: follows the steps for a new app (CMake option, `--automation <pipe>`, `expose`/`start`/`stop`,
naming views), writes tests from the template with `AppTestCase`, and adds helpers for the app's vocabulary.
**It builds freely but asks for your go-ahead before every run that sends input**, tells you which window must stay uncovered, and
starts a new app with the read-only `python -m newui_guitest check`. A failure that means "the person was busy" is reported, not retried
in a loop.

**4. Keep it current.** Edit the copy under `tools\guitest\skill\`, then repeat step 1. Never edit the installed copy. When the protocol, the driver
API or a command-line option changes, update `automation.md` first, then the skill's `reference.md` and `SKILL.md`.

The skill is guidance, not enforcement: a rule that must always hold (such as "no input without asking") also belongs in the project's
`CLAUDE.md` or permission settings. `D:\code\newui-demos\tools\guitest\skill\README.md` lists the files the skill depends on.

## Safety rules the driver enforces

- Per-monitor DPI aware, so the app's physical-pixel rectangles line up with `SendInput`.
- A random pipe name per run, and a job object that kills the app when the driver goes away.
- Before it injects anything it waits until nobody has touched the mouse or keyboard for 1.5 s (`GetLastInputInfo`), gives up
  after 30 s (`UserBusyError`), and **aborts the test** if outside input arrives mid-test, releasing a held mouse button first.
  `--no-guard` (or `App.user_input_guard = False`) turns the guard off for an unattended VM.
- Before every click it checks `WindowFromPoint` belongs to the app (nothing covers the target).
- It waits with the `idle` command, never `Sleep`. `idle` means the UI loop is empty, not that background work is done: for
  that, use `wait_until`.

"another window covers", "could not bring the app to the foreground" and "input from outside" mean the person was busy,
not that the app is broken. Rerun when the machine is idle.

## What an app has to provide

1. The newui automation server, behind a CMake option, started with `--automation <pipe>` (see `automation.md`, "Setting it up in an app").
2. A **name** on every view a test touches (`setName()` or the name in the `.newui` file).
3. Nothing else: no config file. App-specific behaviour (where the executable is, helper methods like "draw a rectangle")
   lives in a small `AppTestCase` subclass.

## Layout

```
newui_guitest/
    app.py          App: pipe, process, window, input, menus, dialogs
    screenshot.py   ScreenShot (BMP reader), differs, dark
    keys.py         key names and chord parsing
    testing.py      AppTestCase (unittest)
    _win32.py       the ctypes declarations
    runner.py       the command line: options, test discovery, newui_guitest.main()
    __main__.py     python -m newui_guitest check | test
tests/test_pure.py  no-app tests: chords, BMP reader, Win32 struct sizes
skill/              reference copy of the Claude Code skill (SKILL.md, reference.md, templates) + how to install it
```

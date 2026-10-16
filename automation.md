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

## Limits

- Window bounds are screen pixels; with a DPI-unaware driver they will not line up. Make the
  driver per-monitor DPI aware.
- `SendInput` moves the real cursor and needs the target window in the foreground: run tests on a
  session nobody is using, and check the window isn't covered before trusting a click.
- No enabled/disabled state is reported (views don't carry one).
- No `getProperty`/tree snapshot yet.

## Tests

`unittests/test_automation.cpp` covers each command headlessly and a real pipe round trip:
`build\unittests\Debug\newui_tests.exe --gtest_filter="Automation*"`.

## Code

`include/newui/automation.h` (API), `src/automation.cpp` (server; the protocol is also documented
at the top of the file).

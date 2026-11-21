# newui

## Requirements

- CMake 3.24+
- A C++17 compiler (MSVC, GCC, or Clang)

All third-party dependencies (Blend2D, asmjit, GoogleTest) are vendored under
[3rdparty/](3rdparty/), so no network access or package manager is needed to build.

## Building

Configure and build from the repository root:

```sh
cmake -S . -B build
cmake --build build --config Debug
```

This produces:

- `newui` — static library
- `newui_app` — sample executable linking against `newui`
- `newui_tests` — GoogleTest test binary

Build artifacts are written under `build/` (e.g. `build/Debug/newui_app.exe` for a
multi-config generator like Visual Studio, or `build/newui_app` for a single-config
generator like Ninja/Makefiles).

## Building with Visual Studio from the command line

Visual Studio ships its own copy of CMake and MSBuild. The easiest way to get
both on `PATH` is to open a **Developer Command Prompt for VS** (or
**Developer PowerShell for VS**) from the Start Menu, then run the same
commands as above:

```bat
cmake -S . -B build
cmake --build build --config Debug
```

`cmake --build` drives MSBuild for you. To invoke MSBuild directly on the
generated solution instead:

```bat
msbuild build\newui.slnx /p:Configuration=Debug
```

If you're in a plain (non-Developer) terminal and `cmake` isn't on `PATH`,
call the CMake bundled with your Visual Studio install directly, e.g.:

```bat
"C:\Program Files\Microsoft Visual Studio\<version>\<edition>\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build
```

replacing `<version>`/`<edition>` with your installed Visual Studio version
(e.g. `2022`) and edition (`Community`, `Professional`, `Enterprise`).

## Running tests

Tests are maintained in [unittests/](unittests/) and run through CTest:

```sh
ctest --test-dir build -C Debug --output-on-failure
```

Or run the test binary directly:

```sh
build\unittests\Debug\newui_tests.exe
```

## GUI automation (opt-in)

`newui_automation` is a separate library that lets a test driver find named views and read state
over a named pipe while it drives the app with real `SendInput` input. It is **not** part of
`newui`: an app has to link it, call `Automation::expose(rootView)` and `Automation::start(pipe,
runLoop)` itself, ideally behind its own `#ifdef`, so a shipping build has none of it.

Setup in an app:

```cmake
option(MYAPP_GUI_AUTOMATION "Build with newui's automation server" ON)
if(MYAPP_GUI_AUTOMATION)
    target_link_libraries(myapp PRIVATE newui_automation)
    target_compile_definitions(myapp PRIVATE MYAPP_GUI_AUTOMATION)
endif()
```

```cpp
#ifdef MYAPP_GUI_AUTOMATION
#include <newui/automation.h>
#endif

#ifdef MYAPP_GUI_AUTOMATION
if (!pipeName.empty()) {                       // e.g. from a --automation <pipe> option
    newui::Automation::expose(frame.rootView());
    newui::Automation::start(pipeName, app.runLoop());
}
#endif
```

Enable or disable:

- **Off for good:** build with the app's option OFF (the library isn't linked, the code isn't
  compiled).
- **Off for a run:** start the app without the pipe option; nothing listens.
- **On for a run:** start the app with the pipe option (`myapp.exe --automation my_pipe`).

Views are found by name, so name the ones a test needs. The pipe is local-only and restricted to
the current user. Requests are one JSON object per line (`hello`, `findView`, `focused`, `getText`,
`screenshot`, `idle`). See [automation.md](automation.md) for the full protocol, security notes and
limits.

## Component settings

A `Component` can persist some of its reflected properties between runs. Mark a property as a
setting, register the component with the application's `SettingsManager`, and the stored value is
applied immediately and written back when the frame closes:

```cpp
splitter->assignSetting("splitPosition");                              // per user (default)
slider->assignSetting("value", newui::SettingScope::Machine);          // shared by all users

auto& settings = newui::Application::instance().settings();
settings.registerComponent(*splitter, "mainSplitter");                 // key must be stable and unique
settings.registerComponent(*slider, "slider");
settings.setAutosaveInterval(std::chrono::seconds(5));                 // optional: also store periodically
```

- The property must be reflected, have a getter and setter, and be a `bool`, integer, `float`/`double`
  or `std::string`.
- Values live in JSON5 files: `%APPDATA%\<company>\<app>\settings.json` (user) and
  `%PROGRAMDATA%\<company>\<app>\settings.json` (machine). Company and app come from the bundle's
  `Info.json` (`"company"`, `"name"`) or can be set with `setCompanyName()`/`setAppName()`.
- Settings that aren't a single property (restored window state, recent-files lists) can override
  `Component::loadSettings()`/`storeSettings()` and read/write through `SettingsAccess`.
- A registered component must outlive the manager's `storeAll()`, or be unregistered first.

See [examples/settings1.cpp](examples/settings1.cpp) for a runnable demo (`settings1.exe --reset`
clears the stored files) and [include/newui/componentsettings.h](include/newui/componentsettings.h)
for the full API.

## Data binding and validation

A `Binding` connects one property of a control to one value of a source and keeps the two in step.
Bindings, converters and validation rules are plain reflected objects declared in the frame's `.newui`
file, so a form designer can edit them; C++ only supplies the data:

```json5
bindings: [
  { type: "Binding", name: "age", group: "form", target: "ageField", property: "text",
    source: "form", key: "age", converter: "integer", trigger: "OnCommit",
    rules: [ { type: "RangeRule", min: 0, max: 120, message: "Age must be 0 to 120" } ] },
  { type: "Binding", target: "volumeLabel", property: "text", source: "volume", key: "value",
    converter: "percent", mode: "OneWay" },            // control to control, no code
],
converters: [ { type: "NumberTextConverter", name: "integer", decimals: 0 } ],
groups: [ { type: "BindingGroup", name: "form",
            rules: [ { type: "CompareRule", left: "password", right: "repeat", op: "Equal" } ] } ],
```

```cpp
frame.setBindingSource("form", model);   // before Bundle::loadFrame(), which resolves the bindings
auto& group = frame.bindingGroup("form");
if (!group.commit()) { /* group.errors() */ }   // all or nothing
```

- **Sources:** a `Model` (addressed by `key`), any reflected object (`key` is a property name), or
  another control found by name. Register them with `Frame::setBindingSource()`; they must outlive the frame.
- **Mode:** `OneTime`, `OneWay`, `TwoWay` (default), `ValidateOnly` (no source or key: the control is
  only checked, e.g. a "repeat password" box that a group's `CompareRule` compares with the real one). **Trigger** (for `TwoWay`): `OnChange` writes back at
  every edit, `OnCommit` when the control loses focus or the group commits, `Manual` only on `commit()`.
- **Pipeline:** read the control, convert to the source's type, run the rules, then write. A value that
  fails conversion or a rule is never written, and the control is outlined in red until it is fixed. Set a binding's `errorTarget` to the name of a
  `Label` (placed under or beside the field, its space reserved by the layout) to show the message there;
  it is emptied again once the value is fine. Enter in a `TextField` commits an `OnCommit` binding too.
- **Converters** (named, in the frame's `converters` list): `ScaleConverter`, `InvertConverter`,
  `NumberTextConverter`; the default is a strict conversion between numbers, bool and text.
- **Rules** (a binding's `rules`, all must pass): `RequiredRule`, `MinRule`, `MaxRule`, `RangeRule`,
  `LengthRule`, `RegexRule`, `OneOfRule`, `AnyOfRule` (OR), `CustomRule` (a C++ check registered by name
  with `registerValidator()`).
- **Groups:** bindings naming the same `group` form one form: `load()`/`revert()`, `dirty()`, `validate()`,
  `commit()`, `errors()`. Group rules (`CompareRule`, `CustomGroupRule`) check across fields.

See [examples/binding1.cpp](examples/binding1.cpp) and [examples/binding1.newui](examples/binding1.newui),
and [include/newui/binding.h](include/newui/binding.h) for the full API.

## Project layout

```
include/newui/   public headers
src/             library and app sources
unittests/       GoogleTest-based test suite
3rdparty/        vendored dependencies (Blend2D, asmjit, GoogleTest)
```

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

## Project layout

```
include/newui/   public headers
src/             library and app sources
unittests/       GoogleTest-based test suite
3rdparty/        vendored dependencies (Blend2D, asmjit, GoogleTest)
```

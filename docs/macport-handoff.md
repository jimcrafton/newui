# macOS / iOS port: handoff

Snapshot: 2026-11-21. Branch `macport` (tracks `origin/macport`), last commit `2fe1835 added initial porting plans`.
Read this first when resuming. The detailed plans are:

* [macos-ios-port-plan.md](macos-ios-port-plan.md): overall design, subsystem-by-subsystem, phased roadmap
* [phase0-portability-plan.md](phase0-portability-plan.md): Phase 0 step by step, with status table

## Working rules

* **Claude never commits and never pushes** (`CLAUDE.md`, still untracked: the user hasn't added it).
  Changes are left in the working tree for the user.
* **No Windows machine is available to Claude.** Anything touching MSVC-only code has to be built and tested on
  Windows by the user after each step. Say what was and wasn't verified.
* Scratch work goes in the session scratchpad, not in the repo (the spike build tree, venv and stub headers
  there are disposable).

## Decisions made

* Min OS: macOS 12, iOS 16. Coordinates: device pixels (same as Windows; `dpiScale` = backing scale).
* Closing the last window quits the app on macOS. macOS menu: global menu bar only (in-window `MenuBar` hidden).
* Settings: JSON5 files in Application Support (same format/API as Windows). Machine scope on macOS:
  `/Users/Shared/<company>/<app>/settings.json`; iOS and sandboxed apps fall back to the user file.
* reflectgen sizeof probe: skipped when targeting iOS (only feeds an optional memory estimate in the build log).
* Platform macros come from the compiler (`_WIN32`, `__APPLE__`, `TARGET_OS_*`) via `platform.h`, never from
  CMake `-D` flags. Prefer separate files (`src/win32|macos|ios`) over `#if` in shared code.
* Architecture: same as Windows. One native window, one native view backing `RootView`; Objective-C++ only in
  `src/apple|macos|ios/`; present via IOSurface-backed `CALayer` contents.

## Environment (this Mac, Apple Silicon)

| Tool | State |
|---|---|
| Xcode | 26.6 at `/Applications/Xcode-26.6.0.app`, selected with `xcode-select -s`; macOS 26.5 / iOS 26.5 SDKs |
| CMake | 4.4.5 at `/Applications/CMake.app` (not part of Xcode); **not yet on `PATH`**: call `/Applications/CMake.app/Contents/bin/cmake` or install the command-line links |
| Python | 3.13.1 from python.org (`/usr/local/bin/python3.13`), matches the Windows machine. Apple's 3.9.6 untouched |
| Homebrew | not installed |
| reflectgen venv | **not created in the repo** (the spike used a scratch venv, since deleted with the wrapper). Create: `python3.13 -m venv tools/reflectgen/.venv` then `.venv/bin/pip install -r tools/reflectgen/requirements.txt` (`clang==21.1.7`) |
| libclang | `LIBCLANG_PATH=/Applications/Xcode-26.6.0.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib` |

## Uncommitted changes in the working tree

| File | Change |
|---|---|
| `src/graphics.cpp`, `src/bundle.cpp` | `_stricmp` → portable ASCII case-insensitive suffix compare (`std::equal` + `std::tolower`); added `<algorithm>`/`<cctype>` includes |
| `include/newui/reflectionio.h` | `localtime_s` → `#ifdef _WIN32 localtime_s #else localtime_r`; comment updated |
| `docs/macos-ios-port-plan.md` | moved into `docs/` (already committed); decisions section; Phase 0 link; reflectgen flags note |
| `docs/phase0-portability-plan.md` | new: Phase 0 step-by-step plan |
| `docs/macport-handoff.md` | this file |
| `CLAUDE.md` | new: never commit, never push |

The three source edits were checked only with a standalone Apple Clang test program (suffix match and date
format behave correctly). **They have not been built on Windows.** First action when the user is at the Windows machine:
build + `ctest`.

## What the spike established (2026-11-20/21)

* **Win32 coupling:** with stubbed Win32 headers, only 32/75 public headers and 4/62 sources compile with
  Apple Clang (`color`, `namemanager`, `newui`, `piecetree` are clean).
* **Confirmed on Apple Clang:** `wchar_t` is 4 bytes; `typeid().name()` is mangled (`N5newui4SizeE`);
  `size_t` ≠ `uint64_t` (but no per-type integer overloads found in `reflection.h`: downgraded to an audit).
* **Real Clang-vs-MSVC conformance errors:** `classinfo()` used in templates before its declaration (8 sites in
  `reflection.h`: lines 680, 688, 1081, 1254, 1335, 1868, 1870, 1943); `delegate.h:338` uses `RunLoop` while
  incomplete; wide-string-path `ifstream`/`ofstream` (`componentsettings.cpp:99,179`); `\` path separators
  (`bundle.cpp`, `fontmanager.cpp`); vendored `svgandme/svg/core_table.h` needs `<iostream>`.
* **CMake:** `cmake -G Xcode` configures the whole project with a venv wrapper at the Windows path
  (without it, `cmake/ReflectGen.cmake:46` fails on the hard-coded `.venv/Scripts/python.exe`). Blend2D and
  asmjit configure and build under Xcode on Apple Silicon.
* **reflectgen:** runs on the Mac (found 73 of 75 headers). Plain parse fails on `windows.h`; with Win32 stubs
  it fails on `<cstdint>` unless given `-isysroot <SDK>` and `-isystem <SDK>/usr/include/c++/v1`; with those it
  reaches the real `classinfo`/`RunLoop` errors above. So no generated output on the Mac until Steps 1.2/1.3
  (and 3.3 for `windows.h`).
* **sizeof probe:** does not build on macOS (includes `reflection.h` → `utils.h` → `windows.h`, plus the same
  two Clang errors), contrary to its own header comment.

## Where we are in Phase 0

| Step | Status |
|---|---|
| 0 Baseline/tooling | Mac tooling done. **Windows baseline test count not yet recorded** (0.1) |
| 1.1 CRT calls | done in working tree, unverified on Windows |
| 1.2 `classinfo` / incomplete-type fixes in `reflection.h` | done in working tree, unverified on Windows; Apple Clang reports 0 errors in `reflection.h` (remaining errors are `windows.h`/`RunLoop`) |
| 1.0 Portability sweep script | not started (the spike's method lives only in the scratchpad: stub `windows.h` + `clang++ -fsyntax-only` over every header and source) |
| 1.2–1.9, 2, 3, 4 | not started |

## Next actions, in order

1. User: build + `ctest` on Windows with the working-tree changes; record the passing-test count (0.1); then
   commit what they want (`CLAUDE.md` included or not is their call).
2. ~~Step 1.2~~ done (see table).
3. Step 1.3: fix `RunLoop` being incomplete at `delegate.h:338` (watch the `runloop.h` ↔ `delegate.h` include cycle).
4. Steps 1.4–1.8 (wide-path streams, separators, svgandme include, type-name demangling, integer round-trip test).
5. Step 1.0 sweep script into `tools/portcheck/`, so each step shows errors going down.
6. Then Step 2 (UTF-16), Step 3 (Win32 out of public headers), Step 4 (first Mac build).

## Open items

* None of the design decisions are open. Open work is the step list above.
* Not yet done: put CMake on `PATH`; create the repo venv; add `CLAUDE.md` to git (user's call).

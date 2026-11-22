# macOS / iOS port: handoff

Snapshot: 2026-11-22. Branch `macport`, last commit `271a0f4 phase 2.4 mac port`.
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

## Uncommitted changes in the working tree

Only this handoff file (and the untracked `CLAUDE.md`, the user's call). Step 2.4 was committed by the user as `271a0f4`;
whether the examples/demos built on Windows was not confirmed to Claude (see Next actions).

What 2.4 contained, for reference:

* `ClipboardManager::setText/getText/textMimeData` and the drag-and-drop text API (`onProvideText`, `onTextDropped`,
  `onTextDroppedAt`, `onTextDragOver`, `extractUnicodeText`, `TextDataObject`, `StartDragOperation`, `currentText_`,
  `rootview.cpp`) use `std::u16string`. Mime names, format names and file paths stay `std::wstring`.
* The temporary `toWideString`/`fromWide` bridges from 2.2 were removed from `controls.cpp`/`test_texthistory.cpp`
  (the helpers stay in `utils.h`, `_WIN32` only, for the DirectWrite calls in `text.cpp`).
* Examples and demos that still used wide text with `TextField` were converted: `clipboard1`, `dragdrop1`,
  `uiinputmanager1`, `loadcoloreditor1`, `demos/manyrows`, `demos/sketch`. These are probably not in the user's default
  Windows target, so **ask them to build all targets**, not only `newui_tests`.

## What is done (all verified on Windows unless noted)

Step 1.1-1.8 and 2.1-2.4 are done and committed (2.4's Windows verification of examples/demos unconfirmed). Notes worth remembering:

* 1.2 needed more than the plan: forward declarations of `classinfo`, plus a `DependentOn<Tag, T>` alias in `reflection.h`
  so Clang accepts member access on the still-incomplete `Class`/`Property`.
* 1.7: `readableTypeName()` in `utils.h` (`__cxa_demangle` on Clang/GCC) underlies `demangleTypeName`, `extractNamespace`,
  `qualifiedTypeName`, `automation.cpp typeName`.
* 1.8 found two real bugs: `Property::readValue/writeValue` matched integer types by width typedef (broke `size_t`/`long`
  on Apple), and `ObjectWriter::writeInt*` ignored `signedVal` (negative ints never round-tripped). Both fixed. Known,
  left alone: `ClassReader` only has 32-bit `readInt()`; 64-bit values go through `double`.
* 2.1 portable `utf8ToUtf16`/`utf16ToUtf8`; 2.2 text core on `char16_t`; 2.3 UxTheme class names are `std::string`
  (converted to wide at `OpenThemeData`), dead `wstring` property branches removed.
* 1.9 (optional `clang-cl` build) skipped: the user never said whether the Clang tools are installed.

## Where we are in Phase 0

| Step | Status |
|---|---|
| 0.1 Windows baseline test count | never recorded (not needed so far: every step was verified by a green build + test run) |
| 1.0 Portability sweep script `tools/portcheck/` | not started (method lives in the scratchpad: stub `windows.h` + `clang++ -fsyntax-only`) |
| 1.1-1.8 | done, verified |
| 1.9 `clang-cl` | skipped (optional) |
| 2.1-2.3 | done, verified |
| 2.4 clipboard/dragndrop to `u16string` | done; all targets (examples, demos) build on Windows after include/`utf16ToUtf8` fixes (uncommitted) |
| 2.5 final grep | done (all hits are Win32-boundary files); only the emoji/CJK TextField hand-test is pending |
| 3 Win32 out of public headers | 3.1 done (`platform.h`, `win32/nativehandles.h`); 3.2 done in tree, Windows build pending (geometry already verified); 3.3 next |
| 4 first Mac build | not started |

## Step 3 order change (decided 2026-11-22)

3.3 (drop `<windows.h>` from `newui.h`) moves to the END of Step 3: 31 public headers still use Win32 types, so do 3.4, 3.5, 3.6
first, then 3.3. 3.2 is verified on Windows. 3.4 slice 1 (Cursor + RunLoop) is in the working tree, Windows build pending:
`NativeCursorHandle` (`platform.h`, `toNativeCursor/toHCURSOR` in `win32/nativehandles.h`), `Cursor::handle()` and
`View::resolvedCursor()` return it, `RunLoop::TimerHandle` is `std::uintptr_t`, `RunLoop::runModal` takes `NativeWindowHandle`,
`TimerProc` replaced by a file-local `winTimerProc` calling public-but-internal `RunLoop::timerFired`.
3.4 slice 1 verified on Windows. Slice 2a (Dialog + ContextMenu) in tree, Windows build pending: `Dialog::show*`/`dialogHandle()`/`showModal`
take/return `NativeWindowHandle`; `ContextMenu::show`, `handle()`, `MenuItem::ownerMenu_` use new `NativeMenuHandle`
(`toNativeMenu/toHMENU`); tests updated. Still to do: `MenuItem::bitmap()` (HBITMAP) check, `PopupTool`/`CalloutTool`
(their `HWND`+`HINSTANCE` feed the `RootView` constructor, so do with slice 3), `ClipboardScope` (3.5, detail namespace),
slice 3 (Frame/RootView accessors).

## Next actions, in order

1. Ask the user to confirm all targets (examples and demos too) build after 2.4; fix any errors they paste.
2. Step 2.5: `grep -rnE 'wstring|wchar_t|\bL"' include src`; every remaining hit must be in a file that calls Win32 APIs
   (`uicolormanager`, `dialogs`, `filewatcher`, `clipboardmgr`/`dragndrop` internals, `automation` pipe code, `text.cpp`
   DirectWrite literals, `viewstyle.cpp` `OpenThemeData`). Hand-test: type/select/copy/paste emoji and CJK in a TextField.
   Update the plan's status table.
3. Decide on 1.0 (sweep script) and 1.9; then Step 3 (the big refactor: `windows.h` out of public headers), then Step 4.

## Working method that has worked (reuse it)

* Make the change, then check what can be checked on the Mac with Apple Clang `-fsyntax-only` and the stub `windows.h`
  (scratchpad `/tmp/stub2/windows.h` defines `WORD UINT HWND WPARAM LPARAM DWORD`; recreate if gone). In shell scripts,
  **quote include paths: the repo path contains a space** (`/Volumes/Music Data/...`); use a bash array.
* Pure-C++ pieces can be built and run natively on the Mac: `test_piecetree.cpp` + `piecetree.cpp` + the UTF-16 helpers
  + `3rdparty/googletest/googletest/src/gtest-all.cc`/`gtest_main.cc` (13 tests pass).
* MSVC stops at the first failing file, so expect the user to paste errors one file at a time. Blanket `L"..."` -> `u"..."`
  conversions caught DirectWrite literals once (`test_text.cpp`): when converting, always exclude `CreateTextFormat`,
  `CreateTextLayout`, `CreateWindow` and similar Win32 calls.
* Say plainly what was and wasn't verified; the user runs Windows builds and reports back ("works on windows, next step").

## Open items

* None of the design decisions are open.
* Not yet done: put CMake on `PATH`; create the repo venv; add `CLAUDE.md` to git (user's call).

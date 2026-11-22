# Phase 0: portability fixes, step by step

Status: draft, 2026-11-20. Expands Phase 0 of [macos-ios-port-plan.md](macos-ios-port-plan.md) with the
findings of the Apple Clang spike (`clang++ -fsyntax-only` over every header and source, Win32 headers
stubbed out).

## Ground rules

* **One step = one small change that merges on its own.** After every step the Windows build and the full
  test suite pass, with no behavior change. A step that can't be verified on Windows doesn't merge.
* **Nobody commits or pushes except the user** (see `CLAUDE.md`). Claude prepares each step as an
  uncommitted working-tree change, says what was verified and what wasn't, and stops.
* **Verification a step needs, in order:** (1) Windows MSVC build + `ctest`; (2) the portability sweep
  (step 1.0) shows no new errors; (3) the step's own check, listed below.
* Order matters: Steps 1 and 2 are mechanical and low-risk, Step 3 is the big refactor, Step 4 needs a Mac
  toolchain. Don't start a step until the previous one has merged.

## Spike results this plan is built from

| Finding | Evidence | Fix in |
|---|---|---|
| Public headers include `<windows.h>`; only 32/75 headers and 4/62 sources compile with stubbed Win32 types | spike sweep | Step 3 |
| `classinfo(...)` called in templates before it's declared (8 sites in `reflection.h`) | `reflection.h:680,688,1081,1254,1335,1868,1870,1943` | 1.2 |
| `delegate.h:338` uses `RunLoop` while it is only forward-declared | `delegate.h:338` | 1.3 |
| Wide-path `ifstream`/`ofstream` (MSVC extension) | `componentsettings.cpp:99,179` | 1.4 |
| Hard-coded `\` path separators | `bundle.cpp` (e.g. `:802`), `fontmanager.cpp`, others | 1.5 |
| Missing `<iostream>` in vendored `svgandme/core_table.h` | `core_table.h:489-496` | 1.6 |
| `typeid().name()` is mangled on Clang (`N5newui4SizeE`) | probe program on Apple Clang | 1.7 |
| `wchar_t` is 4 bytes on Apple | probe program | Step 2 |
| `size_t` and `uint64_t` are distinct types on Apple | probe program; no per-type overloads found in `reflection.h` | 1.8 (audit) |
| MSVC-only CRT calls (`_stricmp`, `localtime_s`) | **already fixed** (uncommitted: `graphics.cpp`, `bundle.cpp`, `reflectionio.h`) | done |
| Mac has no CMake, no Xcode (Command Line Tools only); Python 3.9 has no `clang` module | tool checks | Step 0 |

## Step 0: Baseline and tooling

* 0.1 On Windows: clean build + `ctest`, record the number of passing tests. This is the number every
  later step must match.
* 0.2 On the Mac (**done 2026-11-21**): Xcode 26.6 at `/Applications/Xcode-26.6.0.app`, selected with
  `xcode-select -s` (macOS 26.5 and iOS 26.5 SDKs); CMake 4.4.5 from cmake.org at `/Applications/CMake.app`
  (Xcode doesn't include CMake; Homebrew isn't installed). Put `/Applications/CMake.app/Contents/bin` on
  `PATH` (CMake menu: Tools → How to Install For Command Line Use).
* 0.3 On the Mac (**done**): Python 3.13.1 from python.org (same version as the Windows machine; Apple's 3.9.6
  is end-of-life and is left alone). Create the reflectgen venv with that Python:
  `python3.13 -m venv tools/reflectgen/.venv`, then `.venv/bin/pip install -r tools/reflectgen/requirements.txt`
  (`clang==21.1.7`, bindings only). The venv layout is `.venv/bin/python3` here, not `.venv/Scripts/python.exe`.
  Set `LIBCLANG_PATH=/Applications/Xcode-26.6.0.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib`.
* **Verified so far (spike, 2026-11-21):** `cmake -G Xcode` configures the whole project (blocked only by the
  hard-coded Windows venv path in `cmake/ReflectGen.cmake:46`); Blend2D and asmjit build under Xcode;
  reflectgen loads and discovers 73 of 75 headers but cannot parse them yet (see Step 4.1).
* **Done when:** baseline test count written down (0.1); `cmake --version`, `xcodebuild -version` work.

## Step 1: Standard-C++ conformance (no design changes)

Each item is a few lines. Windows behavior must not change.

* **1.0 Portability sweep script.** Commit the spike's method as `tools/portcheck/portcheck.sh`
  (Apple Clang `-fsyntax-only` over every header and source, with the Win32 stub headers it generates).
  It exists so each later step can show "errors went down, none added". Remove it when Step 4 makes the real
  Mac build possible.
* **1.1 CRT calls** (done): `_stricmp`, `localtime_s`. Add a grep check to the sweep for
  `_stricmp|_strnicmp|_wcsicmp|localtime_s|gmtime_s|_wfopen|fopen_s|sprintf_s|strcpy_s|_snprintf|_strdup`
  outside `src/win32/`.
* **1.2 `classinfo` lookup order.** Declare `classinfo(const std::type_info&)` before the templates that call
  it in `reflection.h` (forward declaration near the top of the namespace). Alternative if the forward
  declaration isn't enough: make the call dependent (`this->`/ADL via a dependent argument). Check:
  `reflection.h` compiles alone under Clang; reflection tests unchanged on Windows.
* **1.3 `RunLoop` incomplete in `delegate.h`.** Move the body that calls `runLoop.post(...)` out of line
  (define it after `runloop.h` is included, or make the parameter type dependent). Watch the include cycle
  (`runloop.h` includes `delegate.h`). Check: `delegate.h` compiles alone under Clang; async delegate tests
  pass.
* **1.4 Wide-path streams.** `widePath(filePath_)` → `std::filesystem::path`. Same for any other
  `ifstream`/`ofstream`/`fopen` taking a wide string. Check: `test_componentsettings`, `test_bundle`.
* **1.5 Path separators.** Replace `"\\"` joins with `std::filesystem::path` operations (or `'/'`, which Windows
  accepts). Replace the `path[1] == ':'` absolute-path test in `fontmanager.cpp` with
  `std::filesystem::path::is_absolute()`. Check: path tests; `settings1`/`fonts1` examples still find
  their files.
* **1.6 `svgandme` include.** Add `#include <iostream>` at the top of `svgandme/svg/core_table.h`, or better, include it from
  `src/svgimage.cpp` before the svgandme header, so the vendored tree stays an untouched pinned commit.
  Check: `test_svgimage`.
* **1.7 Type names.** Make `qualifiedTypeName()` (and `demangleTypeName()` if it's affected) use
  `abi::__cxa_demangle` under `#if defined(__GNUC__) || defined(__clang__)`, keeping the MSVC prefix-strip
  branch. Also check whether `.newui` files store these names; if they do, add a test that writes a file
  and compares it with a stored Windows-produced golden file. Check: reflection tests on Windows;
  on the Mac, a small test that `qualifiedTypeName(typeid(newui::Size))` is `"newui::Size"`.
* **1.8 Integer type audit.** Search `reflection.h`/`reflectionio.h` for places that pick behavior by integer
  type (templates on `std::size_t`, `std::uint64_t`, `long`, `unsigned long`). The spike found none that
  differ, so this is a short confirmation: add a unit test that round-trips a `size_t`, `uint64_t`,
  `int64_t` and `long` property through `ObjectWriter`/`ObjectReader`. Check: test passes on Windows now,
  and on the Mac at Step 4.
* **1.9 `clang-cl` build (optional, high value).** If Visual Studio's Clang tools are installed, build with
  `-T ClangCL` and fix what it finds. It's the cheapest way to catch real Clang-vs-MSVC errors while
  Windows verification is still available.
* **Done when:** sweep shows none of 1.1-1.8's error categories left; Windows build and tests match the
  Step 0 baseline.

## Step 2: UTF-16 text (`wchar_t` → `char16_t`)

Do this on Windows, before any Apple code exists. Wrong offsets would otherwise appear only at runtime.

* 2.1 Add portable UTF-8⇄UTF-16 helpers in `utils.h/.cpp` with tests (valid text, surrogate pairs,
  invalid sequences, empty strings). Keep the Windows `MultiByteToWideChar` versions behind them until
  the new ones match them on a test corpus, then delete them.
* 2.2 Introduce `using u16string = std::u16string;` and migrate leaf code first: `PieceTree`,
  `TextHistory`, `TextStorage` (`piecetree`, `texthistory`, `text`). Check after each file: their test files.
* 2.3 Migrate `controls.cpp`, `viewstyle`, `themedata`, `uicolormanager` (13-36 occurrences each).
* 2.4 Migrate `clipboardmgr` and `dragndrop` last; these are the Windows API boundary where
  `reinterpret_cast<const wchar_t*>(u16.data())` is needed (sizes match on Windows, so it's safe).
  Keep casts in one helper (`toWide()`/`fromWide()` in `src/win32/`).
* 2.5 `L"..."` literals → `u"..."`; remaining `std::wstring` only inside Win32-specific files.
* **Done when:** `grep -rn "wstring\|wchar_t\|L\"" include src` hits only files that call Win32 APIs; all
  text/clipboard/dnd tests pass; hand-test: type, select, copy/paste Unicode (emoji, CJK) in a TextField.

## Step 3: Remove Win32 from public headers

Detailed design is in the main plan, 2.2. Order by dependency, leaves first, and merge after each
group:

* 3.1 **Create `include/newui/platform.h`** with `NEWUI_PLATFORM_*` macros and the opaque `NativeWindow`/`NativeView`
  handle types. Add `include/newui/win32/` for Windows-only conversions. No other change yet.
  * **Platform detection comes from the compiler, never from CMake `-D` flags.** `platform.h` derives
    `NEWUI_PLATFORM_WIN32` from `_WIN32`, and `NEWUI_PLATFORM_MACOS`/`NEWUI_PLATFORM_IOS` from `__APPLE__` plus
    `TARGET_OS_IOS`/`TARGET_OS_OSX` (`<TargetConditionals.h>`). Because the compiler sets these itself, the build
    and reflectgen's separate libclang parse always agree, as long as libclang gets the right target and sysroot.
  * **Prefer separate files to `#if`.** Platform differences live in `src/win32/`, `src/macos/`, `src/ios/`,
    chosen by CMake, behind the shared interfaces. `#if NEWUI_PLATFORM_*` in shared code is only for tiny
    cases (like `localtime_s`/`localtime_r`, or demangling in `qualifiedTypeName()`).
  * **Check:** `grep -rn "NEWUI_PLATFORM\|_WIN32\|__APPLE__" include src` hits `platform.h`, `src/win32|macos|ios/`
    and a handful of small helpers only. Add this grep to the portability sweep (1.0).
* 3.2 **Value types:** `geometry.h` (move `POINT`/`RECT` conversions to `win32/geometryconv.h`), `color.h`
  (`COLORREF`), `displayunit.h` (`HWND` → `NativeView`), `keyboard_constants.h` (remove
  `SHIFT_KEY_DOWN`/`GetKeyState`), `custom_message_constants.h`.
* 3.3 **Stop `newui.h` including `<windows.h>`**. Each `.cpp` that needs Windows includes it
  itself (through a `src/win32/winheaders.h` that sets `NOMINMAX`/`UNICODE` first, same sequence as today).
  Expect a wave of missing-include fixes. This is the step that surfaces the most hidden dependencies.
* 3.4 **Handle-taking APIs:** `Cursor` (`HCURSOR`), `RunLoop` (`TimerHandle`, `runModal(HWND...)`),
  `Dialog`/`ContextMenu`/`ClipboardScope`/`PopupTool` constructors and `show*` functions, `Frame`/
  `RootView` accessors (`frameHandle()`, `windowHandle()`).
* 3.5 **COM/WRL out of public headers:** `RootView::comDropTarget_`, `dragndrop.h`'s `COMDropTarget`,
  `TextDataObject`, `enum_format_etc.h` → `src/win32/`. Public `DropTarget`/`DropSource` stay.
* 3.6 **Peers:** `FramePeer`/`RootViewPeer`/`PresentSurface`/`RunLoopBackend` as in the main plan; move
  `WndProc`/`handleMessage` bodies into `src/win32/`. `RootView::mouseDown()`, `keyEvent()` and the other
  input entry points stay public/protected exactly as they are, so `TestableRootView` tests keep working.
* 3.7 **New async APIs** (added alongside the blocking ones, Windows implements them on top):
  `Dialog::showModalAsync`, `showOpenFileAsync` etc., `DropSource::onDragEnded`, a `PopupHost` abstraction.
* **Done when:** `grep -rn "windows.h\|HWND\|HDC\|HCURSOR\|HMENU\|WRL" include/` finds nothing outside
  `include/newui/win32/`; Windows build and tests pass; examples run as before (`example1`, `controls1`,
  `dragdrop1`, `popuptool1`, `clipboard1` hand-tested because tests don't cover everything).

Each sub-step above is its own merge. 3.3 and 3.6 are the largest and may need splitting by header group.

## Step 4: First Mac build (needs Step 0 tooling)

* 4.1 Restructure `CMakeLists.txt`: common sources, `if(WIN32)` block (`src/win32/*`, Windows libs),
  `if(APPLE)` placeholder. Make the reflectgen Python path platform-aware (`.venv/bin/python3` vs
  `Scripts/python.exe`, in both `ReflectGen.cmake` and `ScriptGen.cmake`) and pass libclang the Apple SDK
  flags (no `NEWUI_PLATFORM_*` defines are needed, see 3.1). **Spike finding:** without
  `-isysroot <SDK>` and `-isystem <SDK>/usr/include/c++/v1`, libclang called from Python can't find `<cstdint>`
  and every parse fails. Per target: macOS `-isysroot $(xcrun --sdk macosx --show-sdk-path)`; iOS adds
  `-target arm64-apple-ios16.0` with the `iphoneos` SDK (`-target arm64-apple-ios16.0-simulator` with
  `iphonesimulator` for the simulator). Also set `LIBCLANG_PATH` (or have CMake find Xcode's
  `libclang.dylib` through `xcrun`). Guard WIL and Windows-only tests (`test_gdipresentsurface`,
  `test_dxgipresentsurface`, `test_dragndrop`, ...) with `WIN32`.
* 4.2 Skip the reflectgen sizeof probe when `CMAKE_SYSTEM_NAME` is `iOS` (decided), keep it on macOS.
  **Spike finding:** the probe doesn't build on macOS yet either: it includes `reflection.h`, which pulls in
  `utils.h` and so `windows.h`, and hits the `classinfo`/`RunLoop` errors (1.2, 1.3). Its own comment says
  `reflection.h` needs nothing beyond its own includes; making that true again is part of Step 3.
* 4.3 Configure with `cmake -G Xcode`. Target: the **shared core** (everything not under `src/win32/`) compiles
  as a static library on macOS with Apple-side stub peers that abort when called.
* 4.4 Run the non-UI tests on the Mac (`piecetree`, `texthistory`, `reflection*`, `layout`, `viewstyle`,
  `color`, `bundle`, `componentsettings`...). Fix Mac-specific failures (path, locale, float formatting).
* 4.5 Blend2D with JIT, asmjit builds on Apple Silicon. `BLEND2D_NO_JIT` config checked for iOS in Step 6+.
* **Done when:** the library builds in Xcode; non-UI tests green on macOS; reflectgen generates the same
  registration `.cpp` as on Windows (diff them; differences must be explained).

After Step 4, Phase 1 of the main plan (NSWindow, NSView, present, RunLoop) starts.

## Tracking

| Step | Status | Verified on Windows | Notes |
|---|---|---|---|
| 0 Baseline/tooling | not started | | |
| 1.1 CRT calls | done in working tree | no | needs Windows build |
| 1.2 `classinfo` order | not started | | |
| 1.3 `RunLoop` incomplete | not started | | |
| 1.4 Wide-path streams | done in working tree (`componentsettings.cpp`); Windows build+test pending | | |
| 1.5 Path separators | `bundle.cpp`/`bundle.h` joins now use `/` (2 test expectations in `test_bundle.cpp` updated); `fontmanager.cpp` deliberately left (Windows registry code, replaced per platform in Step 4). Windows build+test pending | | |
| 1.6 svgandme include | done in working tree (`src/svgimage.cpp`); Apple Clang syntax check of `svgimage.cpp` now has 0 errors (4+ without the fix); Windows build pending | | |
| 1.7 Type names | done in working tree: new `readableTypeName()` in `utils.h` (`__cxa_demangle` on Clang/GCC), used by `demangleTypeName`, `extractNamespace`, `reflection.h` `qualifiedTypeName` and `automation.cpp` `typeName`; test `Reflection.TypeNameHelpers`; Windows build+test pending | | |
| 1.8 Integer audit | done in working tree. Audit found a real problem: `Property::readValue`/`writeValue` matched `uint64_t`/`unsigned long long` and then `any_cast<uint64_t>`, so `size_t` (`unsigned long` on Apple) and `long` were skipped or would throw; now matched by exact type in `reflection.cpp`; test `ReflectionIO.EveryIntegerTypeRoundTripsThroughObjectWriterAndReader`; Windows build+test pending | | |
| 1.9 clang-cl (optional) | skipped (user hasn't said whether the Clang tools are installed) | | |
| 2.1 UTF-16 helpers | done (verified on Windows): `utf8ToUtf16`/`utf16ToUtf8` in `utils.h/.cpp`, tests `Utf16.*` in `test_utils.cpp`; 2.2 text core -> `std::u16string` (`piecetree`, `texthistory`, `text`, plus its consumers `controls`, `textfolding`, `automation`, `binding`, `reflection`, demos/examples and their tests), done in working tree, Windows build+test pending (`PieceTree` tests pass natively on Apple Clang). 2.2 verified on Windows. 2.3 done in working tree (not the plan's literal list: those files only held UxTheme class names, now `std::string` in the public API, converted to wide at `OpenThemeData`; dead `std::wstring` property branches removed from `reflection`/`binding`/`automation`), Windows build+test pending. 2.4 done in working tree (`ClipboardManager::setText/getText/textMimeData` and the drag-and-drop text API now `u16string`; mime types, format names and file paths stay wide; the temporary `toWideString`/`fromWide` bridges in `controls.cpp`/tests removed; the examples and demos that still used wide text brought over too), Windows build+test pending. 2.5 grep audit done: every remaining `wstring`/`wchar_t`/`L"` hit is Win32 boundary code (`clipboardmgr`, `dragndrop`, `dialogs`, `uicolormanager`, `filewatcher`, `automation` pipe/SDDL, `text.cpp` DirectWrite, `utils` `utf8ToWide`/`toWide` helpers) or a comment; no source change needed. Hand-test of emoji/CJK in a TextField still pending on Windows | | |
| 3 Public headers | not started | | |
| 4 First Mac build | not started | | |

## Risks specific to this plan

* **No Windows machine here.** Claude can't compile MSVC code from this Mac. Steps 1-3 therefore depend on the
  user running the Windows build and tests after each change, and each step should be small enough
  that a failure points at one change.
* **The sweep uses stubs.** It undercounts: errors hidden behind cascades appear only after the previous
  layer is fixed. Re-run it after every step and expect new categories to appear.
* **Template code is only checked where it's instantiated.** Syntax-only passes miss errors in templates nobody
  uses. The real Step 4 build and reflectgen's generated output will find more.
* **Merge conflicts.** Steps 2 and 3 touch most files. Merge each step quickly and keep branches short.

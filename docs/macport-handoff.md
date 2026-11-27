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
Slice 2a verified on Windows. Slice 3 in tree, Windows build pending: `Frame::frameHandle()`/`RootView::windowHandle()` return
`NativeWindowHandle`; `RootView(NativeWindowHandle, NativeInstanceHandle, ...)`, `PopupTool`/`CalloutTool` ctors likewise
(new `NativeInstanceHandle`, `toNativeInstance/toHINSTANCE`). Every call site in src/examples/demos/unittests was wrapped with
`newui::win32::toHWND(...)` by script (harmless where the callee takes a NativeWindowHandle). Private members (`frameHandle_`,
`viewHwnd_`, `externalParentHwnd_`, ...), `getTarget(HWND)`, `WndProc` and `Application::instanceHandle()` stay Win32 until 3.6.

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

## 3.4 done, 3.5 in progress (2026-11-22)

3.4 slices 1-3 verified on Windows. 3.5 in tree, Windows build pending: `dragndrop.h` keeps only `DropEffect`, `VirtualFile`,
`DropSource`, `DropTarget`; everything OLE (`extract*`, `TextDataObject`, `VirtualFileDataObject`, `COMDropSource`, `COMDropTarget`,
`StartDragOperation`, `StartVirtualFileDrag`, `toDropEffect*`) moved to `include/newui/win32/dragndrop_com.h`;
`enum_format_etc.h` moved to `include/newui/win32/`. `RootView::comDropTarget_` is a `std::shared_ptr<void>` holding a
`DropTargetRegistration` (defined in `rootview.cpp`). Not yet done from 3.5: `ClipboardScope`/`clipboardmgr.h` (UINT, HWND, HGLOBAL
types) and `MenuItem::bitmap()` (HBITMAP).

3.5 OLE part verified on Windows. 3.5 clipboard + bitmap in tree, Windows build pending: `clipboardmgr.h` uses `ClipboardFormat`
(`std::uint32_t`) instead of `UINT`, `handleRenderAllFormats(NativeWindowHandle)`; the `detail::` HGLOBAL/`ClipboardScope`/CF_HTML helpers moved to
`include/newui/win32/clipboard_win32.h`; `MenuItem::bitmap()/setBitmap()` use new `NativeBitmapHandle` (`toNativeBitmap/toHBITMAP`).
Then remaining before 3.3: 3.6 (peers, WndProc, `Application::instanceHandle()`, private HWND members, `getTarget(HWND)`), 3.7.

3.5 clipboard + bitmap verified on Windows (2026-11-22). 3.6a in tree, Windows build pending: `Application::instanceHandle()` /
`dummyWindowHandle()` return `NativeInstanceHandle`/`NativeWindowHandle`; `Frame/RootView::getTarget` take `NativeWindowHandle`;
private members renamed/retyped (`Frame::frameHandle_`, `RootView::viewHandle_`, `externalParentHandle_`, `externalInstanceHandle_`);
`PopupFrame/ToolFrame::initialize(NativeWindowHandle)`, `owner_`, `PopupTool::ownerHwnd_`, `enableDarkModeForWindow(NativeWindowHandle)`.
Still Win32 in public headers for 3.6b+: `handleMessage/WndProc/WNDPROC` (Frame, RootView, PopupFrame, ToolFrame, Dialog, Application),
`fromWindow(HWND)`, `PresentSurface::setWindow/window/paint(HDC)` + GDI/DXGI surfaces, `graphics.h` HDC/HBITMAP, `translateKeyEventInfo`,
`UINT commandId` (menus), `text.h systemCaretWidth`, `utils.h translateButtonMask`, `popupframe.h MouseHookProc/kDismissMessage`.

3.6a verified on Windows. 3.6b in tree, Windows build pending: new `NativeDeviceContextHandle` (`platform.h`, `toNativeDC/toHDC`);
`PresentSurface::setWindow/window/paint` and Gdi/Dxgi surfaces use `NativeWindowHandle`/`NativeDeviceContextHandle`; `Image::memDC()`
returns `NativeDeviceContextHandle`, its `dibSection_/memDC_/oldBitmap_` and `GdiPresentSurface`'s members are opaque; call sites in
popuptool.cpp/menus.cpp and unittests/test_graphics.cpp convert with `toHDC`.

3.6b verified on Windows (2026-11-26). 3.6c in tree, verified building on Windows (examples/demos included): `platform.h` gained
`NativeMessage/NativeWParam/NativeLParam/NativeLResult/NativeWindowProc` (bit-compatible with UINT/WPARAM/LPARAM/LRESULT/WNDPROC on x64 only);
`handleMessage`, `Frame/RootView::WndProc`, `Application::DummyWndProc`, `RootView::fromWindow`, `updatePenState`, the `WNDPROC` members use them
(definitions convert to `HWND` at the top; `lpfnWndProc` sites use `reinterpret_cast<WNDPROC>`). `platform.h` is now included from `newui.h`
and every header that uses a `Native*` type. `Image::memDC()` needs `//@reflect ignore=true`. Next: small leaks (`translateKeyEventInfo`, menu
`UINT commandId`, `systemCaretWidth`, `translateButtonMask`, `popupframe.h MouseHookProc/mouseHook_`), then 3.7, then 3.3.

3.6c and the small leaks verified on Windows (2026-11-26): `translateKeyEventInfo`/`translateButtonMask`/`translateKeyMask`, menu `commandId`/`dispatchCommand`,
`Caret::systemCaretWidth`, `PopupFrame::mouseHook_/MouseHookProc/kDismissMessage` are now opaque (`utils.h` includes `platform.h`). Remaining: 3.7, then 3.3
(drop `<windows.h>` from `newui.h`).

3.3 in tree, Windows build pending (2026-11-26): `newui.h` no longer includes `<windows.h>`; the SDK setup moved to `include/newui/win32/winheaders.h`
(pulled in by `nativehandles.h`, the PCH `src/newuicommon_precomp.h`, `src/main.cpp`, and an explicit include added to every example/demo/unittest
file). `viewstyle.h` lost its uxtheme classes: `ThemedViewStyle` and every `Themed*/Fluent*` style are in `include/newui/win32/themedstyles.h`.
`DispatchMenuMeasureItem/DrawItem` moved to `include/newui/win32/menus_win32.h`. Mac check: every public header parses standalone with Apple Clang
(no windows.h) EXCEPT `controls.h` (and `textfolding.h` through it), which still includes `win32/themedstyles.h` and exposes `Themed*Style` types in its
public API (`Progress::fillState`, `ScrollBar`/`Edit`/`TabControl` members). That needs a design decision (platform-neutral style layer) before the Mac
control port. Check script: scratchpad `hdrcheck.sh` (syntax-checks each header with `clang++ -fsyntax-only`, repo path quoted).

3.3 verified building on Windows (2026-11-26) after adding `win32/themedstyles.h` includes to `viewstyle/rootview/menus/themedata.cpp` and
`test_viewstyle/test_menus.cpp`. Unit tests and example hand-tests not yet reported. Open: platform-neutral style layer for `controls.h`;
3.7 (new async APIs); then Step 4 (first Mac build).

3.6c, small leaks and 3.3: unit tests and example hand-tests OK on Windows (2026-11-26). Starting Step 4.

## Step 4 started (2026-11-26): first Mac build

Done and verified on this Mac (Xcode 26.6, CMake 4.4.5, `out/macos`, Debug): `libnewui.a` builds; `ctest` 78/78 pass.

    python3 -m venv tools/reflectgen/.venv && tools/reflectgen/.venv/bin/pip install -r tools/reflectgen/requirements.txt   # once
    cmake -S . -B out/macos -G Xcode
    cmake --build out/macos --config Debug -- -quiet
    ctest --test-dir out/macos -C Debug
    git checkout VERSION.state   # every build bumps this tracked file

* 4.1: `CMakeLists.txt` splits `newui` into a shared list and an `if(WIN32)` list (a `.cpp` moves up once it no longer needs `<windows.h>`);
  PCH, Windows libs, app/examples/demos/automation are WIN32-only. reflectgen/scriptgen use `.venv/bin/python3` off Windows, Xcode's
  `libclang.dylib` (`xcode-select`), SDK flags, and a new `generate_reflection.py --exclude` (Apple skips `include/newui/win32`,
  `controls.h`, `textfolding.h`). `reflectgen_lib` ignores libc++'s inline namespace (`std::__1`). `winheaders.h` is empty off Windows.
* 4.2: sizeof probe builds on macOS; skipped for iOS (iOS configure untested).
* 4.3: library builds, but there are NO Apple stub peers yet; the 30 Windows-dependent `.cpp` files are simply left out.
* 4.4: Mac `newui_tests` = version, delegate, curveinterpolation, namemanager, command, undostack. Others need either a left-out source
  (link: `utf16ToUtf8`/`utf8ToUtf16` in `utils.cpp`, `toBLCompOp`, `View`, `DisplayValue`, `FontManager`, `UIColorManager`, `RunLoop`) or have
  Windows code in the test file. Next: move the pure helpers in `utils.cpp` into a shared file, then `displayunit`, `pen`, `view`.
* Windows build and tests OK with these changes (2026-11-26).
* Not checked: generated registration `.cpp` diff vs Windows; Release config; the 493 compiler warnings.

Next step done (2026-11-26), Windows build pending: pure code moved out of Windows-only files - new `src/stringutils.cpp` (UTF-8/16,
`normalizePath`, type names), `src/compositing.cpp` (`toBLCompOp`), `src/displayunit.cpp` and `src/graphics.cpp` and `src/pen.cpp` are now shared,
with their Windows halves in new `src/win32/graphics_win32.cpp` and `displaymetrics_win32.cpp` (`DisplayMetrics::forWindow/forDpi`). Mac tests add
piecetree, enumserialization, delegateserialization (106 total at time of writing: check ctest). Remaining link blockers: `FontManager`
(property, shapes, font, displayunit), `View`/`Cursor`/`RootView` (layout, viewpath, splitter, proxies, segmentedcontrol), `UIColorManager`, `RunLoop`.

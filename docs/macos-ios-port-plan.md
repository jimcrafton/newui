# newui → macOS / iOS port plan

Status: draft, 2026-11-20. Nothing implemented yet.

## 1. Goal and shape

Use the same model as Windows: a thin native window, one native view that backs `newui::RootView`,
and everything else (`SubView`, controls, layout, styles) drawn by newui into a Blend2D buffer.

| newui           | Windows today                     | macOS                                     | iOS / iPadOS                                       |
|-----------------|-----------------------------------|-------------------------------------------|----------------------------------------------------|
| `Application`   | message pump, dummy HWND          | `NSApplication` + `NUIAppDelegate`        | `UIApplicationMain` + `NUIAppDelegate` / `NUISceneDelegate` |
| `Frame`         | top-level `HWND` (WS_OVERLAPPEDWINDOW) | `NSWindow` + `NUIWindowDelegate`     | `UIWindow` (one per `UIWindowScene`) + `NUIViewController` |
| `RootView`      | child `HWND`, `WndProc`           | `NUIRootNSView : NSView` (layer-backed, flipped) | `NUIRootUIView : UIView`                    |
| `PresentSurface`| GDI BitBlt / DXGI swap chain      | `CALayer.contents` = IOSurface (or CGImage) | same as macOS (CALayer)                          |
| `RunLoop`       | `GetMessage`/`PeekMessage` pump   | `CFRunLoop` (+ `[NSApp run]` on main)     | `CFRunLoop` (main loop owned by UIKit)             |
| `SubView`       | windowless                        | windowless (unchanged)                    | windowless (unchanged)                             |

Language: C++17 for shared code; Objective-C++ (`.mm`, ARC on) only in files under
`src/apple/`, `src/macos/`, `src/ios/`. No Swift.

What helps: controls and `SubView`s are already windowless and draw only through Blend2D;
`PresentSurface` already separates "render pixels" from "get pixels on screen"; text already uses
UTF-16 offsets, the same as CoreText; the clipboard already has a MIME mapping layer.

What hurts: Win32 types are in nearly every public header (`newui.h` includes `<windows.h>`,
`<ole2.h>`, `<commctrl.h>`; `geometry.h` converts to `POINT`/`RECT`; `Frame`, `RootView`,
`PresentSurface`, `RunLoop`, `Cursor`, `Dialog`, `ContextMenu`, `DisplayMetrics` take `HWND`/`HCURSOR`/`HMENU`;
`rootview.h`/`dragndrop.h` hold WRL `ComPtr`s). Text layout and rendering are DirectWrite/D2D. The
code has only been built with MSVC.

## 2. Phase 0: platform split on Windows (do this first)

Do this on Windows, with the Windows build and tests passing after every step. Most of the
risk is here, and it's far easier to verify while only one platform exists.

### 2.1 Layout

```
include/newui/            platform-neutral public headers only
include/newui/platform/   platform.h (NEWUI_PLATFORM_WIN32 / _MACOS / _IOS, NativeWindow etc.)
src/                      shared C++
src/win32/                everything that includes <windows.h>
src/apple/                shared macOS+iOS: CoreText, CoreFoundation, IOSurface present, RunLoop (CF), paths
src/macos/                AppKit (.mm)
src/ios/                  UIKit (.mm)
```

### 2.2 Remove Win32 from public headers

* `newui.h`: stop including `<windows.h>`. Add `platform.h` with opaque handles:
  ```cpp
  struct NativeWindowTag; using NativeWindow = NativeWindowTag*;   // HWND / NSWindow* / UIWindow*
  struct NativeViewTag;   using NativeView   = NativeViewTag*;     // HWND / NSView*  / UIView*
  ```
  Keep Win32-only accessors (`frameHandle()`, `windowHandle()`) but return `NativeWindow`/`NativeView`,
  and add `win32::toHwnd(...)` helpers in a `newui/win32/` header only Windows code includes.
* `geometry.h`: move `POINT`/`RECT` conversions to `newui/win32/geometryconv.h`.
* `Frame` / `RootView`: move `WndProc`, `handleMessage`, `defaultWndProc_`, `viewHwnd_`,
  `comDropTarget_`, `preCreateHints()` behind a pimpl peer:
  ```cpp
  class FramePeer    { virtual bool create(Frame&) = 0; virtual void setTitle(...)=0; virtual void setBounds(...)=0; ... };
  class RootViewPeer { virtual bool create(RootView&, NativeView parent)=0; virtual void invalidate(const Rect&)=0;
                       virtual void setCursor(const Cursor&)=0; virtual Point localToScreen(Point)=0; ... };
  ```
  `Win32FramePeer`/`Win32RootViewPeer` hold the existing code. The `mouseDown()`/`keyEvent()`
  entry points on `RootView` stay as they are, so every peer feeds the same functions (and the
  existing `TestableRootView` tests keep working).
* `PresentSurface`: replace `setWindow(HWND)`/`paint(HDC, ...)` with `attach(NativeView)` and a
  platform-specific `Win32PresentSurface::paint(HDC)` subclass hook.
* `RunLoop`: `TimerHandle = UINT_PTR` becomes `std::uintptr_t`; `runModal(HWND, HWND, ...)` takes
  `Frame&`/`NativeWindow`; `postTaskMsg()`/pump move into a `RunLoopBackend`.
* `Cursor`, `ContextMenu::show`, `Dialog::show*`, `DisplayMetrics::forWindow`, `ClipboardScope`,
  `PopupTool(HWND, HINSTANCE, ...)`: take `RootView&`/`Frame*`/`NativeView` instead of handles.
* `keyboard_constants.h`: remove `SHIFT_KEY_DOWN` (calls `GetKeyState`).
* Theme-specific styles (`ThemedViewStyle` and subclasses, which use uxtheme): Windows-only
  file; register them only on Windows (see 4.6).

### 2.3 Text storage: `wchar_t` is 32-bit on Apple

`TextStorage`, `PieceTree`, `TextHistory`, clipboard and `viewstyle`/`themedata` use `std::wstring`
and `L"..."`. On Windows that's UTF-16; on macOS/iOS `wchar_t` is UTF-32, so every offset would
disagree with CoreText's UTF-16 indices, and the text tests would pass while caret and selection
positions came out wrong.

**Fix:** switch to `std::u16string`/`char16_t` (`u"..."` literals), with
`reinterpret_cast<const wchar_t*>` only at Win32 API calls (sizes match there). Do it as a separate
mechanical step while still on Windows, before any Apple code exists. Replace
`MultiByteToWideChar`/`WideCharToMultiByte`/`CharLowerBuffW` in `utils.cpp` with a portable
UTF-8⇄UTF-16 converter (`std::codecvt` is deprecated, so write a small one or vendor `utfcpp`).

### 2.4 Get the shared code building with Clang

Do this on Windows first with `clang-cl`, then on macOS with Apple Clang. Known problem areas:
* `reflection.h`'s `qualifiedTypeName()` removes MSVC's `"class "` prefix from `typeid().name()`.
  Clang returns mangled names (`N5newui4SizeE`), so use `abi::__cxa_demangle`. Check
  `demangleTypeName()` and anything that writes type names into `.newui` files or the reflectgen
  output. If a mangled name gets into a saved file, those files won't be portable.
* Integer type identity: on LP64 Apple, `long` is 64-bit and `std::size_t` (`unsigned long`) is a
  **different type** from `std::uint64_t` (`unsigned long long`); on Windows `size_t ==
  uint64_t`. Reflection or serialization overload sets and type-registration maps keyed on these
  will hit ambiguous overloads or "unregistered type" errors. Audit `reflection.h`/`reflectionio.h`
  and add explicit specializations.
* `__uuidof`, `_com_ptr_t`, `_COM_SMARTPTR_TYPEDEF`, WRL, `sprintf_s`, `#pragma comment(lib, ...)`
  (Windows-only, keep them in `src/win32`).
* MSVC permissiveness: missing `typename`/`template` keywords, two-phase lookup, binding
  temporaries to non-const references, `enum` forward declarations without an underlying type.
  The 3.8k-line `reflection.h` template code will likely produce most of the Clang errors.
* Name collisions with Apple headers: `MacTypes.h` defines global `Point`, `Rect`, `Size`
  (`typedef long`), `Style`, `Fixed`; ObjC adds `id`, `nil`, `YES`, `NO`, `BOOL`, `Nil`, `Protocol`. Code with
  `using namespace newui;` (reflectgen output does this) that also sees
  `<Cocoa/Cocoa.h>`/`<CoreServices>` will get ambiguous `Point`/`Rect`. This is the same problem as
  the `Font`/`<comdef.h>` collision in `text.h`. **Rule:** Apple framework headers are included
  only in `src/apple|macos|ios/*.mm`, never in a public header or the PCH. Headers that must name
  ObjC types use
  `#ifdef __OBJC__ @class NSWindow; #else struct NSWindow; #endif`-style opaque declarations, or
  `NativeWindow`.
* Source encoding: Clang assumes UTF-8. Check for files saved as Windows-1252 or UTF-16.

## 3. Build system (CMake → Xcode)

* Configure: `cmake -S . -B build-mac -G Xcode` and
  `cmake -S . -B build-ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_DEPLOYMENT_TARGET=16.0`.
  Proposed minimums: macOS 12, iOS 16 (Metal, IOSurface-as-contents, `UTType`, modern scene APIs).
* `enable_language(OBJCXX)` (CMake ≥3.16; already require 3.24). Per-file
  `-fobjc-arc`: set `XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES` on the target, or the
  `COMPILE_OPTIONS` source property on the `.mm` files.
* Split the `add_library(newui ...)` source list into common + `if(WIN32)` / `if(APPLE AND NOT IOS)` / `if(IOS)` blocks.
  Link frameworks via `"-framework AppKit"` etc.: macOS `AppKit QuartzCore CoreText CoreGraphics
  IOSurface UniformTypeIdentifiers CoreServices`, iOS `UIKit QuartzCore CoreText CoreGraphics IOSurface
  UniformTypeIdentifiers MobileCoreServices`.
* Vendored deps: WIL is Windows-only (only include it under `WIN32`). zlib, json5, svgandme,
  googletest build as-is.
* **Blend2D JIT:**
  * macOS arm64 with hardened runtime: asmjit needs `MAP_JIT` plus the
    `com.apple.security.cs.allow-jit` entitlement. Without it, a signed or notarized app crashes at
    the first JIT-compiled fill pipeline. Ship an entitlements file and set
    `XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS`.
  * iOS: JIT is not allowed. Build with `BLEND2D_NO_JIT=ON`. The non-JIT pipelines are
    noticeably slower, so measure `RootViewRepaintBenchmark` on a device early. `RepaintMode::Dirty`
    becomes more important there.
  * Universal macOS build (`CMAKE_OSX_ARCHITECTURES="arm64;x86_64"`) works; asmjit supports both.
* **reflectgen / scriptgen** (build-time Python + libclang):
  * Hard-coded `.venv/Scripts/python.exe` → choose `.venv/bin/python3` on non-Windows (or `find_package(Python3)`).
  * libclang: pip `libclang` wheel, or Xcode's
    `$(xcode-select -p)/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib`.
  * It must parse headers with the target's flags (`-isysroot $(xcrun --sdk macosx|iphoneos --show-sdk-path)`,
    `-isystem <SDK>/usr/include/c++/v1`, target triple). Platform macros come from the compiler itself
    (`_WIN32`, `__APPLE__`, `TARGET_OS_*`), not CMake defines, so a correct target is enough to avoid parsing
    the Windows branch. Verified: without the sysroot flags libclang can't find `<cstdint>`.
  * `reflectgen_sizeof_probe` is built and **run** at build time. That's impossible when
    cross-compiling for iOS. Darwin arm64 iOS and macOS share the LP64 ABI, so build the
    probe as a host macOS tool (`ExternalProject` or a separate host build), or emit sizes from
    `static_assert`/`offsetof` tables at compile time instead of running a binary. Decide this
    before the iOS step.
* **App bundles:** `add_executable(... MACOSX_BUNDLE)`, `MACOSX_BUNDLE_INFO_PLIST` template,
  `XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER`, `XCODE_ATTRIBUTE_DEVELOPMENT_TEAM` (iOS needs signing even for the
  simulator for some features). `newui_add_resources()`/`newui_add_bundle_info()` copy into
  `$<TARGET_FILE_DIR>/Resources`. For bundles they must target
  `$<TARGET_BUNDLE_CONTENT_DIR:tgt>/Resources` (macOS) or the flat bundle root (iOS), or use the
  `RESOURCE` target property. A helper `newui_add_app(target SOURCES ... RESOURCES ... INFO ...)`
  hides the difference.
* The `newui_version` target edits the tracked `VERSION.state` on every build. That works in
  Xcode, but the generated `version.h` should be listed as a byproduct so Xcode's
  dependency scanner doesn't rebuild everything every time.
* Examples: most use plain `main()` and Win32 APIs (e.g. `crossthread1` registers its own window
  class). Port a core set first (`example1`, `controls1`, `layout1`, `animation1`, `fonts1`,
  `binding1`, `settings1`); keep the rest `if(WIN32)` until someone ports them.

## 4. Subsystem design and problem areas

Items are ordered roughly by risk. ⚠ = likely hard.

### 4.1 Window and view host (Frame / RootView)

**macOS**
* `MacFramePeer` owns an `NSWindow` (titled|closable|miniaturizable|resizable) and an
  `NUIWindowDelegate`: `windowShouldClose:` → `onClosing` veto, `windowWillClose:` →
  `onClosed`/`onDestroyed`, `windowDidResize:`/`windowDidMove:` → `onSizeChanged`/`onPosChanged`,
  `windowDidChangeBackingProperties:` → `refreshDisplayMetrics()`.
* `NUIRootNSView`: `isFlipped = YES` (top-left origin like Win32), `wantsLayer = YES`,
  `layerContentsRedrawPolicy = NSViewLayerContentsRedrawNever` (newui pushes pixels itself,
  so AppKit should never call `drawRect:`), `acceptsFirstResponder = YES`, implements
  `NSTextInputClient`, `NSDraggingDestination`/`Source`. Set as `window.contentView`.
* ⚠ **Coordinates:** `Frame::setBounds()` currently uses screen pixels with a top-left origin and
  includes the window border. AppKit uses points, a **bottom-left** origin on the primary screen,
  and separates content rect from frame rect. Decide: `Frame::getBounds()` = frame rect in
  top-left-origin *points* (flip using `NSScreen.screens[0].frame.size.height`). Multi-monitor
  layouts with screens above or left of the primary will have negative coordinates; test them.
* App lifecycle: `applicationShouldTerminate:` → `onQueryEndSession`, `applicationWillTerminate:` →
  `onEndSession`/settings `storeAll()`. Decide whether closing the last Frame quits
  (`applicationShouldTerminateAfterLastWindowClosed`). The Windows behavior is effectively yes;
  default to yes for now.
* An unbundled `main()` binary must call `[NSApp setActivationPolicy:NSApplicationActivationPolicyRegular]`
  and `activateIgnoringOtherApps:`, or the window opens behind other windows with no menu bar.
  Bundles are preferred.
* Hosted mode (`RootView(HWND externalParent, ...)` today): becomes `RootView(NativeView parent, ...)`
  and adds `NUIRootNSView` as a subview of someone else's `NSView` (e.g. a plugin host).

**iOS**
* ⚠ **Lifecycle inversion:** `UIApplicationMain()` never returns, and windows can only exist after
  a `UIWindowScene` connects. Today apps build a `Frame`, then call `Application::run()`. Plan:
  * `Application::run()` on iOS calls `UIApplicationMain`. `onStartup` fires from
    `application:didFinishLaunchingWithOptions:`.
  * `Frame::initialize()` before a scene exists queues the Frame; `NUISceneDelegate
    scene:willConnectToSession:` attaches the first pending Frame to its window. On iPad, extra
    scenes map to extra Frames (later).
  * Provide `NEWUI_MAIN(fn)` (or a `newui_main` static lib that owns `main`) so app code has one
    entry point that works on all three platforms.
* `Frame::setBounds/setTitle` are mostly no-ops (a scene fills the screen; title → scene
  title). `onSizeChanged` comes from `viewWillTransitionToSize:` / `layoutSubviews`.
* Safe areas (notch, home indicator) and the on-screen keyboard: add
  `RootView::safeAreaInsets()` and `onSafeAreaChanged`, feed keyboard-frame notifications into it,
  and let layout respect it. There's no Windows equivalent, so this is new API.
* Background/foreground (`sceneDidEnterBackground`): stop repaint and animation timers, flush settings.
  iOS can kill a background app without warning.

### 4.2 Present path (Blend2D buffer → screen)

* `ApplePresentSurface` (shared by both platforms) with two strategies:
  1. **IOSurface (default):** allocate the backing buffer as an `IOSurface` (BGRA,
     `kCVPixelFormatType_32BGRA`) and wrap its locked base address in a `BLImage`
     (`BLImage::create_from_data`, with stride from `IOSurfaceGetBytesPerRow`) so Blend2D renders straight
     into it. Then `layer.contents = (__bridge id)surface`. No copies. `BL_FORMAT_PRGB32` is
     premultiplied BGRA little-endian, which is exactly `kCGImageAlphaPremultipliedFirst |
     kCGBitmapByteOrder32Little`, so no pixel conversion is needed. The opaque `XRGB32` default works too.
  2. **CGImage fallback:** `CGDataProvider` over the BLImage + `CGImageCreate`, set as contents.
     One copy per frame, simplest, good for bring-up.
* ⚠ **Tearing and concurrent reads:** WindowServer may read the IOSurface while Blend2D writes
  the next frame. Use two surfaces; after presenting, copy only the last frame's dirty rect into
  the new back buffer before rendering the new dirty rect (cheap, and keeps
  `RepaintMode::Dirty` valid). `IOSurfaceIsInUse()` tells you when the front buffer is free.
* ⚠ **Retina / scale:** View coordinates are plain pixel `Rect`s today, with `DisplayMetrics`
  (`dpiScale = dpi/96`) and `DisplayValue` (Px/Dip/Dlu) resolving to pixels. Two options:
  * (a) **Keep pixels:** buffer = `bounds × backingScaleFactor`, newui coordinates are
    device pixels, `dpiScale = backingScaleFactor` (2.0 / 3.0), and mouse coordinates get multiplied
    by the scale. Matches the Windows model exactly and needs no layout changes, but any code
    with hard-coded pixel sizes renders at half size on Retina (the same problem those sizes already
    have at 200% scaling on Windows).
  * (b) Points: newui works in points, the BLContext gets a scale transform.
  Recommend (a): consistent with Windows and already exercised by the per-monitor DPI path.
  Set `layer.contentsScale = backingScaleFactor`. iOS behaves the same with `UIScreen.scale` /
  `traitCollection.displayScale`.
* Present timing: drive `scheduleRepaint()` from the run loop as on Windows (below). Use
  `CADisplayLink` (iOS, and macOS 14+ `NSView.displayLink`) for animations, so `AnimationManager`
  runs at the display's refresh rate instead of an idle loop that never sleeps. On ProMotion
  displays (120 Hz) set `preferredFrameRateRange`.
* `PresentBackend::Dxgi` → an optional later `Metal` backend (`CAMetalLayer` + texture upload)
  only if IOSurface turns out too slow. It won't be needed for correctness.

### 4.3 RunLoop ⚠

The Windows `RunLoop` is a custom `GetMessage`/`PeekMessage` pump with an idle phase, `SetTimer`
timers, posted-message wakeups, and hotkey matching inside the pump. On Apple platforms the
main loop belongs to AppKit/UIKit.

* `post()` → a `CFRunLoopSource` (version 0) per RunLoop: append to `tasks_`, then
  `CFRunLoopSourceSignal` + `CFRunLoopWakeUp`. Works cross-thread, keeps FIFO order.
* `postIdle()` → a `CFRunLoopObserver` on `kCFRunLoopBeforeWaiting`: run **one** idle task (to
  match the round-robin `idleProcessing()` semantics). If more remain, signal the source again
  so the loop doesn't go to sleep, letting pending input be processed between idle tasks.
* `postDelayed()` → `CFRunLoopTimer`; `TimerHandle` = the timer pointer as `uintptr_t`.
* ⚠ **Run-loop modes:** during live window resize, menu tracking, scrolling momentum, and drag
  sessions, AppKit runs the loop in `NSEventTrackingRunLoopMode`. If sources, observers and timers
  are added only to the default mode, repaints and animations stop during a resize and the
  window shows stretched or stale content. Add everything to `kCFRunLoopCommonModes`. On iOS,
  `UITrackingRunLoopMode` has the same problem. Note that the Windows `repaintNow()` comment
  about DoDragDrop starving idle tasks mostly goes away here, as long as common modes are used.
* `run()`: on the main thread call `[NSApp run]`. On secondary threads call `CFRunLoopRun()`.
  `quit()` → `[NSApp stop:nil]` + post a dummy `NSEvent` (stop takes effect only after an
  event), or `CFRunLoopStop` on secondary threads. iOS: `run()` = `UIApplicationMain`, and
  `quit()` isn't allowed (apps don't quit themselves); make it a no-op with a debug warning.
* `runModal()` → `[NSApp runModalForWindow:]` (handles owner disabling) or a manual
  `CFRunLoopRunInMode(NSModalPanelRunLoopMode)` loop for `isDone()`. ⚠ iOS has no blocking modals
  (see 4.8).
* `postAndWait()`/`runTillNotified()`: keep the "pump while waiting" behavior by calling
  `CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, true)` in the wait loop; never block the main thread.
* Hotkey `Action`s (`registerAction`): on macOS, map them to `NSMenuItem` key equivalents when they
  belong to a menu, otherwise handle them in `NUIRootNSView performKeyEquivalent:`. Use
  `[NSEvent addLocalMonitorForEventsMatchingMask:]` only for truly global in-app hotkeys. iOS:
  `UIKeyCommand` on the view controller (shown in the iPad ⌘-hold overlay).
* ⚠ **Main-thread only:** AppKit/UIKit windows and views must be created and touched on the main
  thread. Windows allows a UI thread per window, and `RunLoop::runThreaded()` plus `crossthread1`
  (a RootView on a worker thread) depend on that. On Apple, a secondary `RunLoop` can only run
  non-UI work. Add debug asserts (`[NSThread isMainThread]`) in every peer method, and document
  the limit.

### 4.4 Text: layout, rendering, fonts ⚠⚠ (largest work item)

Today `TextRenderer`/`TextLayoutEngine`/`DirectWriteResources` are DirectWrite plus D2D rendering
into a WIC bitmap, then blitted into the BLContext. `Font`/`FontManager` load font *files* found
through the Windows registry into `BLFontFace` for Blend2D-drawn labels and `measureText`.

* Keep the public API (`TextLayoutEngine`, `TextRenderer`, `DirectWriteResources` renamed to
  `TextSystem`). Their `Impl` pimpl becomes per-platform: `text_dwrite.cpp` / `text_coretext.mm`.
  Shared logic (folding, whitespace marks, line caching, `PieceTree` snapshots) moves out of the
  DWrite file into `text.cpp`.
* CoreText mapping:
  * Layout: `CFAttributedString` (UTF-16, so offsets match TextStorage once 2.3 is done) →
    `CTTypesetter` → one `CTLine` per visual line (word wrap via
    `CTTypesetterSuggestLineBreak`), or a `CTFramesetter` per paragraph. Per-line layouts match the
    existing per-line `IDWriteTextLayout` cache.
  * `hitTestPoint` → `CTLineGetStringIndexForPosition`. `hitTestPosition` →
    `CTLineGetOffsetForStringIndex`. `hitTestRange` → per-line offsets (beware bidi: one range can
    map to several rects). `TextFontRun` → `kCTFontAttributeName`/traits per range.
    Underline and strikethrough are drawn manually (CoreText only does underline).
  * Tabs: `kCTParagraphStyleSpecifierTabStops` / `DefaultTabInterval` from `tabWidth`.
  * Rendering: a `CGBitmapContext` over the private buffer (`kCGImageAlphaPremultipliedFirst |
    kCGBitmapByteOrder32Little`, which is the same layout as `BL_FORMAT_PRGB32`), flip Y
    (`CGContextTranslateCTM`/`ScaleCTM`), `CTLineDraw`, then blit into the BLContext. This
    matches the existing D2D→WIC→BLImage path. Later, render directly into the target
    BLImage's memory; that needs `BLContext::flush(BL_CONTEXT_FLUSH_SYNC)` first, plus matching
    clip state.
* ⚠ **Metric differences:** CoreText and DirectWrite disagree on line height (ascent + descent +
  leading), default line spacing, and caret positions. Layout and text tests that compare exact
  pixel values (`test_text`, `test_textfolding`, `test_controls`) need per-platform expected values
  or tolerances.
* ⚠ **Fonts for Blend2D:** `FontManager` enumerates the Windows registry and loads files from
  `C:\Windows\Fonts`. On macOS: `CTFontManagerCopyAvailableFontFamilyNames` + `kCTFontURLAttribute`
  for file paths (many are `.ttc` collections, which need the face index; `SFNS.ttf` is a variable
  font). On iOS system font files can't be read from the sandbox. Build `BLFontData` in memory
  from `CTFontCopyAvailableTables` + `CTFontCopyTable` (create an sfnt wrapper and pass it to
  `BLFontData::create_from_data`). Use that path on both Apple platforms. It also covers fonts
  that have no file.
* Default font names are hard-coded Windows faces ("Segoe UI" etc., `SystemUIFont::Message`).
  Map them to `CTFontCreateUIFontForLanguage(kCTFontUIFontSystem)` / `.AppleSystemUIFont`
  (SF Pro), which must be resolved by descriptor, not by family name. Add a `SystemUIFont` →
  platform table, and fallback mapping for `.newui` files that name Windows fonts.
* Glyph fallback (emoji, CJK) works automatically in CoreText but not in Blend2D-drawn
  labels. Long term, route `Font::measureText` and label drawing through the same `TextSystem`
  so Blend2D-only text paths are rare.
* Caret blink (`GetCaretBlinkTime`) → ~530 ms default, or the `NSTextInsertionPointBlinkPeriodOn`
  user default. Double-click time → `[NSEvent doubleClickInterval]`.

### 4.5 Input

* **Mouse (macOS):** `mouseDown/Up/Dragged`, `rightMouse*`, `otherMouse*` → existing
  `RootView::mouse*()`. Notes:
  * Hover and enter/leave need an `NSTrackingArea` (`mouseMoved | mouseEnteredAndExited |
    activeInKeyWindow | inVisibleRect | cursorUpdate`). Without it, `mouseMoved:` is never sent.
  * Capture is implicit: `mouseDragged` keeps going to the view that got `mouseDown`, so
    `SetCapture` maps to nothing. `WM_CAPTURECHANGED` has no direct equivalent; use
    `windowDidResignKey` and the end of a drag.
  * Double-click: AppKit sends down(clickCount 1), up, down(clickCount 2), up. Windows sends
    down, up, **dblclk**, up. Map `clickCount == 2` → `mouseDblClick`; check that controls don't
    also act on the first down twice.
  * Ctrl-click = right-click convention, Force Touch (`pressureChangeWithEvent:`).
* **Scrolling:** `scrollWheel:` gives `hasPreciseScrollingDeltas` (trackpad, point deltas,
  momentum phases) vs. line deltas (mouse wheel). `mouseWheel(float delta)` was built around
  `WHEEL_DELTA` notches. Define it as "lines, fractional allowed" plus a new `mouseScroll(dx, dy,
  precise, phase)`. Horizontal scrolling and momentum are new, and needed for scroll views to feel right
  on macOS. "Natural" scrolling direction is already applied by the OS.
* **Modifiers** ⚠: add `kmCommand` (Cmd) and `kmFunction`. Shortcut handling everywhere uses
  `kmCtrl` (Ctrl+C, Ctrl+Z, ...). Add `kmPrimary` (= Ctrl on Windows, Cmd on Apple) and change
  `CommandTable`, `Action` hotkeys, text editing, and `.newui` hotkey strings to use it.
  Word navigation is Option+arrow and line start/end is Cmd+arrow on macOS, so
  `TextController` key bindings need a per-platform table. Mac users will notice right away if
  these are wrong.
* **Keyboard / text input** ⚠: the current model is WM_KEYDOWN (virtual key) + WM_CHAR (character).
  There's no IME support today.
  * `keyDown:` → `keyEvent(keKeyDown, vk)` using a hardware `keyCode` → `VirtualKeyCode` table
    (layout-independent for arrows, F-keys and so on). For characters, use
    `charactersIgnoringModifiers`.
  * Then call `[self interpretKeyEvents:@[event]]` → `NSTextInputClient`: `insertText:` →
    `keKeyPress` per code point. `doCommandBySelector:` → standard editing commands
    (`moveWordLeft:`, `deleteBackward:`, `insertNewline:` ...) → map onto `CommandId`s. That also
    gets the user's custom key bindings for free.
  * Even without real IME composition, `NSTextInputClient` must be implemented: dead keys
    (Option-e, e → é), the press-and-hold accent popup, the emoji picker (Ctrl-Cmd-Space), and
    dictation all go through `setMarkedText:`/`insertText:`. Phase 1: show marked text inline as
    plain inserted text with an underline and replace it on commit. CJK IMEs need
    `firstRectForCharacterRange:`, so add a `View::textInputClient()` query that the focused text
    control implements.
* **iOS touch** ⚠: there's no hover, no right button, and no cursor.
  * Phase 1: one-finger touch → synthesized left-button events. Long press → context menu.
  * Scrolling: touch-drag inside scrollable views needs momentum, rubber-banding and
    a deceleration curve. Either add a `UIScrollView` proxy (an invisible scroll view whose offset drives the
    newui scroll position; feels native) or write newui's own inertia. Recommend the proxy approach
    for the root/list views.
  * Pinch/zoom (sketch demo) → `UIPinchGestureRecognizer` → new `onMagnify` event (also
    `magnifyWithEvent:` on macOS trackpads).
  * Hit targets: controls sized for a mouse are too small for fingers (44 pt guideline). This is a
    style and layout problem, not a port problem, but it determines whether iOS is usable.
  * iPad pointer/trackpad: `UIHoverGestureRecognizer` (hover), `UIPointerInteraction`, and
    `buttonMask` on touches give near-desktop behavior.
* **Software keyboard (iOS):** `NUIRootUIView` adopts `UITextInput`. `becomeFirstResponder`
  when a text control gains focus shows the keyboard. `TextInputTraits` (already modeled on
  `UITextInputTraits`) maps directly. Keyboard-frame notifications → safe-area or bottom inset so
  the focused field scrolls into view.
* **Pen:** `PenState` ← macOS `NSEvent` tablet events (`pressure`, `tilt`, `isEnteringProximity`,
  eraser via `pointingDeviceType`). On iPad, Apple Pencil `UITouch` `force`, `altitudeAngle`,
  `azimuthAngle`, plus hover (iPadOS 16.1+ `UIHoverGestureRecognizer.zOffset`), squeeze/double-tap
  via `UIPencilInteraction`.

### 4.6 Styles, theme, colors

* `ThemedViewStyle` and its subclasses draw native Windows controls with uxtheme into an HDC.
  There's no clean AppKit equivalent (`NSCell` drawing is legacy and needs a view and context;
  there's nothing on iOS). Plan: no native control rendering. On Apple, the default factory
  hands out the Blend2D-drawn styles (Fluent/Flat), plus a new Blend2D "Mac" style set
  (rounded bezel buttons, accent-colored checkboxes) later. `.newui` files that name
  `Themed*` styles need an alias map (`ThemedButtonStyle` → `ButtonStyle` on Apple).
* `UIColorManager`: dark mode → `NSApp.effectiveAppearance` (KVO on `effectiveAppearance`) /
  `traitCollectionDidChange` on iOS. Accent → `NSColor.controlAccentColor`/`tintColor`. System
  colors → `NSColor` semantic colors, resolved under the current appearance
  (`[appearance performAsCurrentDrawingAppearance:]`) and converted to sRGB. ⚠ The semantic colors are
  dynamic: they must be re-resolved on an appearance change, not cached once. The existing
  `refreshThemes()` → `markDirty()` path is the right hook. Map these into `onThemeChanged` /
  `onColorizationColorChanged`.
* Color space: tag the IOSurface or layer as sRGB (`layer.colorspace` / IOSurface color space),
  or colors look oversaturated on P3 displays.

### 4.7 Cursors (macOS) / none (iOS)

* `Cursor` → `NSCursor`: arrow, IBeam, pointingHand, crosshair, resizeLeftRight/UpDown, openHand/closedHand,
  operationNotAllowed. Windows has diagonal resize, "size all", "wait"/"app starting" with no
  public NSCursor before macOS 15 (`frameResizeCursorFromPosition:`). Embed images for those.
  Custom cursors from file/image → `NSCursor initWithImage:hotSpot:` (account for Retina: a 2×
  representation).
* ⚠ Model mismatch: Windows asks for the cursor on each move (WM_SETCURSOR →
  `cursorTargetAt()`). AppKit resets the cursor in `cursorUpdate:` and on window activation, so
  call `[cursor set]` from `mouseMoved:`/`cursorUpdate:` with `cursorTargetAt()`, and
  invalidate cursor rects on capture changes.
* iOS: no-op. On iPad with a pointer, optionally map IBeam/pointing hand to
  `UIPointerStyle`.

### 4.8 Dialogs and modality ⚠

* `Dialog::showMessageBox` → `NSAlert runModal` (or `beginSheetModalForWindow:` for a
  window-modal sheet, the Mac-native choice). `showOpenFile/Save/BrowseForFolder` →
  `NSOpenPanel`/`NSSavePanel`, with `FileDialogFilter` extensions → `UTType typeWithFilenameExtension:` →
  `allowedContentTypes`. Sandboxed apps get security-scoped URLs; the path is only
  usable while access is held, so document `startAccessingSecurityScopedResource`.
* Custom `Dialog : Frame` → `NSPanel` + `runModalForWindow:`, or a sheet.
* ⚠ **iOS has no blocking modals.** `UIAlertController`, `UIDocumentPickerViewController` and
  presented view controllers are async only, so a call that returns `DialogResult` can't
  work. Add an async API on all platforms now:
  `showModalAsync(std::function<void(DialogResult)>)`, `showOpenFileAsync(..., callback)`. Windows
  implements it on top of the blocking version. Mark the blocking APIs desktop-only and move
  examples and demos to the async ones. This affects how app code is structured: "Save changes?"
  on close (`onClosing` veto) becomes "veto now, then close programmatically from the callback".
* iOS file access: the document picker returns copies or security-scoped URLs, and there's no
  real filesystem browsing. `FileWatcher` on picked files won't work.

### 4.9 Popups (PopupFrame, PopupTool, CalloutTool, dropdowns, tooltips) ⚠

* Windows uses `WS_POPUP`/layered windows (`UpdateLayeredWindow`, per-pixel alpha), owner
  windows, and capture juggling.
* **macOS:** borderless `NSPanel` (`NSWindowStyleMaskBorderless | NonactivatingPanel`,
  `opaque = NO`, `backgroundColor = clearColor`, `hasShadow`, level `NSPopUpMenuWindowLevel`),
  added as a child window of the owner (`addChildWindow:ordered:`) so it moves with it. A layer
  backed by a PRGB32 buffer handles per-pixel alpha, so `PopupTool`'s format override works as-is.
  * Non-activating matters: dropdowns and tooltips must not take key status from the main
    window. Text-entry popups need `becomesKeyOnlyIfNeeded` / `canBecomeKeyWindow` overrides.
  * Dismiss on outside click: install `addLocalMonitorForEventsMatchingMask:` (mouse down) while
    open, and close on `NSWindowDidResignKeyNotification` / app deactivate.
* **iOS:** there are no extra windows. Popups become an overlay `UIView` (its own RootView peer)
  added above the root view in the same `UIWindow`, or a `UIPopoverPresentationController` on
  iPad. So the platform layer needs a "PopupHost" abstraction (create a popup surface relative
  to an anchor rect in a root view) instead of "make a top-level window". Plan it in Phase 0 so
  `PopupFrame` doesn't bake in windows.

### 4.10 Menus

* `ContextMenu` (native `HMENU` + `TrackPopupMenuEx`) → `NSMenu` +
  `popUpMenuPositioningItem:atLocation:inView:` (also blocking/modal, so the semantics match). Owner-draw
  items (`WM_MEASUREITEM/DRAWITEM`) → `NSMenuItem.image`, or `NSMenuItem.view` for custom content. Keep it rare.
* `MenuBar` (a newui-drawn in-window menu row): macOS apps should use the global menu bar.
  Add a `MenuBar` peer that mirrors its `MenuItem` tree into `NSApp.mainMenu` and hides the
  in-window row on macOS. Also generate the standard App menu (About, Settings… ⌘,, Services,
  Hide ⌘H, Quit ⌘Q) and Window menu. Without these, the app looks broken on macOS. Key equivalents
  come from the `Action` hotkeys (see `kmPrimary`).
* iOS: `ContextMenu` → `UIContextMenuInteraction`/`UIMenu` (async). `MenuBar` → `UIMenuBuilder` on iPad
  (keyboard shortcuts menu, iPadOS 26 menu bar), hidden on iPhone.

### 4.11 Clipboard

* `ClipboardMgr` → `NSPasteboard.generalPasteboard` / `UIPasteboard.generalPasteboard`. The MIME mapping layer
  maps to UTIs: `text/plain` → `public.utf8-plain-text`, `text/html` → `public.html` (no CF_HTML
  wrapper needed), `image/png` → `public.png`, `image/bmp`/CF_DIB → PNG/TIFF, file list →
  `public.file-url` items, Cut vs Copy flag → no standard (a custom private type).
* Delayed rendering (`WM_RENDERFORMAT`/`WM_RENDERALLFORMATS`) → `NSPasteboardItemDataProvider`
  / `UIPasteboard setItemProviders:` (`NSItemProvider`).
* `ClipboardScope`'s open/retry loop isn't needed; make it a no-op RAII.
* iOS 16+ shows a paste-permission alert on programmatic reads. Prefer reading only in response to
  a user Paste command, or use `UIPasteControl`.

### 4.12 Drag and drop ⚠

* Today: OLE `IDropTarget`/`IDropSource`/`IDataObject` via WRL. `DoDragDrop` is a **blocking modal loop**
  that returns the final `DropEffect`, and `repaintNow()` exists partly to work around it.
* macOS: `NUIRootNSView` registers `registerForDraggedTypes:` and implements
  `draggingEntered/Updated/Exited/performDragOperation:` → the existing `DropTarget` routing.
  Outgoing: `beginDraggingSessionWithItems:event:source:` is **asynchronous**; the result
  arrives in `draggingSession:endedAtPoint:operation:`. Any `DropSource` code that assumes it gets the
  effect back synchronously (e.g. deleting the source on Move) has to move to a completion callback.
  Change the `DropSource` API in Phase 0 to `onDragEnded(DropEffect)`.
* `VirtualFileDataObject` (`CFSTR_FILECONTENTS`) → `NSFilePromiseProvider`.
* iOS: `UIDragInteraction`/`UIDropInteraction` with `NSItemProvider` (iPhone too since iOS 15).
  Drags start from a long press.
* Move `COMDropTarget`, `TextDataObject`, `enum_format_etc` entirely into `src/win32/`, and out of the
  public `dragndrop.h`/`rootview.h`.

### 4.13 System services

| Area | Windows | macOS | iOS |
|---|---|---|---|
| `Bundle` resources | `<exe dir>/Resources`, `Info.json` | `NSBundle.mainBundle.resourcePath` (`Contents/Resources`); name/version from `Info.plist` (`CFBundleName`, `CFBundleShortVersionString`), keep `Info.json` as fallback | bundle root |
| Settings (User) | `%APPDATA%\company\app` | `~/Library/Application Support/<bundle id or company/app>` | app container `Library/Application Support` |
| Settings (Machine) | `%PROGRAMDATA%` | `/Users/Shared/<company>/<app>` (shared, writable; sandboxed apps fall back to user scope) | no machine scope: map to user |
| `FileWatcher` | `ReadDirectoryChangesW` | FSEvents (`kFSEventStreamCreateFlagFileEvents`, latency ~0.1 s; events are coalesced, so tests that expect exact event sequences will need loosening) | `dispatch_source` `DISPATCH_SOURCE_TYPE_VNODE` per file/dir (no recursive watch) |
| Temp/known folders | `GetTempPathW`, `SHGetKnownFolderPath` | `NSTemporaryDirectory`, `NSFileManager URLsForDirectory:` | same |
| Paths | `\` separators, `C:` checks in `fontmanager.cpp` `isAbsolutePath` | use `std::filesystem` everywhere | same |
| Double-click, drag threshold | `GetDoubleClickTime`, `SM_CXDRAG` | `NSEvent.doubleClickInterval`, ~3 pt | 0.3 s, ~10 pt |
| Session end | `WM_QUERYENDSESSION` | `applicationShouldTerminate:` | n/a (background → flush) |

### 4.14 GUI automation (`newui_automation`)

* Named pipe → Unix domain socket in `$TMPDIR` (per-user, mode 0600). Same JSON-lines protocol.
* `SendInput` → posting real `CGEvent`s needs the Accessibility (TCC) permission prompt, which breaks
  unattended CI. Prefer in-process injection: synthesize `NSEvent`s and send them through
  `[NSApp sendEvent:]`, or call the `RootView` entry points directly. Use XCUITest for real
  end-to-end tests on iOS.
* Popup menu inspection (`MN_GETHMENU` hack) → walk the `NSMenu` model directly.

### 4.15 Unit tests

* gtest works on macOS as a command-line target. Split `newui_tests` into common tests and
  `if(WIN32)` tests (`test_gdipresentsurface`, `test_dxgipresentsurface`, `test_dragndrop`,
  `test_clipboard`'s Win32 parts, theme-style tests).
* Tests that need a window need an `NSApplication`: a gtest `Environment` that calls
  `[NSApplication sharedApplication]` and runs tests on the main thread.
* iOS: run gtest inside a host app on the simulator (an XCTest bundle whose single test runs
  `RUN_ALL_TESTS()`), via `xcodebuild test -destination 'platform=iOS Simulator,...'`. Do this
  after macOS is green.
* Pixel and metric tests: per-platform expected values for text-heavy cases (see 4.4).

## 5. Phased roadmap

Phase 0 is broken down step by step in [phase0-portability-plan.md](phase0-portability-plan.md).

| Phase | Deliverable | Exit criteria |
|---|---|---|
| **0. Split (on Windows)** | 2.1–2.4: platform folders, opaque handles, peers, `u16string`, clang-cl clean, async Dialog/DropSource APIs, PopupHost abstraction | Windows build and all tests pass unchanged; `include/newui/*.h` has no `windows.h` |
| **1. macOS core** | CMake/Xcode targets, `NSWindow`/`NUIRootNSView`, CGImage present, CF RunLoop, mouse/keys (no IME), Blend2D-only text (labels), Fluent styles | `example1`, `layout1`, `animation1` run; resize is live and smooth; Retina correct; non-UI tests green |
| **2. macOS text** | CoreText `TextLayoutEngine`/`TextRenderer`, CoreText-backed `FontManager`, `NSTextInputClient` (dead keys, emoji), Cmd/Option shortcut tables | `controls1` TextField/TextControl fully editable; text tests green with platform baselines |
| **3. macOS platform services** | IOSurface double-buffered present, cursors, NSMenu context menus + global menu bar, NSAlert/NSOpenPanel, clipboard, DnD, popups as NSPanel, settings/bundle/filewatcher, dark mode | sketch and manyrows demos usable; app bundle signed with hardened runtime + JIT entitlement |
| **4. iOS bring-up** | UIKit lifecycle + scene, `NUIRootUIView`, present, no-JIT Blend2D, touch→mouse, safe area, UITextInput + software keyboard, async dialogs, overlay popups | `controls1` runs on iPhone simulator and a device; repaint benchmark measured on device |
| **5. iOS polish** | UIScrollView proxy scrolling, gestures (pinch), Pencil, UIMenu/context menus, document picker, iPad pointer/hover, multi-scene | sketch demo usable on iPad with Pencil |
| **6. Optional** | Metal present backend, IME composition (marked text UI, CJK), accessibility (NSAccessibility/UIAccessibility tree from Views), full-size-content title bar | as needed |

## 6. Top risks, summarized

1. **`wchar_t` size / text offsets** (2.3): silent wrong caret and selection positions. Fix on Windows first.
2. **MSVC-only code and the reflection layer** (2.4): `typeid().name()` format, `size_t` vs
   `uint64_t`, permissive templates. May also affect saved `.newui` file portability.
3. **Text stack rewrite** (4.4): DirectWrite → CoreText for layout, hit-testing and rendering,
   plus Blend2D font loading without font files on iOS. The largest single piece of work.
4. **Run-loop ownership and modes** (4.3): AppKit/UIKit own the loop. Wrong modes mean frozen repaints during
   resize; iOS `UIApplicationMain` never returns; UI is main-thread only.
5. **Blocking APIs** (4.8, 4.12): modal dialogs and DoDragDrop have no blocking equivalent on iOS, and drag on macOS is
   async. API changes affect app code, so make them early.
6. **Platform conventions** (4.5, 4.10): Cmd vs Ctrl, global menu bar, Option-arrow
   navigation. Without these the port works but doesn't feel like a Mac app.
7. **Blend2D JIT** (3): entitlement on macOS, unavailable on iOS (performance).
8. **Build-time tools** (3): reflectgen needs the right sysroot, and the sizeof probe can't
   run when cross-compiling for iOS.
9. **Popups as windows** (4.9): non-activating panels on macOS, no extra windows on iOS.
10. **Touch** (4.5): the input model is mouse-first; scrolling and hit-target sizes decide whether
    iOS is usable.

## 7. Decisions

Decided (2026-11-20):

* Minimum OS versions: **macOS 12, iOS 16**.
* Coordinate unit: **device pixels** (same as Windows; `dpiScale` = backing scale factor).
* Closing the last Frame **quits the app** on macOS.
* macOS menu bar: **global menu bar only**. `MenuBar` mirrors into `NSApp.mainMenu` and the in-window row is hidden; no in-window option.

* Settings storage: **JSON5 files in Application Support** (same format and API as Windows).
* Machine-scope settings on macOS: **`/Users/Shared/<company>/<app>/settings.json`**, the closest match to `%PROGRAMDATA%` (shared by all users, writable). iOS and sandboxed macOS apps fall back to the user file.
* reflectgen sizeof probe: **skipped when targeting iOS**. It only feeds an optional memory-footprint estimate in build output and doesn't affect generated code. Revisit with a host-built probe if the estimate is missed.

Nothing is open now.

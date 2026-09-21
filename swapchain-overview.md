# Presentation backends and the repaint pipeline

*What the `swapchain` branch added to newui, the techniques it uses and why, and what to watch for
when building on it.*

Status (2026-09-20): merged into `cpptools` and pushed. Design history and the original spike results
are in `swapchain-plan.md`; this document is the finished picture.

---

## 1. Summary

Every newui window (`RootView`) draws its whole UI on the **CPU**, with Blend2D, into an in-memory
image. Getting those pixels onto the screen used to be hard-wired to GDI. This work:

1. **Put "get the pixels onto the screen" behind an interface** (`PresentSurface`), so it can be
   swapped per window without touching anything that draws.
2. **Added a DXGI flip-model swap-chain backend** on Direct3D 11 (`DxgiPresentSurface`) that quietly
   falls back to GDI when it can't be set up on a machine (see section 3 for one important caveat
   about *late* fallbacks).
3. **Reworked the repaint pipeline** so it is *correct* (every repaint starts from a blank buffer),
   *cheap* (off-screen children are skipped) and, by default, *incremental* (only the changed region
   is redrawn: "dirty" mode).
4. **Made it work for a `RootView` that lives on its own thread inside another application's
   window**, which is how the Visual Studio editor extension hosts newui.
5. **Made the choices settable from code**, not only from environment variables.

Defaults after this work: GDI presentation (DXGI is opt-in), dirty-rect repaint **on**, verification
off.

---

## 2. Why

| Motivation | What it led to |
|---|---|
| The window "doesn't quite refresh right": text getting bolder after repeated repaints, stale pixels | A real repaint bug found and fixed (section 4.1), and invalidation made explicit and checkable |
| A path to hardware-accelerated presentation | The `PresentSurface` abstraction and the DXGI backend |
| Hosting a newui editor inside Visual Studio, on a dedicated thread, under a VS-owned window | Cross-thread rules, Frame-less `RootView` support, a `DropDownList` fix (section 6) |
| Simple UIs should stay simple | GDI stays the default and DXGI degrades to GDI silently instead of failing |

---

## 3. The presentation backends

### The big idea: separate *drawing* from *presenting*

```
  View tree ──paint──▶  Blend2D (CPU)  ──▶  image()  ── one CPU buffer per window
                                               │
                                        PresentSurface
                                     ┌─────────┴──────────┐
                              GdiPresentSurface     DxgiPresentSurface
                              DIB + BitBlt          upload ─▶ texture ─▶ swap chain
```

Blend2D always renders into a normal CPU image, whichever backend is in use. So DXGI here is a
*presentation* technology, not GPU rendering: the GPU's job is to get finished pixels to the window
through the compositor. The consequence is that everything that draws (controls, styles, the repaint
logic) is identical for both backends, and a backend can be swapped or fall back without any drawing
code knowing.

### GDI

The original path, moved behind the interface unchanged: a DIB section in a memory DC, `InvalidateRect`
on present, `BitBlt` on `WM_PAINT`.

### DXGI: the techniques and why

**Persistent CPU buffer, upload only what changed.** The first spike rendered directly into a mapped
GPU texture. That doesn't survive partial repaints: a discard-mapped texture (or a `DISCARD` swap
chain back buffer) loses every region that wasn't just redrawn. So the surface keeps one persistent
CPU image, uploads only the **dirty box** into a persistent default-usage texture, and copies that
whole texture into the back buffer each frame. `UpdateSubresource` is Direct3D's documented way to copy
CPU memory into a default-usage (GPU-only, non-mappable) resource, and it accepts a destination box, so
only the changed texels are sent. A side benefit: the CPU cost of a repaint is the same for both
backends.

**Flip-model swap chain (`FLIP_DISCARD`, 2 buffers).** In Microsoft's terms there are two ways to
present: the older *bitblt model* copies the back buffer into a surface owned by the Desktop Window
Manager (DWM) on every present, while the *flip model* shares the back buffers with the DWM so it can
compose straight from them with no extra copy. Flip is the recommended model for new code. It comes
in two flavours, and the choice matters here:

- `FLIP_SEQUENTIAL` guarantees each back buffer's contents survive a present, and is the one to use
  when an app relies on *partial presentation* (dirty and scroll rectangles) or reads earlier frames.
- `FLIP_DISCARD` gives **no such guarantee** (the compositor may even draw other content onto the
  app's back buffer in some optimised cases), and Microsoft recommends it *when the app fully renders
  over the back buffer before each present*. That is exactly what we do: every frame copies the
  complete texture into the back buffer. It requires Windows 10 for Direct3D 11; on an older system
  swap-chain creation fails and the surface falls back to GDI.

**No vsync wait.** `Present1` is called with sync interval 0. For flip model, Microsoft documents this
as cancelling the remaining time on the previously presented frame and dropping this frame if a newer
one is queued; that is, it does not wait for a vertical blank. A repaint therefore never blocks on the
display. (The documentation states this for `FLIP_SEQUENTIAL`; it doesn't describe `FLIP_DISCARD`
separately, and we've assumed the same.)

**DXGI's own window handling is switched off.** After creating the swap chain we call
`MakeWindowAssociation` with `DXGI_MWA_NO_WINDOW_CHANGES`, which stops DXGI monitoring the window's
message queue. That means it can't respond to Alt+Enter full-screen toggling; `RootView` owns its
window's behaviour.

**Lazy creation.** The D3D device and swap chain are created on the first present, once there is both
a buffer and a window handle. This keeps construction cheap and means a window that never presents
never touches the GPU.

**One device per window, not shared.** Each surface creates its own single-threaded device. That is
simple and safe when each hosted `RootView` lives on its own thread, at the price of more GPU objects
per window.

**Automatic fallback to GDI.** If anything goes wrong, the surface replaces itself with a GDI surface
(carrying the pixels across) and logs why. Triggers: no hardware device, an unreachable DXGI factory,
only a **software adapter**, swap-chain or texture creation failing, present failing, or a device
loss that repeats (one loss gets a rebuild and a full re-upload). There is deliberately no separate
"auto" mode: `Dxgi` already means "DXGI if possible".

**Software adapters are refused on purpose.** WARP / the Microsoft Basic Render Driver is a
CPU-emulated GPU, which is slower than plain GDI here. Two forms exist, and the check covers both:
the render-only "Basic Render Driver" adapter (vendor ID `0x1414`, device ID `0x8c`) carries
`DXGI_ADAPTER_FLAG_SOFTWARE`, but when a machine's display driver isn't working, its *primary* adapter
can also be called Microsoft Basic Render/Display Driver, with outputs and **without** that flag.
The surface therefore tests the software flag *and* the Microsoft vendor ID. Expect the fallback on a
VM, some remote-desktop setups, or with a broken graphics driver.

**Dirty rectangles are not passed to `Present1`, by design.** Microsoft documents that
`DXGI_SWAP_EFFECT_FLIP_DISCARD` "cannot be used with multisampling and partial presentation". That is
consistent with the `DXGI_ERROR_INVALID_CALL` that passing dirty rectangles returned during
development (we never isolated the exact cause, but this is the documented limitation). The upload
is still dirty-box-only, which is where the saving is. If partial presentation is ever wanted (it can
reduce traffic for remote-desktop scenarios), it would need `FLIP_SEQUENTIAL` plus tracking of dirty
rectangles across every buffer in the chain, which is a redesign of the present step, not a flag.

**Creation-time fallback is safe; a *late* fallback is not.** Microsoft documents that after the
first successful `Present1` on a flip-model swap chain, **GDI no longer works with that window, "even
after the destruction of the swap chain"**. So falling back to GDI because the swap chain couldn't be
*created* works, but falling back after frames have already been presented (a later `Present` failure,
or a device that keeps getting lost) is likely to leave that window unable to show GDI output. The
current fallback keeps the same window, so this case is expected to be broken (see section 9).

---

## 4. The repaint pipeline

### 4.1 The bug that started it: text thickening

`RootView::repaint()` filled the background only inside the dirty rectangle but redrew **every child
over the whole window**. Anything outside the dirty rectangle was therefore composited onto its own
previous pixels, so anti-aliased text darkened and thickened each time *something else* repainted.

**Fix:** every repaint starts from a blank buffer and draws everything that shows there. The dirty
rectangle now decides only what is *presented*. The important property this creates is that
**painting is idempotent**: nothing can depend on what was in the buffer before.

### 4.2 Three levels of work, cheapest first

1. **Visibility culling (always on).** A child whose whole drawn extent lies outside what is visible
   (outside the window or an ancestor's clipped bounds, e.g. scrolled away) is skipped. The extent
   includes focus-ring and drop-shadow padding, because those paint outside a view's bounds.
2. **Dirty-rect pruning (default).** Clip everything to the changed region, blank only that region,
   draw only children overlapping it, and do nothing if nothing is dirty. A small hover repaint costs
   about 0.012 ms regardless of tree size (Release), versus about 0.9 ms for a 320-view tree in `Full`
   mode: roughly 70x.
3. **`Full` mode.** Blank and redraw everything. Always right, cost grows with the view count. Kept
   as the escape hatch and for special cases (below).

### 4.3 The contract dirty mode imposes

Dirty mode is only as correct as **every control's invalidation**: a control that changes how it looks
without marking itself (and any overhanging padding) dirty leaves stale pixels on screen. `Full` mode
used to hide such omissions because the next repaint anywhere fixed them.

### 4.4 Verification: catching missing invalidations

With verification on, each pruned repaint is followed by a full render into a scratch buffer and a
comparison. A difference prints a report naming the rectangle: *something changed there without
invalidating it*. Two design decisions matter:

- **A tolerance of 2 levels per channel, not zero.** Blend2D clips vector edges at a clip box with
  fixed-point rounding, so pixels along a clip edge can legitimately differ by 1. The difference is
  invisible and doesn't accumulate (each repaint starts from blank).
- **It is a testing tool, off by default.** It doubles the render work.

This flushed out a real masked bug: `SubView::setBounds()` never invalidated anything.

### 4.5 Invalidation fixes found along the way

`RootView::invalidate()` (public, but with no callers in the codebase) had three real bugs: it cleared
a pending dirty rectangle so a queued repaint presented nothing, it did nothing under DXGI, and it
truncated fractional rectangles, losing up to a pixel. `GdiPresentSurface` also rejected a `WM_PAINT`
rectangle that overhung the buffer. All fixed, each with a test that fails on the old code.

---

## 5. Configuration

Three settings, each settable **in code** (which always wins) or by **environment variable**:

| Setting | In code (`newui::`) | Environment variable | Default |
|---|---|---|---|
| Present backend | `setDefaultPresentBackend(Gdi \| Dxgi)` | `NEWUI_PRESENT=gdi\|dxgi` | Gdi |
| Repaint mode | `setDefaultRepaintMode(Full \| Dirty)` | `NEWUI_REPAINT=full\|dirty` | Dirty |
| Verify pruned repaints | `setDefaultVerifyRepaint(bool)` | `NEWUI_VERIFY_REPAINT=1` | off |

- **Call the setters once at startup, before the first `RootView` exists.** Each `RootView` reads the
  defaults at construction; environment variables are read once, on first use.
- **Environment variables proved a poor fit for hosted scenarios** (an extension launched from a
  debugger, where it is hard to tell whether they arrived), which is why the in-code path exists.
  The Visual Studio editor sets all three in `NativeEditManager`'s constructor.
- **`PopupTool` is pinned to GDI + `Full`.** It uses a layered window with per-pixel alpha and is
  meant for simple UIs. The code records the reason as "a layered window can't present through a swap
  chain"; that is our own earlier conclusion, and we did not find Microsoft documentation confirming
  or refuting it (their layered-window documentation says nothing about swap chains). Treat it as a
  cautious choice, not a documented rule.
- A `RootView` with `onRedrawNeeded` listeners always repaints in full (a listener draws straight into
  the buffer, unclipped, so it can't be pruned).

---

## 6. Hosting a `RootView` inside another application

The motivating case: a Visual Studio extension whose editor is a newui `RootView` on a **dedicated
worker thread**, as a child window of a window owned by VS's UI thread. The rules that fell out of
that:

- **The parent thread only ever `PostMessage`s to the child, and never blocks waiting for it.**
  `SendMessage` from parent to child can deadlock, and creating or destroying a cross-thread child
  sends messages *back* to the parent, so a parent blocked on the child hangs. Shutdown is a posted
  handshake.
- **Cross-thread parent and child share input state.** Microsoft's own developer blog explains that a
  cross-thread parent/child (or owner/owned) relationship *implicitly attaches the two threads' input
  queues*, transitively, which makes formerly asynchronous things (focus changes, for instance)
  synchronous, and calls the arrangement legal but "very difficult to manage correctly". In our
  tests, if the parent's thread stops pumping, mouse input to the child stalls too. Same for both
  backends.
- **A surface lives entirely on one thread:** created, sized, presented to and destroyed on the worker,
  which is also the thread that created the child window. That is consistent with Microsoft's DXGI
  multithreading guidance, which warns that DXGI calls made from a thread other than the window's
  creator can deadlock (their example is a full-screen mode change whose internal `SendMessage` waits
  on a blocked message-pump thread; we run windowed, with DXGI's window handling switched off).
- **A standalone `RootView` has no `Frame`** (and no `Application`); `getFrame()` is null. Anything
  that assumed a `Frame` breaks *silently* there. Found the hard way: `DropDownList` took its popup's
  owner from the `Frame` and quietly returned when there was none, so the arrow click did nothing in
  the editor. It now falls back to the top-level window above the `RootView`. The `crossthread1`
  example hosts a `DropDownList` as a permanent check of exactly this shape.

---

## 7. Technologies used

| Technology | Role |
|---|---|
| **Blend2D** | CPU 2D rasterizer that draws the whole UI (vendored under `3rdparty/`) |
| **Direct3D 11** | The device, the upload texture and the copy into the back buffer |
| **DXGI 1.2** | The flip-model swap chain for a window, `Present1`, adapter queries (to detect software adapters) |
| **GDI** | Default backend and fallback: DIB sections, memory DCs, `BitBlt`, `WM_PAINT` |
| **Win32 windows and threads** | Cross-thread child windows, `PostMessage`, per-thread message loops, layered popups |
| **DWM composition** | The Desktop Window Manager composes the flip-model back buffers directly. Tests read composed pixels back with `PrintWindow(..., PW_RENDERFULLCONTENT)`: that flag is defined in `winuser.h` but is **not described** on Microsoft's `PrintWindow` page (which lists only `PW_CLIENTONLY`), so we rely on it because it was observed to work, not because it is documented |
| **COM / WRL `ComPtr`** | Lifetime of the D3D and DXGI objects |
| **C++17, MSVC, CMake, GoogleTest** | Implementation, build and tests (`newui` links `d3d11`, `dxgi`, `dxguid`) |
| **reflectgen (libclang)** | Not presentation, but it parses the STL headers at build time; a newer MSVC STL needed an extra define for an older libclang |

---

## 8. Verification summary

- **Automated:** the suite passes in default mode and with dirty + verification forced on for every
  window in every test (only the deliberate missed-invalidation test reports). The DXGI tests skip on
  a machine with no usable hardware adapter.
- **Live:** 20 GUI examples run on both backends with no fallback or crash and mostly pixel-identical
  output; scripted input and randomized 60-second input storms with verification produced no reports;
  an integrated-graphics-only laptop presents via DXGI; and in the real Visual Studio host the editor
  ran on a live `DxgiPresentSurface` with no fallback, with resizing, the designer, the properties
  grid and the drop-down all working.
- **Examples:** `presentbackend1 [gdi|dxgi]` reports the backend and repaint mode it got;
  `crossthread1 [gdi|dxgi] [--stall]` is the hosted-window shape.
- **Benchmark:** an opt-in `RootViewRepaintBenchmark` (Release only; repeat runs before trusting it).

---

## 9. Pitfalls and guidance for future development

**Writing controls under dirty mode**

- **Invalidate on every visual change, including overhang.** Call `markDirty()`/`redraw()` when a
  control's appearance changes, and remember effects that paint outside the control's bounds (focus
  rings, shadows) need their padding invalidated too. Run new controls with verification on; a
  report names the region.
- **Never rely on previous frame contents.** The buffer is blank at the start of every repaint, so
  painting must be a pure function of current state.
- **Drawing straight into the image buffer outside a repaint is wiped by the next repaint.** Draw in a
  paint handler, or use an `onRedrawNeeded` listener (which forces that window to `Full`).

**Clipping**

- **Blend2D can assert on a combined clip that isn't a plain rectangle** (seen with themed children),
  and an earlier attempt at per-child clipping corrupted the display. The current design uses one
  root-level dirty clip, and relies on `restore_clipping()` returning to the *last saved* clip (so the
  "unclipped" focus-ring and shadow phases are still bounded by it). Changing the clipping scheme is
  risky; re-run the pruning tests and the clip-edge sweep.

**The DXGI backend**

- **Every present must be a complete frame.** With `FLIP_DISCARD` the back buffer's previous contents
  aren't guaranteed to survive, so the surface copies the whole texture each time. Don't "optimise"
  that into a partial copy without switching to `FLIP_SEQUENTIAL` (section 3).
- **A late fallback to GDI is a known weak point.** Microsoft documents that GDI stops working on a
  window after the first successful `Present1` of a flip-model swap chain, even once the swap chain is
  destroyed. Only fallbacks that happen *before* the first successful present (device, adapter,
  swap-chain or texture creation failing) are reliable. A failure *after* frames have been shown, or
  switching backend on a window that has already presented, would likely leave that window unable to
  show anything, and the current code keeps the same window. The tests cover creation-time fallbacks
  only. Likely fix if it matters: recreate the child window when falling back late. Until then, treat
  `setDefaultPresentBackend()` as a startup-only setting.
- **One flip swap chain per window, and nothing else may draw on it.** Microsoft's guidance: use one
  flip-model swap chain per `HWND`, and don't target that `HWND` with GDI or other Direct3D presentation:
  "only Direct3D content in flip model swap chains ... are visible" and other updates are ignored. So no
  direct GDI drawing into a `RootView`'s window while DXGI is active, and don't use `ScrollWindow` or
  `ScrollWindowEx` on it (Microsoft says they need the bitblt model).
- **Destroying and recreating a swap chain has a documented trap.** Direct3D 11 defers object
  destruction, so freeing a flip swap chain's references doesn't destroy it immediately, and creating a
  new one on the same `HWND` can then fail. Microsoft's remedy is to release everything, call
  `ClearState()`, then `Flush()`, before creating the new swap chain. Our rebuild paths (a resize that
  can't resize in place, a device-loss rebuild) release the whole device and create a new one and
  don't call `ClearState`/`Flush`. That is probably fine because the old device is fully released, but it
  is unverified; adding the two calls is cheap insurance.
- **Layered windows:** `PopupTool` is pinned to GDI (section 5) on our own earlier conclusion, not on
  documentation. Don't loosen that without testing.
- **Untested:** recovery *after* a swap chain has been live and the device is really lost (only
  "can't create a swap chain" is tested); behaviour when a window moves between monitors or DPI
  changes with a live swap chain.
- **Fallback is silent to the user.** The reason goes only to `OutputDebugString`. Check
  `RootView::presentBackend()` when you need to know what a window really got.
- **One D3D device per window** adds up with many windows.

**Hosting**

- **Frame-less hosts break code that assumes a `Frame` or `Application`, silently.** If something works
  in an ordinary example but does nothing in the host, search that code path for `getFrame()` and
  `Application::instance()`. Other controls may have the same gap `DropDownList` had.
- **The host's own window calls can block on the editor thread.** Its frame calls `SetWindowPos` /
  `MoveWindow` directly on the editor's window from another thread. Microsoft documents that
  `MoveWindow` sends `WM_WINDOWPOSCHANGING`, `WM_WINDOWPOSCHANGED`, `WM_MOVE`, `WM_SIZE` and
  `WM_NCCALCSIZE` to the window, and that the `SWP_ASYNCWINDOWPOS` flag exists to stop "the calling
  thread from blocking its execution while other threads process the request", so a cross-thread call
  waits on the window's thread. It is true with either backend and only matters if the editor thread is
  busy for long stretches. Note `SWP_ASYNCWINDOWPOS` applies when the two threads are "attached to
  different input queues", and cross-thread parent and child are attached (section 6), so we expect the
  flag would not help here (an inference, untested), and in any case the host, not us, makes those calls.
  A proxy window on the host's own thread that forwards resizes by `PostMessage` would remove it;
  not built. Note that you can't reproduce `SetWindowPos` by posting `WM_SIZE`: the position lives in
  the window manager, so the equivalent is a custom message that the window's own thread acts on.

**Diagnostics and testing**

- **Output that goes only to `OutputDebugString` is easy to miss** inside a VSIX. Microsoft's
  documentation says the string goes to the attached debugger (and, with none attached, to the system
  debugger if one is active), and notes that Visual Studio's handling of these strings has changed
  between versions. In a managed project such as a VSIX, viewing native `OutputDebugString` output
  requires ticking **"Enable native code debugging"** in the project's Debug settings. In practice
  DebugView only sees the output when no debugger is attached. Verification reports and fallback
  reasons are affected. Enable native code debugging, use DebugView with no debugger attached, or add
  an explicit log.
- **Presentation tests must read pixels back.** A version once shipped a solid black window while every
  test passed, because the tests only asserted "nothing failed". Read the composed pixels, and re-read
  a file after any scripted multi-line edit.
- **Global state:** the `setDefault...` values are process-wide and have no reset, so a test that sets
  one leaks it into later tests. Save and restore around it, as the DXGI tests do.
- **Benchmarks:** the first run reads high; repeat it, and use Release.

**Open decisions**

- Making DXGI newui's own default (a one-line change; it now works on a development machine, an
  integrated-graphics laptop and in the VS host).
- The proxy window above.
- Whether these branches go into `main`.

---

## 10. References

Microsoft documentation the terms and behaviour above were checked against (September 2026). Where a
claim rests on our own testing instead, the text says so.

- [DXGI flip model](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-flip-model): flip vs bitblt model, one flip swap chain per window, don't mix GDI/other APIs on the window, `ScrollWindow`.
- [DXGI_SWAP_EFFECT](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_swap_effect): `FLIP_SEQUENTIAL` vs `FLIP_DISCARD`, no partial presentation with `FLIP_DISCARD`, GDI stops working on the window after the first flip-model present.
- [DXGI 1.4 improvements](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-4-improvements): when to prefer `FLIP_DISCARD` (fully overwritten back buffer) vs `FLIP_SEQUENTIAL` (partial presentation).
- [Flip model, dirty rectangles, scrolled areas (DXGI 1.2)](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-2-presentation-improvements): dirty-rectangle tracking across buffers.
- [IDXGISwapChain1::Present1](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiswapchain1-present1): sync interval semantics, dirty rectangles.
- [IDXGIFactory2::CreateSwapChainForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforhwnd) and [ID3D11DeviceContext::Flush](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush): deferred destruction when replacing a flip swap chain (`ClearState` then `Flush`).
- [IDXGIFactory::MakeWindowAssociation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory-makewindowassociation): `DXGI_MWA_NO_WINDOW_CHANGES`.
- [DXGI overview](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi): Microsoft Basic Render Driver (vendor `0x1414`, device `0x8c`, software flag) and its non-software form; multithreading considerations.
- [DXGI_ADAPTER_FLAG](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ne-dxgi-dxgi_adapter_flag): `DXGI_ADAPTER_FLAG_SOFTWARE`.
- [ID3D11DeviceContext::UpdateSubresource](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-updatesubresource): destination box, default-usage resources.
- [SetWindowPos](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos) (`SWP_ASYNCWINDOWPOS`) and [MoveWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-movewindow): messages sent, cross-thread blocking.
- [Window features: layered windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features): `WS_EX_LAYERED` usable on child windows from Windows 8 (nothing about swap chains).
- [PrintWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-printwindow): synchronous; owner processes the request (`PW_RENDERFULLCONTENT` is not described there).
- [OutputDebugString](https://learn.microsoft.com/en-us/windows/win32/api/debugapi/nf-debugapi-outputdebugstringa) and [Debug in mixed mode (Visual Studio)](https://learn.microsoft.com/en-us/visualstudio/debugger/how-to-debug-in-mixed-mode?view=visualstudio): where debug strings go; enabling native code debugging.
- The Old New Thing: [Is it legal to have a cross-process parent/child or owner/owned window relationship?](https://devblogs.microsoft.com/oldnewthing/20130412-00/?p=4683) and [Sharing an input queue takes what used to be asynchronous and makes it synchronous](https://devblogs.microsoft.com/oldnewthing/20130607-00/?p=4143): implicit input-queue attachment for cross-thread parent/child.
- [Blend2D](https://blend2d.com/): high-performance 2D vector graphics engine with a built-in JIT pipeline compiler and optional multithreaded rendering.

# Plan: DXGI Hardware Swap Chain Presentation Backend for `RootView`

Status: **investigation + plan only, no implementation yet.**

**Blend2D stays as the only rasterizer, unconditionally, throughout every phase below.** This
plan changes *only* how an already-rendered `BLImage` buffer reaches the screen (GDI
`CreateDIBSection`+`BitBlt` today, a DXGI staging-texture+`Present1` after) - nothing here
touches how pixels get drawn (`repaint()`/`paintStyle()`/`paint()`/`paintChildren()`, `ViewStyle`,
`gfx::Fill`/`Gradient`/`shapes::*`, ...), and no alternative renderer (Direct2D, a GPU vector
pipeline, etc.) is in scope anywhere in this document.

## Motivation (real, not speculative)

Asked directly whether there's an actual current case driving this, the user's answer: real,
current paint-refresh glitches ("there are times when paint doesn't quite refresh right"), a real
belief that the current buffer-management/present structure "should be structured better,
cleaner," **and** a strategic reason - any future `newui`-based application wanting real hardware
acceleration (the user's own example: an image editor) needs this groundwork regardless, so it's
not wasted effort even if today's glitch turns out to have a smaller, more local fix. Also, the
user's own framing of the *current* design: "what we have today is not 100% a swap chain, but
it's close... this style of rendering is ideal for our setup, it just makes sense to polish that
off." That reframes Phase 2 below from a pure no-behavior-change refactor into the phase that
actually builds a real front/back-buffer swap-chain-shaped abstraction - see its own updated
section.

**On the specific glitch**: no concrete repro was given ("times when paint doesn't quite refresh
right" is a symptom description, not a reproducible case), so nothing below claims to have
diagnosed *the* bug - that needs a real repro before it's fixable with confidence. Two genuine,
already-latent candidate mechanisms turned up while reading the current code, worth checking
against whatever the user actually sees, not proposed as instead-of a real repro:

1. **`RootView::invalidate(const newui::Rect*)` (`rootview.cpp`) unconditionally clears
   `dirtyRect_` the moment it calls `::InvalidateRect()`** - but `InvalidateRect()` only *marks* a
   region for a future `WM_PAINT`; it doesn't paint synchronously, and Windows accumulates its own
   independent "update region" (queryable via `GetUpdateRect()`, delivered as `ps.rcPaint` at the
   real `WM_PAINT`) that can coalesce multiple separate `InvalidateRect()` calls, or gain a region
   from a source that never went through `markDirty()`/`invalidate()` at all (e.g. another window
   briefly covering this one). `dirtyRect_` and the OS's own pending update region can genuinely
   diverge - `repaint()` only re-renders *into* `imageBuffer_` for whatever `dirtyRect_` was at
   each individual `notifyRedrawNeeded()` call, while `paintImageBufferToWindow()` later `BitBlt`s
   from `imageBuffer_` for whatever `ps.rcPaint` Windows itself accumulated, which isn't
   guaranteed to be the same region. Worth instrumenting/logging both regions side by side against
   a real repro before concluding this is (or isn't) the actual cause.
2. **Already documented in `rootview.cpp`'s own comment** (`paintChildren()`, called
   unconditionally with no dirty-rect pruning): a child painting translucent/anti-aliased content
   with no opaque background under it re-blends the same edge pixels onto themselves on every
   repaint anywhere in the tree, visibly darkening/thickening over repeated repaints rather than
   staying idempotent - a different *kind* of "doesn't look right" than a stale/missing region,
   but a second real, already-known mechanism.

## Where this came from, and why it was rewritten

The original version of this file (and `DrawLoop.docx`, which the original content was
distilled from) is a generic AI-chat writeup of "how to do a DXGI swap chain in Win32" - useful
for the raw DXGI/Direct3D technique, but written with zero knowledge of this codebase. Its
starting premise (rewrite the app around a raw `wWinMain` + a continuous `PeekMessage` busy loop,
à la a game engine) is actively wrong for `newui`: this is a desktop UI framework, and
`RunLoop::run()` (`runloop.cpp`) already implements a proper idle-driven pump - `PeekMessage(...,
PM_NOREMOVE)` to check whether idle work is pending, `MsgWaitForMultipleObjects` to actually
*sleep* when there's nothing to do, then a real `GetMessage`/dispatch. Replacing that with a
continuous per-frame loop would be a regression (constant CPU burn instead of zero when idle),
not an improvement. This rewrite keeps everything about `RunLoop`/`Application`/`Frame` exactly
as-is and scopes the change to *only* how `RootView` gets its already-rendered pixels onto the
screen.

The real, useful technique from `DrawLoop.docx` - map Blend2D's CPU-rendered pixels into a
`D3D11_USAGE_DYNAMIC` staging texture via `D3D11_MAP_WRITE_DISCARD`, then `CopyResource()` into
the swap chain's back buffer and `Present1()` - is kept, with one real optimization the docx
didn't make: render Blend2D **directly into the mapped staging-texture memory** each frame,
instead of always rendering into a separate buffer and then `memcpy`-ing the whole thing across
(see Phase 3below - this removes one of the two full-frame copies the docx's own naive version
does, which matters most exactly on the iGPU/shared-memory-bandwidth case the docx's own analysis
is worried about).

## Where the real integration seam already exists

`RootView` (`rootview.h`/`.cpp`) already separates "render into a CPU buffer" from "get that
buffer onto the window" - and the second half already has a real seam, because `PopupTool`
(`popuptool.h`/`.cpp`) already needs a *different* presentation path (a translucent, per-pixel-
alpha popup, presented via `UpdateLayeredWindow()` instead of a plain `WM_PAINT`/`BitBlt`):

- **`BLFormat imageBufferFormat() const`** - `virtual`, already overridden by `PopupTool` to
  request `BL_FORMAT_PRGB32` (real alpha) instead of the default `BL_FORMAT_XRGB32` (opaque).
- **`void presentRepaintedBuffer()`** - `virtual`, called once per repaint after `repaint()` has
  finished writing into `getImageBuffer()`. Base implementation is `invalidate(&dirtyRect_)`
  (triggers an ordinary `WM_PAINT`); `PopupTool` overrides it to call `UpdateLayeredWindow()`
  directly instead.
- **NOT virtual today**: `resizeImageBuffer()`/`releaseImageBuffer()`/`paintImageBufferToWindow()`
  - these own the actual `CreateDIBSection()`/`memDC_`/`dibSection_`/`BitBlt()` GDI machinery, and
  are called directly from `RootView::handleMessage()`'s `WM_SIZE`/`WM_PAINT` cases. A DXGI
  backend needs to replace *this* half too, not just `presentRepaintedBuffer()` - `PopupTool`
  never needed to, since it still wants a plain GDI-backed `BLImage` to hand to
  `UpdateLayeredWindow()`.

So the real shape of this change is: **make buffer allocation/presentation together into one
small swappable strategy** (mirroring how `imageBufferFormat()`/`presentRepaintedBuffer()` are
already per-`RootView`-subclass hooks), with the existing `CreateDIBSection`/`BitBlt` code moved
into that strategy unchanged as the default, and a new DXGI strategy as an opt-in alternative -
not a rewrite of `RootView` itself, and no default-behavior change for the vast majority of
windows (or for `PopupTool`, which stays on GDI/`UpdateLayeredWindow` permanently - see Phase 5).

## Two real architectural risks the generic plan never considered

1. **`PopupTool`'s `WS_EX_LAYERED` + `UpdateLayeredWindow()` translucent-popup path is
   fundamentally incompatible with a plain DXGI flip-model swap chain.** A flip-model swap chain
   presents an opaque (or premultiplied-but-DWM-composited-as-a-normal-window) surface to the
   desktop compositor; `UpdateLayeredWindow()`'s per-pixel-alpha blending against *whatever is
   behind the window* is a completely different presentation model (real translucency needs
   `DirectComposition`, a materially bigger scope addition - `IDCompositionDevice`/visual trees).
   **Decision for this plan: explicitly out of scope.** `PopupTool` keeps its exact current GDI
   path forever; the DXGI backend is only ever opt-in for an ordinary opaque (or simple-alpha-
   blended-against-black, not real desktop translucency) `RootView`.
2. **A standalone, `WS_CHILD`-hosted `RootView`** (Part 91's work - `cpp_codetools`' VSIX
   extension hosts a `RootView` as a child of a VS-owned window, no `Frame` at all) is a real,
   already-shipping consumer. `CreateSwapChainForHwnd()` against a child `HWND` is supported by
   flip-model swap chains (Windows 8+), but has real-world reports of composition-ordering/flicker
   quirks when the *parent* window isn't itself DXGI-composited (which VS's own main window isn't
   guaranteed to be). **This needs an early, deliberate spike against the actual VSIX host**
   (Phase 1c below) before committing further - if it doesn't compose cleanly there, the DXGI
   backend may need to stay Frame-owned-top-level-window-only for v1, with standalone/child hosting
   staying on GDI.

## Phase 1: Feasibility spike (standalone, not wired into `RootView` yet)

- **1a. Link verification.** Add `d3d11.lib`/`dxgi.lib`/`dxguid.lib` to the `newui` target's
  `target_link_libraries()` (top-level `CMakeLists.txt`, alongside the existing
  `Comctl32.lib`/`Uxtheme.lib`/... list) and confirm a trivial `D3D11CreateDevice()` call links
  and runs through this project's actual MSBuild-via-`vcvars64.bat` build path (not assumed -
  confirm for real, this session already hit real Git-Bash/`cmd /c` build-tooling gotchas worth
  re-checking don't also bite a fresh linker dependency).
- **1b. Bare swap-chain smoke test**, isolated from `RootView` entirely - a throwaway
  `examples/`-style program (not wired into the real app) that creates a plain top-level `HWND`,
  a `D3D11_CREATE_DEVICE_BGRA_SUPPORT` device, an `IDXGISwapChain1` (`DXGI_SWAP_EFFECT_
  FLIP_DISCARD`, `DXGI_FORMAT_B8G8R8A8_UNORM`), and presents one solid color via the staging-
  texture-map technique from `DrawLoop.docx`. Confirms the whole chain actually works on this
  machine/driver before any real integration work starts.
- **1c. Child-HWND composition spike** (addresses risk #2 above) - same smoke test, but as a
  `WS_CHILD` of a plain parent window first, then (if that machine is available) inside a real VS
  instance the way `cpp_codetools`' VSIX host actually would. This is the one result that could
  redirect the rest of the plan (Frame-owned-only vs. also-standalone).

## Phase 2: Build a real front/back-buffer swap-chain-shaped abstraction (still GDI, real behavior
change - this is where the "polish off what's already close" work actually happens)

Given the motivation above, this phase is **not** a pure no-op refactor any more - it's the real
structural fix, on the existing GDI backend, before DXGI is added on top of it:

- Extract `resizeImageBuffer()`/`releaseImageBuffer()`/`paintImageBufferToWindow()` plus the
  existing `imageBufferFormat()`/`presentRepaintedBuffer()` hooks into one small, named strategy
  object `RootView` owns (matching this project's own convention - CLAUDE.md: "a one-off piece of
  general-purpose bookkeeping... gets its own small standalone class + header/source pair" -
  `namemanager.h`/`viewpath.h` are the precedent) - the seam Phase 3's DXGI backend needs regardless.
- **Real double-buffering**, not today's single `dibSection_`: a back buffer Blend2D/`repaint()`
  renders into, a front buffer `WM_PAINT`/`BitBlt` presents from, and one explicit, named "swap"
  step that only ever runs once a frame is fully rendered - never a half-painted buffer visible to
  `WM_PAINT`. This directly matches the user's own "not 100% a swap chain, but it's close" read of
  the current design, and gives Phase 3's real DXGI back buffer an exact structural analog to slot
  into later (front/back/present is the same shape DXGI itself already uses).
- **Make the dirty-region bookkeeping self-consistent by construction**, addressing candidate #1
  above directly: instead of `dirtyRect_`/Windows' own accumulated update region being two
  independently-tracked things that can diverge, the "present" step becomes the single place that
  both finalizes what was actually rendered *and* drives the resulting `InvalidateRect()`/`BitBlt`
  region - no window for them to disagree. Whether this alone was the actual cause of the user's
  glitch needs verifying against a real repro (see Motivation above), but it's a real correctness
  improvement either way, independent of whether it turns out to be *the* fix.
- `PopupTool` keeps working against the same restructured GDI strategy (still `UpdateLayeredWindow`
  for presentation, just against the new front/back buffer shape underneath) - verify its own
  behavior is unchanged, it's the one existing consumer of the alternate `presentRepaintedBuffer()`
  path.
- Verified by the full existing suite, twice (project convention) - **not** "zero regressions
  expected" this time, since real behavior is changing; also needs live visual verification
  (`EnumWindows`-by-PID + screenshot, this project's established pattern) specifically exercising
  resize/rapid-repaint scenarios, since that's where the current glitch is reported and where a
  real front/back-buffer fix would show its effect.

## Phase 3: Real DXGI backend, wired in as an opt-in alternative strategy

- Device + swap chain created in `RootView::viewCreated()` (the real `WM_CREATE` hook - matches
  this project's own established "per-window lifecycle-sensitive setup goes here" pattern,
  CLAUDE.md) - not in a constructor, since the real `HWND` doesn't exist yet at that point.
- `WM_SIZE` drives `ResizeBuffers()` + re-creating the staging texture at the new size (same
  `releaseImageBuffer()`-then-recreate shape the existing GDI path already has for
  `resizeImageBuffer()` - not new architecture, same lifecycle, different backing store).
- **The real optimization over `DrawLoop.docx`'s own example**: `repaint()`'s `BLContext ctx(...)`
  targets a `BLImage` wrapping the *mapped* staging-texture memory directly
  (`D3D11_MAP_WRITE_DISCARD` at the start of the frame, `Unmap()` after `repaint()` finishes) via
  `BLImage::create_from_data()` - exactly the same pattern `resizeImageBuffer()` already uses for
  the GDI DIB section today, just pointed at D3D11-mapped memory instead of `CreateDIBSection()`'s.
  This skips the extra whole-frame `memcpy` the docx's own version does (render into a separate
  `BLImage`, then copy it into the mapped texture) - one less full-frame copy, which matters most
  on exactly the shared-memory-bandwidth-constrained iGPU case the docx's own research flagged.
- **Dirty-rect `Present1()`, fed from data `RootView` already has** - `dirtyRect_` already exists
  and is already unioned correctly across multiple `markDirty()` calls before each repaint
  (`RootView::markDirty(fromView, rect)`, `rootview.cpp`). The generic plan's "Phase 4: architect
  dirty rectangle tracking" is mostly already built; this phase just needs to translate the
  existing `dirtyRect_` into one `RECT` for `DXGI_PRESENT_PARAMETERS::pDirtyRects` at `Present1()`
  time - no new tracking mechanism.
- `WM_PAINT` becomes passive (`ValidateRect`/`BeginPaint`+`EndPaint` with no drawing) for a
  DXGI-backed `RootView`, matching the docx's own correct observation that mixing GDI painting
  and swap-chain presentation on the same `HWND` is what causes flicker/tearing.

## Phase 4: Robustness the generic plan skipped entirely

- **Device-lost handling.** A real GPU driver crash/update/TDR is not hypothetical - `Present()`
  can return `DXGI_ERROR_DEVICE_REMOVED`/`DXGI_ERROR_DEVICE_RESET`. Needs a real recreate-device-
  and-swap-chain path, not a crash or a silently-frozen window. `docx`'s own example has zero
  error handling anywhere (every `D3D11CreateDevice()`/`CreateSwapChainForHwnd()`/`Map()` call's
  `HRESULT` is either ignored or only checked for the happy path).
- **No-D3D11-driver fallback.** A remote-desktop session, a locked-down VM, or genuinely ancient
  hardware may not have a usable D3D11 driver at all. `RootView` must degrade to the existing GDI
  strategy automatically when device creation fails at startup - same "degrade silently, don't
  throw" convention this project already uses for other optional Win32 features (CLAUDE.md's own
  dark-mode-support functions, `uicolormanager.h`) - never a hard requirement that breaks every
  window on a machine without it.
- **Minimize/restore.** `DrawLoop.docx`'s own `WM_SIZE` handler already correctly special-cases
  `SIZE_MINIMIZED` (skip resizing to 0x0) - worth keeping, but needs a real minimize/restore cycle
  tested against `RootView`'s actual `WM_SIZE` case (`rootview.cpp`), not assumed to transfer
  cleanly.

## Phase 5: Explicitly out of scope for this plan

- **`PopupTool`/any real per-pixel-translucent popup staying on DXGI** - see risk #1 above.
  Permanently GDI/`UpdateLayeredWindow`, not a "later phase."
- **16-bit color (`B5G6R5`) fallback** - a real mitigation `DrawLoop.docx`'s own research
  mentions for extreme iGPU-bandwidth cases, but no concrete need for it exists in this codebase
  today (`imageBufferFormat()` is already always 32bpp for both existing formats) - noted as a
  possible future Phase 4-of-Phase-4 if a real low-end-hardware complaint ever shows up, not
  designed for speculatively.
- **V-sync/frame-rate-capping policy** - `Present1(1, ...)` (locked to vsync) is the obviously
  correct default for a mostly-static UI (this is not a game render loop), but isn't itself
  something this plan needs to design further.

## Verification strategy (this is genuinely different from every other feature in this codebase)

- This isn't gtest-testable in any meaningful way (a real windowed swap chain, real GPU device) -
  verification has to be live, matching this project's own established pattern (`EnumWindows`-by-
  PID + screenshot, used throughout `HANDOFF.md` Parts 90-97).
- **One real gotcha to fix before that pattern even works here**: `PrintWindow()` (this project's
  own established screenshot method) does not reliably capture a `DXGI_SWAP_EFFECT_FLIP_DISCARD`-
  presented surface without passing `PW_RENDERFULLCONTENT` (Windows 8.1+) - confirm this flag is
  used for any DXGI-backed `RootView`'s own live verification, or the screenshot may come back
  blank/stale even though presentation is genuinely working.
- Full existing suite (currently 1262/1262) must stay green, run twice per this project's own
  order-sensitivity convention, at the end of every phase above that touches `RootView` itself
  (Phase 2 especially - real double-buffering behavior change touching every existing GDI-backed
  window, the phase most likely to regress something if done carelessly).

## Resolved: real motivation, not exploratory

Per the Motivation section above, this is real, current work (a real glitch, a real "this should
be structured better" judgment, and a real future hardware-acceleration need), not speculative
future-proofing - so **Phase 4's robustness work (device-lost handling, no-D3D11-driver fallback)
is in scope to build properly, not deferred** to "only if it actually gets used somewhere real."
Phase 2 (the real GDI-level restructure) is the priority to land first regardless of how far
Phase 3+ (actual DXGI) ends up going in the near term - it's the part most directly addressing the
glitch/structure complaint on its own, and doesn't require the Phase 1c child-HWND composition
spike to resolve favorably before it can proceed.

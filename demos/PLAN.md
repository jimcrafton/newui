# newui demos - plan

Two demo apps that exercise newui end to end. **sketch is built first**, then manyrows.
Both live under `demos/`, are added with `add_subdirectory(demos)` from the root
`CMakeLists.txt`, and link `newui` (Blend2D comes in transitively).

```
demos/
  PLAN.md
  CMakeLists.txt        (adds sketch, manyrows)
  sketch/    Resources/ (.newui UI files)
  manyrows/  Resources/ (.newui UI files)
```

## UI authoring approach

App chrome (window, toolbars, panels, status bar) is authored as **`.newui` JSON5 files** in each demo's
`Resources/`, loaded through `Bundle` (see the `newui-view-json5` skill in `D:\code\newui\.claude\skills`),
copied next to the exe with `newui_add_resources()`. Custom views (`CanvasView`, `TableView`, `StatusBar`)
must be reflected (reflectgen) so a `.newui` file can reference them by `"type"`, and delegates
(button clicks, selection changes) are wired via the file's `delegates` connections or in code after load.
Interactive logic stays in C++ controllers.

## Coding conventions (from `D:\code\newui\CLAUDE.md` and `cpp_naming_conventions.md`)

- Match framework style: property-style getters (`name()`/`setName()`), `is`/`has` for bools, private members
  with trailing underscore, `Delegate<>` events returning `SyncReturn`, heavy explanatory comments like the
  framework headers, one class per header/source pair under `demos/<app>/`.
- Indices/rows/columns are `std::size_t`; invariants `throw std::runtime_error("Class::method: ...")`, no `assert()`;
  never `std::min`/`std::max` (use ternaries); new enum-to-string helpers return `std::string`.
- Views are heap-only (`new`, freed via `destroy()`); reflection-constructible classes need an explicit
  `Foo() = default;`; every exe calls `registerReflectionData()` at the top of `main()`.
- Build via the VS/MSBuild `build/` tree with vcvars64 (redirect output to a log); tests are run as an exe, not ctest;
  rerun twice after touching singletons.
- **No git commits without asking first** - the "commit per phase" below means *propose* a commit and wait for approval.
- Update `HANDOFF.md` conventions apply to the framework repo; here, keep this plan's phase status current.

## Framework audit (what exists vs. what the demos must build)

| Need | In newui today | Gap |
|---|---|---|
| Document + file I/O | `Document` (models.h): load/save, dirty flag, `.bak`; `DocumentController` | none - subclass it |
| Undo/redo | `UndoStack` (`UndoableAction{doIt,undoIt}`); `commands::undo/redo` | grouped/coalesced actions added in the demos |
| Commands/menus | `Action`, `CommandTable`, `MenuItem`, `commands::copy/paste/...` | none |
| Toolbar | `Toolbar`, `ToolbarButton` (checkable), `ToolbarSeparator` | tool-palette radio grouping to verify |
| Shapes | `Shape` hierarchy (Line, Rectangle, RoundRect, Circle, Arc, Path, Curve, Text), `ShapeLayer`, `ShapeStyle`, `Transform2D` | render only: no hit-testing, handles, ids, groups, SVG serialization |
| Colors | `Color` with HSL/HSV/Lab/CMY/CMYK/YUV conversion | RGB-only storage: CMYK colors must keep their own components |
| Raster export | Blend2D `BLImage` codecs (PNG, JPEG, BMP, QOI, ...) | none |
| SVG | `renderSvgFile()` (svgandme, render-only) | need our own editable SVG reader/writer |
| PDF | nothing (Blend2D has no PDF backend) | write a small vector PDF writer |
| Clipboard | `ClipboardManager` text / MIME / image | none |
| Table | `TableController`, `TableItem`, `ItemController` (item pooling) | **no `TableModel`, `TableView`, `StatusBar`** - manyrows builds them |
| App args | `main()` is ours | parse `argv` ourselves |

Assumption to confirm: generic gaps (`TableModel`, `TableView`, `StatusBar`, hit-testing helpers) start in
`demos/` and are promoted into `include/newui` only if you want them there.

---

# 1. sketch

### Architecture

- **`SketchDocument : Document`** - owns the object tree (layers -> shapes/groups), page size/units and an
  `UndoStack`. `readFromFile`/`writeToFile` implement SVG. Mutations go only through command objects, which
  call `markModified()` and `updateAllViews()`.
- **`SketchObject`** - wraps a `newui::Shape` (geometry/render reuse) plus stable id, name, visible/locked,
  `SketchColor` fill/stroke, `hitTest(point, tolerance)` (via `BLPath::hit_test` + stroke tolerance),
  `bounds()`, `handles()`. `SketchGroup` holds children. Types: Line, Rect, RoundRect, Ellipse, Polygon,
  Polyline, Path (Bezier nodes), Text.
- **Paint** (`SketchPaint`) is what fill and stroke hold: None, Solid (`SketchColor`), LinearGradient or RadialGradient. A gradient is
  `{type, start, end (radial: center, focal, radius), stops[{offset, SketchColor incl. alpha}], spread (pad/repeat/reflect), units}`; stops are
  full `SketchColor`s so CMYK stops stay CMYK. Rendered with Blend2D `BLGradient` (maps onto newui `Gradient`, graphics.h). Edited with the
  Gradient tool (G) - drag on the object to set start/end, on-canvas handles move them - and a gradient editor in the Color panel
  (stop bar: click to add, drag to move, Delete to remove, per-stop color/alpha/offset, reverse, type, spread). SVG: `<linearGradient>`/
  `<radialGradient>` in `<defs>` with `gradientUnits`, `<stop stop-color stop-opacity>`, `spreadMethod`, referenced by `url(#id)`; CMYK stops get
  `data-cmyk`. PDF: axial/radial shadings (types 2/3) with an exponential or stitching function from the stops; gradients with per-stop alpha use a
  soft mask (`SMask`) built from a grayscale shading, CMYK gradients use `DeviceCMYK`. All edits undoable (gradient drags coalesce).
- **Appearance stack** (modelled on Illustrator's Appearance panel; references: Adobe Illustrator Appearance panel / Stroke panel
  documentation and tutorials). An object does not have "a fill and a stroke" but an ordered **Appearance**: a list of attributes painted bottom-to-top,
  each with its own visibility, opacity and blend mode:
  - **Fill** (any number) - `SketchPaint` (none/solid/gradient), opacity, blend mode.
  - **Stroke** (any number) - paint plus *weight, cap, join, miter limit, align (center / inside / outside), dash pattern (up to 2 dash/gap pairs),
    start/end arrowheads with scale, width profile (uniform, taper, bulge, and later custom via a width tool)*. Inside/outside alignment is done by
    clipping to the path and doubling the weight; arrowheads are generated geometry; profiles are a generated outline. Open paths support center only
    (and arrowheads), as in Illustrator.
  - **Effect** (any number, live and re-editable) - Drop Shadow, Inner Shadow, Outer Glow, Gaussian Blur, Round Corners, Offset Path, Roughen. The first
    three reuse newui `DropShadow`/`Glow` (shapes.h); blur is our own separable box-blur pass on an offscreen layer (Blend2D has no blur).
  - **Object opacity + blend mode** (the base row) and a "group opacity" toggle.
  Blend modes: Normal, Multiply, Screen, Overlay, Darken, Lighten, Color Dodge, Color Burn, Hard Light, Soft Light, Difference, Exclusion -
  mapped to Blend2D comp ops. The panel (`ListView`-style stack) supports eye toggle, drag to reorder, select an item to edit it in the Stroke/Color/Opacity panels,
  Add stroke/fill/effect, Duplicate, Delete, Clear Appearance. Every operation is one undoable action.
- **Graphic Styles**: a named, saved Appearance; click a tile to apply it to the selection, "new" saves the selection's appearance, alt-click on a style
  replaces it, and editing a style can update every object using it (objects keep a style reference + local overrides). Styles live in the document and are
  exported/imported as a `.svg` of swatches.
  Persistence: because plain SVG cannot express multiple fills/strokes, an object with a non-trivial appearance is written as a `<g data-sketch="appearance">`
  holding one `<use>`/path element per fill and stroke (each with `fill`/`stroke`, `opacity`, `style="mix-blend-mode:..."`), effects as `<filter>`
  (`feDropShadow`, `feGaussianBlur`, ...) so other viewers still render it, and sketch-specific data attributes to restore the live structure on reload.
  PDF export draws attributes in order with `ExtGState` (`ca`/`CA`/`BM`); effects that PDF cannot express (blur, shadow, glow) are rasterized into image XObjects
  at export resolution. Everything uses the same stack for the raster exporter.
- **Opacity** is first-class at three levels, all in 0..1 and all undoable: per-color alpha (`SketchColor::alpha`, i.e. fill
  alpha and stroke alpha), whole-object `opacity` (applies to the fill+stroke composite, and to a group as a unit), and
  layer opacity. Rendered with `BLContext` `set_fill_alpha`/`set_stroke_alpha` and `set_global_alpha` (groups/objects with
  opacity < 1 render into a layer so overlapping fill/stroke do not double-blend). SVG: `fill-opacity`, `stroke-opacity`, `opacity`.
  PDF: `ExtGState` `ca`/`CA` for color alpha, and a transparency group XObject for object/group opacity. UI: an Alpha slider
  in the Color panel and an Appearance panel (object opacity, fill A, stroke A). Raster export keeps alpha (PNG/QOI) and flattens on
  the chosen background for JPEG/BMP.
- **`SketchColor`** - `{ColorSpace space; float c[4]; float alpha}`, space in {RGB, CMYK, Gray, HSB}. Keeps the
  *authored* components and converts to RGB for display via `Color` (naive CMYK->RGB, no ICC; documented limit).
- **`SketchController`** (interaction controller) - active tool, selection, snapping/guides, zoom/pan. Tools are
  small state machines: Select, Direct-select, Rectangle, Ellipse, Line, Pen (Bezier), Pencil/Polyline, Text,
  Zoom, Hand. Receives mouse/key events from `CanvasView`, issues commands.
- **`CanvasView : SubView`** - paints the document with Blend2D under zoom/pan, then the selection overlay
  (bounding box, 8 resize handles, rotate handle, path nodes/control handles, marquee, guides). Registered as a
  model view so document changes repaint it.
- **Panels** (`.newui`) - tool palette `Toolbar`; top `Toolbar` (undo/redo, arrange, group, align); `ColorPanel`
  (RGB/CMYK/Gray sliders + hex, fill/stroke swatches, stroke width/cap/join); `LayersPanel` (`ListView`);
  status line (zoom, cursor coords, tool hint).
- **Undo/redo** - every edit is an `UndoableAction`: Add, Delete, Transform, ChangeStyle, Reorder,
  Group/Ungroup, EditPathNode, EditText. Interactive drags coalesce into one action on mouse-up. Ctrl+Z/Ctrl+Y
  and toolbar buttons track `canUndo()/canRedo()` and show `undoDescription()`.

### SVG as native format
- Write: `<svg width height viewBox>`, `<g id>` per layer, `rect/ellipse/line/polygon/polyline/path/text`,
  `transform="matrix()"`, fill/stroke as `#rrggbb` + `fill-opacity`.
- **Color-space round trip:** CMYK colors are written as RGB fallback *plus* `data-fill-cmyk="c m y k"` /
  `data-stroke-cmyk`, so other viewers still render and sketch restores the authored space exactly.
- Read: own tolerant parser for the subset above (path `d` commands M L H V C S Q T A Z, absolute/relative;
  `transform`; `style=` and presentation attributes). Unsupported elements (filters, gradients in phase 1,
  `<use>`, CSS classes) are kept as an opaque read-only object rendered via `renderSvgFile` where feasible,
  otherwise skipped with a warning in the status line.

### Export
- **Raster:** File > Export... picks PNG / JPEG / BMP / QOI (whichever `BLImage` codecs are available), pixel
  size or DPI scale, transparent/solid background; renders offscreen with `BLContext` into a `BLImage`.
- **PDF:** own `PdfWriter` (no external lib) walking the same object tree and emitting vector content streams
  (`m l c re h f S B`, `cm`, `gs` for alpha). RGB -> `rg/RG`, CMYK -> `k/K` (true CMYK in the PDF), Gray ->
  `g/G`. Text via the 14 standard fonts first; font embedding is a stretch goal.

### New Document dialog
Mockup: `sketch/mockup/new-document-dialog.html` (interactive - preset selection, unit conversion, orientation swap, ppi, summary).
Presets are grouped Print / Web / Mobile / Art (Letter, Legal, Tabloid, A3-A5, B5, business card, poster, postcard; Full HD, HD, desktop,
tablet, social square, banner; phone/tablet/app icon; square/illustration/custom) and carry a default unit, resolution and color mode.
Settings: name, width/height, **unit** (points, pixels, millimeters, centimeters, inches, picas), **orientation** (portrait/landscape, swaps
width/height), **color mode** (RGB/CMYK), **resolution** (ppi: 72/96/150/300/600 or any value), background (white/transparent/current fill),
keep-proportions toggle. Changing the unit converts the numbers in place (physical size preserved; pixels use the ppi); the summary shows the
size in pt, in, mm and pixels at the chosen ppi. The document stores its size in points plus ppi and color mode (SVG `width`/`height` in points,
`data-sketch-ppi`/`data-sketch-colormode` attributes). Built as `newdocument.newui` (a Frame loaded modally via `Dialog`, see dialogs.h) with a
custom `PagePreview` view and a thumbnail-item `ListView`; wired to File > New (Ctrl+N).

### Application / CLI
- `sketch.exe [file.svg]` opens that file at startup (missing file -> new doc with that path pre-set; parse
  failure -> message box + empty doc). Extra: `--export out.png|out.pdf` headless convert (also makes export
  testable). File > New/Open/Save/Save As, `DocumentController` unsaved-changes prompts on close.
- Clipboard: Cut/Copy/Paste/Duplicate. Copy writes `application/x-sketch-objects` (lossless SVG fragment),
  `image/svg+xml`, and a PNG bitmap so it pastes into other apps. Paste prefers the private format, then SVG
  text, then image; pasted objects are offset so they don't sit on the originals.

### Direct manipulation (all shapes)
Handles are small by design (about 5 px squares for resize, 3 px radius for rotate, 4.5 px for path anchors - half the first mockup) with a 0.75 px outline (selection frames, marquee, path and gradient guides use the same 0.75 px line); hit-testing uses a larger invisible target (about 9 px) so they stay easy to grab. Click/shift-click/marquee select; drag to move (arrows nudge, shift = 10x); 8 resize handles (shift = keep
aspect, alt = from center); rotate handle; corner-radius handle on RoundRect; Direct-select edits path nodes
and Bezier handles (add/delete/convert smooth<->corner on Pen tool); line endpoints draggable; in-place text
editing. Snap to grid/guides/object edges (toggle). Z-order commands, group/ungroup, lock, hide.

### Phases (each ends runnable, with unit tests)
- **S0 Scaffold - DONE (UI loads and lays out):** `sketch.newui` (generated by `tools/gen_newui.py` from `mockup/sketch-mockup.html`),
  39 icons + 17 cursors as SVG (`tools/gen_assets.py`), stub `CanvasView`/`GradientBar`/`SwatchGrid`, `sketch.exe [file] [--check]`.
  The right-hand panels live in a scrolling `ScrollView`; in a `.newui` file its `childViews[0]` is the ScrollView's own internal
  viewport (the reader merges file element i into existing child i), with the real content nested inside it - see `sidebar()` in
  `tools/gen_newui.py`.
  Old S0 text:  `demos/CMakeLists.txt`, `sketch.exe` with empty window, menu, toolbars, canvas (`.newui`), `argv` stub.
- **S1 Model + render:** `SketchDocument`, objects, `SketchColor`, canvas paints a hard-coded scene; zoom/pan.
- **S2 Create + select:** shape tools, selection, move/resize/rotate handles, undo/redo commands and UI.
- **S3 Style + color:** color panel (RGB/CMYK/Gray), alpha/opacity, gradients (model, renderer, editor, gradient tool)
- **S3b Appearance:** attribute stack (multiple fills/strokes), full stroke options, blend modes, effects (shadow/glow/blur/round corners), graphic styles, stroke options, z-order, group/ungroup, layers panel.
- **S4 SVG I/O:** writer, reader, round-trip tests (incl. CMYK attrs), open from command line, save/save as, dirty prompts.
- **S5 Pen/path editing + text.**
- **S6 Export:** raster export dialog, `PdfWriter`, check PDFs open in a viewer and contain CMYK operators.
- **S7 Clipboard + snapping/guides + polish.**

### Risks
1. The PDF writer is the largest new piece; text/fonts are the hard part (standard-14 only at first).
2. Direct manipulation of rotated shapes needs oriented handles (per-object `Transform2D`).
3. Mesh gradients, pattern fills, live-paint and symbols are out of scope for the first pass.
4. CMYK is device-naive (no ICC profiles).

---

# 2. manyrows

### Architecture
- **`TableModel : Model`** (`rowCount()`, `colCount()`, `cell(row,col)`, `setCell`) - the 2-D counterpart of
  `ListModel`. **`ManyRowsModel`** holds **1,000,000 rows** x ~10 typed columns (int, double, string, date),
  generated lazily from a seeded function plus an **edit overlay** (`unordered_map<(row,col), value>`), so
  startup is instant and memory small. Sort/filter use an index permutation (`vector<uint32_t>`, ~4 MB).
- **`SpreadsheetController : TableController`** - selection (cell, range, row/column, select-all), in-place
  edit state, TSV clipboard, fill-down, keyboard navigation (arrows, Tab/Enter, PgUp/PgDn, Ctrl+arrows,
  Home/End), column resize; issues undo commands.
- **`CellItem : TableItem`** - paints one cell (alignment/format by column type, selection/active border,
  edit caret); pooled via `createItem/releaseItem` for visible cells only.
- **`TableView : Control`** (new, in a `ScrollView`) - virtualized: visible row/column range from scroll
  offset; paints only those cells plus frozen header row and row-number gutter. Fixed row height makes
  row->y O(1). 1M x 22px is ~22M px, so scroll offset is a `double` with a scaled scrollbar thumb (float
  precision is a real risk; proven in M1).
- **Undo/redo:** `UndoStack` with cell-edit, range-paste, fill, sort, column-resize actions storing deltas
  only (sparse old/new lists), never a table copy.
- **`StatusBar : SubView`** (new) - panes: *render time (ms)* (last / smoothed / worst, measured around
  `TableView::paint()` with `steady_clock`), visible range, selection (`A1:C10`, count/sum/avg), total rows.

### Phases
- **M0** scaffold + `StatusBar`. **M1** `TableModel`/`ManyRowsModel` + virtual `TableView` paint (1M scroll proof).
- **M2** selection + keyboard nav. **M3** in-place editing + undo/redo. **M4** clipboard (TSV), fill, column resize.
- **M5** sort/filter via index permutation. **M6** perf pass - target full visible-window paint under ~8 ms in
  Release, shown live in the status bar.

### Risks
Float scroll precision at ~22M px; per-cell `std::any` allocations in `Model::value()` on the paint hot path
(add a non-`any` fast path such as `cellText(row, col, std::string&)`); text shaping cost (cache laid-out text
for visible cells only).

---

## Order of work
1. `demos/` scaffold + CMake (`add_subdirectory(demos)` in root).
2. **sketch S0 -> S7**, proposing a commit per phase on the `demos` branch (only with your approval).
3. manyrows M0 -> M6.

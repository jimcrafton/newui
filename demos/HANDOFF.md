# Demos handoff

_Last updated: 2026-10-11. Nothing is committed - ask before committing._

Plan: [PLAN.md](PLAN.md). Two demos on newui: **sketch** (vector editor, first) and **manyrows** (1M-row table).

## Where things stand

**Framework additions (include/newui, src, unittests) - done, tested (1700 tests, run twice):**
- `StatusBar`/`StatusPane`, `TableView` (+ `CellRange`) in controls.h/.cpp; `TableModel` in models.h; `TableController` geometry/typed model in
  controllers.h/.cpp; tests in `unittests/test_table.cpp`. `TableView` selection getters are `@reflect ignore=true`.
- Known limit: float scroll offsets are exact only to ~760k rows at 22 px (see TableView class comment).

**sketch S0 - done (`demos/sketch/`):**
- `mockup/sketch-mockup.html` (main window) and `mockup/new-document-dialog.html` are the design source of truth; each region's `data-newui`
  names its control (press N). Mockup first, then `.newui` - the user's workflow.
- `tools/gen_newui.py` -> `Resources/sketch.newui` + `Resources/newdocument.newui`; `tools/gen_assets.py` -> 66 icons + 17 cursors (SVG, with a white
  halo; cursors have hotspots in `cursors/cursors.json`). Edit generators, not outputs.
- Custom views (stubs): `CanvasView`, `GradientBar`, `SwatchGrid`, `PagePreview`; reflected via `registerSketchReflection()`.
- `sketch.exe [file.svg] [--check] [--depth=N]`: `--check` loads both frames, lays out, prints the tree (the smoke test).

**sketch S1 - code done, builds, user has run it and the look is approved (centered page, small rulers, 12-20 px icons, darker panel headers, dividers):**
- `sketchcolor` (`SketchColor`, RGB/CMYK/Gray/HSB, hex, naive CMYK->RGB), `sketchobject` (`SketchPaint` none/solid, `SketchObject` with
  Line/Rect/RoundRect/Ellipse/Polygon/Polyline/Path factories, bounds, `hitTest`, `paint`), `sketchdocument` (`SketchDocument : Document`, flat
  object list, ids; SVG read/write landed in S4).
- `CanvasView`: `setDocument()`, pan (middle/right drag, wheel, Shift+wheel), Ctrl+wheel zoom about the cursor (`zoomAt`), `toDocument/toView`;
  rulers follow the pan. `main.cpp` attaches an empty document to the view named `canvas` (the old hard-coded demo scene was removed in S4).
- Also done: `sketch_tests` target (`demos/sketch/tests/test_sketch_model.cpp`, 23 tests: color/object/document) and zoom wiring (combo <-> canvas <-> status pane; View menu Zoom In/Out/Fit/Actual Size/Show Grid/Show Rulers, in `main.cpp`). Builds, tests pass; the zoom menu/combo sync is not yet eyeballed in the running app.
- Not wired: every button/menu, New Document logic (categories, unit conversion, summary), SVG open.

**sketch S2 - code done, builds, 34 model tests pass; NOT yet tried interactively:**
- Model: `SketchObject::transform/setGeometry/geometryBounds/clone` (moves/resizes/rotates are baked into the outline - no per-object matrix, so handles stay axis-aligned); `SketchDocument` selection (`selection()`, `setSelection`, `selectAll`, `selectionBounds`, `onSelectionChanged`) and undoable edits on `undoStack()` (`addUndoable`, `deleteSelection`, `duplicateSelection`, `transformObjects`, `commitGeometry` for live drags).
- `CanvasView` tools (`SketchTool`: Select/Rectangle/Ellipse/Line/Hand): click/shift-click/marquee select, drag to move (Shift = axis lock), 8 resize handles (Shift = uniform on corners), rotate knob (Shift = 15 deg), draw shapes (Shift = square/circle/45 deg line), keys Del, arrows (Shift = 10), Ctrl+Z/Y/A/D, Esc (cancels a drag, else deselects). Overlay painted in `paintOverlay`.
- `main.cpp` wires the tool palette (radio over the toggles), Edit menu (Undo/Redo/Delete/Duplicate/Select All/Deselect) and the undo/redo/delete toolbar buttons, plus a selection-count status message.
- Oriented handles: each object has a `frameAngle` (outline stays baked); `SketchDocument::selectionFrame()` returns a `SelectionFrame` (angle + box in the rotated frame) that the canvas uses for the outline, 8 handles and rotate knob, and resizing is done in that frame (M = R(a)*Scale*R(-a)). Mixed angles in a multi-selection fall back to an upright frame. Undo state is `GeometryState` (path + angle).
- Transform origin: `SketchDocument::selectionOrigin/setSelectionOrigin` (fraction of the frame box, resets on selection change, not undoable); drawn as a ringed dot, drag to move; it is the rotate and shear pivot.
- Alt+Shift on a corner = perspective warp (homography of the frame rect onto the dragged quad, applied via `SketchObject::warp`, control points mapped not re-fitted); Alt+Shift on a middle handle = shear about the origin. Both undoable.
- Warp keeps its quad as the selection frame (`SketchDocument::quadFrame()`, `SelectionFrame::quad/asQuad/pointAt`): while it is set every handle edits the quad (corner follows the mouse, edge slides along its normal) via the projective map old quad -> new quad; move/rotate/nudge carry it; it is part of the undo step (`FrameState`) and is dropped when the selection changes. Resize/shear handles only exist on rectangular frames. The sketch model structs in sketchdocument.h need `// @reflect ignore=true` or reflectgen tries to reflect them.
- Object > Transform > Reset Bounding Box (`SketchDocument::resetBoundingBox()`, undoable): drops the quad and group angle and zeroes the objects' frame angles.
- A rotated multi-selection keeps `groupAngle()` (used when members' angles differ; dropped on selection change; part of the undo step).
- Gotcha: this Blend2D's `BLMatrix2D::make_rotation(angle, x, y)` does NOT pivot about (x, y) (rotates about the origin then translates) - build translate/`post_rotate`/translate instead.
- `sketch.exe --check-tools` headless-tests the tool palette wiring (palette buttons are `ToolbarButton`, a `Control`, not a `Button`).
- Not done: cursors per tool (assets exist), undo/redo enabled state and Edit-menu item text, zoom tool, direct-select, snap to grid, alignment/z-order buttons, panels reflecting the selection (S3).

**sketch S3a - Color / Stroke / opacity panels wired (solid + none, RGB only); builds, 56 model tests + `--check-tools` panel checks pass; NOT yet tried by hand:**
- `StylePanel` (`stylepanel.h/.cpp`, created in `main.cpp`): the fill/stroke wells in the tool palette choose the target (thicker border = active; header shows "Fill/Stroke" dot); R/G/B/A sliders (+ value boxes), hex field, swatch grid (`SwatchGrid::onSwatchPicked`), fill none/solid buttons, stroke weight field, opacity slider all edit the selection, or the document defaults (`defaultFill/Stroke/StrokeWidth`, used by the draw tools) when nothing is selected.
- Slider drags are live (`setSelectionFill/...`, no undo) and become ONE undo step on mouse-up (`commitStyle`); typed values commit immediately. The panel refreshes from `SketchDocument::onChanged` / `onSelectionChanged` (multi-selection shows the first object).
- Gradients (S3b-lite): `SketchPaint` kinds None/Solid/Linear/Radial with `SketchGradient` (stops, angle, spread), defined relative to the object's bounding box so they follow moves/resizes/rotations (a warp does not distort them); rendered in `SketchObject::paint` via Blend2D gradients, for fills and strokes. `GradientBar` is interactive (click marker = select, drag = move, click bar = add stop; fires `onStopsEdited`/`onStopSelected`). `StylePanel`: linear/radial buttons convert between kinds, sliders/hex/swatches edit the selected stop, Stop %, Angle, Spread, add/delete/reverse stop all wired; every edit is a normal undo step (a bar drag is one). Rendering is unit-tested by rasterizing into a `BLImage`.
- Not done: the on-canvas gradient tool (the gradientTool palette button does nothing), radial center/radius handles, gradient strokes along the stroke direction.
- Not done in S3: Gray/CMYK/HSB modes (the mockup has only 4 slider rows - add a 5th, in the mockup first), gradients (the linear/radial buttons snap back), cap/join/dash/arrows, appearance stack + blend modes, graphic styles, layers, group/z-order.
- `--check-tools` also drives the panel headlessly (select, slider, hex, undo).

## Build / run
```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d D:\code\newui-demos && cmake --build build --config Debug --target sketch > build_log.txt 2>&1'
build\demos\sketch\Debug\sketch.exe --check
build\unittests\Debug\newui_tests.exe        # run twice
```
Needs `tools/reflectgen/.venv` (created; gitignored). Resources are copied next to the exe at build time (rebuild to pick up icon/.newui changes).

## Gotchas learned
- Icon SVGs must not carry `width`/`height` (renderSvgFile would draw at that size, clipping when smaller); `ToolbarButton::iconSize` (new) sets the raster size.
- In `.newui`, put `model` before `selectedIndex` on a DropDownList or the selection is dropped.
- Labels in a row need an explicit width or they lay out at 0.
- `.newui`: the reader merges file element *i* of a collection into the existing child *i*. A `ScrollView`'s `childViews[0]` is its internal viewport
  (content nests inside it). Put `bounds` before `layout`/`childViews`. `ToolbarSeparator.horizontal` must match the toolbar and be read before
  `desiredSize`. A `SegmentedControl` in a centering row needs an explicit width. Bare `SubView` needs `visible: true`.
- Bash tool chokes on some heredocs (apostrophes); write scripts with the Write tool. In C++ edits via Python, `\n` inside printf strings gets mangled -
  use Edit.
- IDE clang diagnostics on these files are include-path noise; the MSBuild result is authoritative.

## S4 SVG I/O (done; user verified save/open/close prompts)
- `sketchsvg.h/.cpp`: `svgFromDocument` / `documentFromSvg` (own small XML parser; no library). One `<path>` per object with `data-type/name/frame-angle/locked`,
  `display="none"` for hidden, CMYK/Gray/HSB colors as RGB display attr + `data-fill-color="cmyk c m y k"` (same for stroke / `<stop data-color>`),
  gradients as userSpaceOnUse `<linearGradient>/<radialGradient>` (+ `data-angle`, `spreadMethod`). Reader also takes foreign SVG: path (all commands + arcs),
  rect/circle/ellipse/line/polyline/polygon, `<g>`, `transform`, style="", viewBox scaling, named/#rgb/rgb() colors, href'd gradient stops. Text/images/clip/filters skipped.
- `SketchDocument::readFromFile/writeToFile` use it (so `Document::load/save` give dirty tracking and the `.bak` backup). `SketchObject::make(type, path)` added.
- main.cpp: File > New/Open/Save/Save As/Exit with "save changes?" prompt, Ctrl+N/O/S/Shift+S (via `CanvasView::onControlKey`), title "sketch - name *", `sketch.exe file.svg` opens.
- 8 SVG tests in `sketch_tests` (72 total). Closing the window with the X prompts too: `Frame::onClosing(Frame&, bool& canClose)` (new, in frame.h/frame.cpp) vetoes WM_CLOSE; the File commands live in newui's `DocumentController` (saveDocument/saveDocumentAs/openInto/newInto + virtual UI hooks askUnsavedChanges/chooseSavePath/chooseOpenPath/reportError, unit-tested); sketch's `SketchDocumentController` (sketchcontroller.h/.cpp) overrides the hooks (SVG filters, owner window) and fires onStatus/onStateChanged/onDocumentReplaced. Gaps: File > New keeps the page size
  (no New Document dialog logic yet); Open Recent, Export, Document Setup still inert.

## Next
1. Try opening SVGs exported from Inkscape/browsers and note what is skipped.
2. Gradient tool on canvas (drag start/end, radial center/radius), Gray/CMYK/HSB color modes (mockup needs a 5th slider row first).
3. New Document dialog logic; S5 pen/text; S6 export/PDF; S7 clipboard/snapping.
4. manyrows M0-M6 after sketch.

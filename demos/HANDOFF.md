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
- **Gradient tool (G palette button; builds, 92 model tests + `--check-tools` drag checks pass; NOT yet tried by hand):** `SketchGradient` gained an optional explicit axis (`hasAxis`, `axisX1..axisY2` = two points as fractions of the object's bbox; `axisPoints/setAxis/setAngle`). Linear: start/end; radial: center + a point on the circle. Without it the axis is derived from `angleDegrees` as before. Paint, SVG writer and the canvas all go through `axisPoints`. Canvas (`CanvasView::beginGradientDrag` etc.): drag across the selection (or a clicked object) lays an axis (a solid fill becomes linear; an existing gradient keeps its kind/stops; Shift = 45 deg), one undo step; with a single gradient-filled object selected the tool shows the axis (circle = start/center, square = end/radius point, diamonds = interior stops) - drag the ends or stops, click the axis to add a stop, Alt-click a stop to delete (min 2 stops). Panel Angle field turns an explicit axis about its midpoint (several objects selected: drops the axis). SVG: `data-axis` (box fractions) next to the userSpaceOnUse x1/y1/x2/y2 or cx/cy/r; foreign gradient vectors (bbox or user-space units) are now kept as an axis instead of just an angle. Gaps closed afterwards: (1) the tool edits the **stroke** when the Color panel's target is Stroke (`CanvasView::setGradientOnStroke`, wired from `StylePanel::onTargetChanged`); (2) the selected stop is shared both ways (`CanvasView::onGradientStopSelected/setSelectedGradientStop` <-> `StylePanel::onStopSelected/setSelectedStop`; the selected stop gets a heavy outline); (3) **elliptical radial gradients**: `SketchGradient::axisAspect` (cross radius / along radius, drawn via `ellipseMatrix` as a gradient transform), a diamond aspect handle across the axis on the canvas (Shift on it resets to a circle), written as `gradientTransform` + `data-aspect`; (4) `gradientTransform` is read for foreign gradients (any matrix: center/axis points are mapped, radial aspect = cross/along lengths, shear ignored); (5) per-tool cursors (crosshair for draw/gradient/pen/pencil/zoom, I-beam for text, hand for hand) - there is no magnifier cursor, and Select shows no resize cursors on its handles (the frame can be rotated); (6) the mockup shows a stop diamond.
- Not done: gradient strokes that follow the stroke direction (a stroke gradient is a plain bbox gradient like Illustrator's default), shear in foreign gradient transforms.
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

## Color modes (done; not seen on screen yet)
- `SketchColor::convertedTo(space)` / `toRgb` (naive CMYK, luminance gray, standard HSB). Document mode: `SketchDocument::colorMode()` (RGB|CMYK only),
  `setColorMode` (loading/new doc: sets the mode + converts the defaults, not undoable), `convertToColorMode` (View > Color Mode: converts every
  object/gradient stop/default, one undo step). SVG carries `data-color-mode` on the root.
- Color panel: four component sliders `comp0..comp3Slider` + `alphaSlider`; captions/ranges/row visibility follow the edited color's own space
  (RGB 0-255 R/G/B; CMYK 0-100 C/M/Y/K; Gray = K ink coverage 0-100; HSB 0-360/100/100). The Gray/RGB/CMYK/HSB buttons convert the edited color
  (undoable); edits stay in the color's space; hex converts back into it; swatches apply in the document mode.
- Status bar: the pane before the zoom pane shows "RGB doc"/"CMYK doc"; View > Color Mode checks follow `colorMode()` (also after undo/open).
- Found + fixed in newui: `SegmentedControl` segments never loaded from `.newui` (plain vector property is skipped by the reader) - now a collection
  (`addSegment/removeSegment`); the generator emits `segments` before `selectedIndex`. The New Document dialog's segmented controls benefit too.
- Unverified visually: hiding `comp3SliderRow` (RGB/HSB/Gray) should collapse the row - check the panel does not leave a gap or mis-scroll.
- Not done: Gray/HSB are not document modes (only per-color), no ICC, New Document dialog does not set the mode yet (needs its own logic).
- `--check-tools` now creates its own objects (the demo scene is gone).

- newui `DialogPosition` (CenterOnRootView default / CenterOnFocus / CenterOnMouse): `Dialog::showMessageBox(..., position)` (CBT hook moves the native box) and `Dialog::setPosition()` / `"position"` in a dialog's `.newui` (Dialog is now `@reflect`ed; `Bundle::loadDialog` reads it as a Dialog). Native file pickers ignore it. The New Document dialog should be a `Dialog` loaded with `loadDialog` when its logic is built.

## New Document dialog (done; not seen on screen yet)
- `newdocumentmodel.h/.cpp`: pure logic (presets, unit conversion keeping physical size, px follows ppi, orientation, keep proportions, summary text), unit-tested.
  `PRESETS` in gen_newui.py and `kPresets` in newdocumentmodel.cpp must stay in step.
- `newdocumentdialog.h/.cpp`: loads Resources/newdocument.newui into a `newui::Dialog` (position CenterOnRootView from the file), mirrors the model into the controls;
  `NewDocumentDialog::run()` is modal, `check()` is the headless self-test (`sketch.exe --check-newdoc`, 22 checks). Text boxes commit on Enter / focus loss / Create (no live update while typing).
- File > New / Ctrl+N: `SketchDocumentController::newDocument()` asks to save, shows the dialog, then clears and applies page size, ppi, color mode (`setColorMode`) and background;
  the name (Untitled-1) shows in the title / save prompt / Save As suggestion until the first save.
- `SketchDocument::pageBackground()` (white default, nullopt = transparent -> checkerboard on the canvas); SVG: `data-page-background` + a `<rect id="pageBackground">` the reader skips.
  The canvas now follows the document's page size (it only read it at attach time before - Open of a different-size file used to keep the old page).
- Not done: no Document Setup dialog (change size later), no remembering the last-used settings, no live preview while typing.

## Next
1. Try File > New (choose categories, units, orientation, link, color mode, transparent background), Create, then Save/Open.
2. S5 pen/text; S6 export/PDF (CMYK operators); S7 clipboard/snapping; Document Setup.
3. manyrows M0-M6 after sketch.

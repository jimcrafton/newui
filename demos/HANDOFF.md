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
- Not wired: every button/menu, New Document logic (categories, unit conversion, summary), SVG open.

## Build / run
```powershell
cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" && cd /d D:\code\newui-demos && cmake --build build --config Debug --target sketch > build_log.txt 2>&1'
build\demos\sketch\Debug\sketch.exe --check
build\unittests\Debug\newui_tests.exe        # run twice
```
Needs `tools/reflectgen/.venv` (created; gitignored). Resources are copied next to the exe at build time (rebuild to pick up icon/.newui changes).

## Gotchas learned
- `.newui`: the reader merges file element *i* of a collection into the existing child *i*. A `ScrollView`'s `childViews[0]` is its internal viewport
  (content nests inside it). Put `bounds` before `layout`/`childViews`. `ToolbarSeparator.horizontal` must match the toolbar and be read before
  `desiredSize`. A `SegmentedControl` in a centering row needs an explicit width. Bare `SubView` needs `visible: true`.
- Bash tool chokes on some heredocs (apostrophes); write scripts with the Write tool. In C++ edits via Python, `\n` inside printf strings gets mangled -
  use Edit.
- IDE clang diagnostics on these files are include-path noise; the MSBuild result is authoritative.

## Next
1. S1: `SketchDocument` + objects + `SketchColor`/`SketchPaint`/appearance model, canvas rendering, zoom/pan.
2. New Document dialog logic + File > New; then S2 (tools, selection, undo/redo) per PLAN.md.
3. manyrows M0-M6 after sketch.

# manyrows

Million-row spreadsheet-style table demo built on newui.
See [../PLAN.md](../PLAN.md) for the design and phases and [../HANDOFF.md](../HANDOFF.md) for the current state.

## Two apps

| App | What it is | Shows |
|---|---|---|
| `manyrows_lite.exe` | A bare `TimedTableView` over 1,000,000 generated rows and a status bar. No menus, no files. | How little it takes to put a virtualized table on screen. |
| `manyrows.exe [file]` | The full app: menus, formula bar, File New / Open / Save / Save As (CSV, TSV, XLS, XLSX), async load and save with progress, sheet picker. | A document-based app on `DocumentController`. |

Both sort and filter as a view over the rows (nothing moves; a million rows sort in ~0.3 s in Release): clicking a column header sorts by it (ascending, descending, unsorted; Shift/Ctrl-click only selects), Alt+Up / Alt+Down sort by the active cell's column, Ctrl+Shift+F filters to its value (repeat on other columns to narrow, again to switch off), Alt+Backspace resets; the full app has these in its Data menu, plus Data > Filter... (Ctrl+L), a strip for building a filter rule by rule (column, operator, value; rules are ANDed).

Both edit cells in place, with undo / redo, cut / copy / paste (tab-separated text, so it moves to and from
Excel), fill down / right and Delete, all from the keyboard (F2 or typing to edit; Ctrl+Z / Y / X / C / V / D / R).
`manyrows.exe --check` and `manyrows_lite.exe --check` load, lay out and print the model shape (smoke test).

## Layers (and CMake targets)

```
manyrows_core    the table - no files
  manyrowsmodel.*          ManyRowsModel : TableModel. 1,000,000 x 10 cells generated from a seeded hash (nothing
                           stored), or a loaded file (compact string arena), plus a sparse edit overlay.
  timedtableview.*         TimedTableView : TableView. Times its own paint() (last / smoothed / worst ms), knows the
                           visible rows, edits cells in place through a commit handler.
  cellitem.*               CellItem : TableItem, column-aware cell typography.
  spreadsheeteditor.*      SpreadsheetEditor: every cell change (set, clear, paste, fill) as one undo step that
                           stores only the cells it touched.
  spreadsheetcontroller.*  SpreadsheetController: wires the editor to the table - commit and validation, undo / redo,
                           clipboard, fill, shortcuts, formula bar, status panes, Edit / View menus. Needs only
                           (table, model); the formula bar, status bar and menus are optional parts.
  rowindex.*, filterrule.*, tablequery.*   the sort/filter view: display-row -> stored-row index, filter conditions, and the sort/filter algorithms.
  tablecliptext.*          TableClipText: cells <-> clipboard text. menubinding.h, textformat.h: small helpers.

manyrows_files   the file side
  io/                      the formats - see io/README.md
  manyrowsloader.*         reads a file on a worker thread into a staging model (progress, cancel, sheet choice)
  manyrowssaver.*          writes a file on a worker thread to a sibling temp file, then swaps it in
  manyrowsdocument.*       ManyRowsDocument : newui::Document (path, modified flag, load / save)
  manyrowscontroller.*     ManyRowsController : newui::DocumentController (dialogs, "save changes?", busy state)

manyrows         core + files
  manyrowswindow.*         ManyRowsWindow: the only class that knows both controllers. Connects them through their
                           delegates (an edit marks the document modified, a replaced document resets undo, a running
                           load locks editing) and binds the title, File menu, strips and closing.
  filterstrip.*            FilterStrip: the Data > Filter... strip (rules ANDed; works through SpreadsheetController).
  main.cpp                 loads the frame, builds the window, runs.

manyrows_lite    core only
  lite/main.cpp            loads the frame, attaches a SpreadsheetController to the bare table, runs.
```

The split is the design point: `SpreadsheetController` never mentions a file and `ManyRowsController` never
mentions a cell, so the lite app is the first one plus a status bar.

## Windows (Resources/)

- `manyrows.newui` - menu bar, formula bar, table, load strip, sheet-picker strip, status bar.
- `manyrows_lite.newui` - table and status bar only.
- Status bar panes, in order: message, paint ms, visible rows, selection, total.
- `mockup/` - HTML design sources (press N in the browser to see newui control names): `manyrows-mockup.html`
  (full), `manyrows-lite-mockup.html`, `edit-menu-m4.html` (Cut / Copy / Paste / Fill).

## Tests

`tests/` builds `manyrows_tests.exe` (128 tests, headless): model, editor (edit, clear, paste, fill, saved state),
sort / filter, clipboard text, CSV / XLS / XLSX readers, document, loader (including the sheet picker) and saver. SpreadsheetController is tested on a real TimedTableView with no window (keys, header clicks, sort, filter, Go to, formula bar, edit guard, document replaced). ManyRowsWindow, FilterStrip and the toolbar need a laid-out frame and are checked by launching the apps. Clipboard commands are not tested through the controller (they would overwrite the system clipboard); the editor tests cover paste and fill. `XlsxFormatPerf` times any file named
in the `MANYROWS_PERF_XLSX` environment variable (skipped otherwise).

## Status

Done: M0 + M1 (model, virtual table), cell typography, editing + undo, CSV / XLS / XLSX io with File commands, async
load and save, clipboard + fill (M4; column resize comes from `TableView` itself), the controller split and the lite
app. Sort / filter (M5) is in `SpreadsheetController`, so both apps have it. Next: a filter strip for custom conditions, perf pass (M6). Limits: XLS and XLSX
are read-only (Save As .csv); no Open Recent; colors follow the light theme only.

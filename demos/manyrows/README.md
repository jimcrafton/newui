# manyrows

Million-row spreadsheet-style table demo built on newui.
See [../PLAN.md](../PLAN.md) for the design and phases.

- `manyrowsmodel.*` - `ManyRowsModel : TableModel`, 1,000,000 x 10 cells generated from a seeded hash (nothing stored) plus a sparse edit overlay.
- `timedtableview.*` - `TimedTableView : TableView`, times its own `paint()` (last / smoothed / worst ms) and knows the visible row range.
- `Resources/manyrows.newui` - window: menu bar, `TimedTableView` in a `ScrollView`, status bar (message, paint ms, visible rows, selection, total).
- `cellitem.*` - `CellItem : TableItem`, column-aware cell typography.
- `mockup/manyrows-mockup.html` - design source of truth for the window (press N for newui names).
- `tests/test_manyrows_model.cpp` - `manyrows_tests.exe` (7 model tests).
- `manyrows.exe --check [--depth=N]` loads, lays out and prints the tree (smoke test).

Status: M0 + M1, cell typography, editing + undo, CSV io and File New/Open/Save/Save As done (see HANDOFF.md). Next: XLS, clipboard/fill/resize, sort/filter, perf.

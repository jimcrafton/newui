# manyrows/io

File-format support for manyrows: one reader/writer pair per format, all behind `TableFormat` (`tableformat.h`).

Done: `csvformat.*` (`CsvFormat`, .csv / .tsv; RFC 4180 reader that sniffs delimiter / header / column count
and estimates rows from the first 64 KB, streaming in 256 KB chunks; writer quotes only when needed).
Planned: `xlsformat.*` (.xls, then .xlsx). Nothing here is wired into the app yet - File > Open / Save As
will pick a format from the file extension via a registry.

Reading is two-phase: `TableFormat::open(path)` reads the file's initial data (header block, first few KB)
and returns a `TableReader` whose `info()` (`TableInfo`) says what the format could determine up front -
row / column count with a `Certainty` (Unknown for a plain CSV, Estimate from file size, Exact when stored,
as in XLS), header row and titles, sheets, and metadata (delimiter, encoding, author...). The app uses that
to size the table and ask about anything ambiguous, then calls `readRows()` to stream the rows.

Conventions: one class per header/source pair in namespace `manyrows::io`; add the sources to
`demos/manyrows/CMakeLists.txt` (and to `manyrows_tests` if they are model-only); include as `"io/csvformat.h"`.
A million-row file must stream - readers hand rows to a callback, writers pull rows from the model; never
build the whole table in memory.

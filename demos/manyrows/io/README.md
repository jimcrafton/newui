# manyrows/io

File-format support for manyrows: one class per format, all behind `TableFormat` (`tableformat.h`) and found by
file extension through `FormatRegistry` (`formatregistry.*`; `extensions(writableOnly)` feeds the Open / Save As
dialogs). Compiled into the `manyrows_files` library.

| Format | Files | Extensions | Read | Write |
|---|---|---|---|---|
| CSV / TSV | `csvformat.*` | .csv .tsv | yes | yes |
| Excel 97-2003 | `xlsformat.*` on `compoundfile.*` | .xls | yes | no |
| Excel workbook | `xlsxformat.*` on `zipfile.*` + `xmlparser.*` | .xlsx .xlsm | yes | no |

`rowassembler.h` is shared by the Excel readers: it turns cells that arrive one at a time (any order within a row,
gaps allowed) into the rows `readRows()` hands out, and sniffs a header row (text above numbers).

## Reading

Reading is two-phase: `TableFormat::open(path)` reads the file's initial data (header block, first few KB) and
returns a `TableReader` whose `info()` (`TableInfo`) says what the format could determine up front - row / column
count with a `Certainty` (Unknown for a plain CSV, Estimate from file size, Exact when stored, as in XLS and XLSX),
header row and titles, sheets, and metadata (delimiter, encoding...). The app uses that to size the table and ask
about anything ambiguous (several sheets: the sheet picker), then calls `readRows()` to stream the rows.

- **CSV** - RFC 4180 reader that sniffs delimiter / header / column count and estimates rows from the first 64 KB,
  streaming in 256 KB chunks. The writer quotes only when needed.
- **XLS** - `CompoundFile` reads the OLE2 container (FAT, mini FAT, DIFAT; bounded chain walks); `XlsFormat` reads BIFF8:
  sheets from BOUNDSHEET, exact row / column counts from DIMENSIONS, the shared string table across CONTINUE records,
  numbers, booleans and errors, and date detection from XF + FORMAT records (1900 and 1904 date systems).
- **XLSX** - `ZipFile` reads the central directory and streams stored or deflated entries in 64 KB pieces through the
  vendored zlib inflate (`3rdparty/zlib`, see its README); ZIP64 and encrypted archives are refused. `XmlParser` is a
  push parser (namespace prefixes stripped, entities, CDATA). `XlsxFormat` reads workbook.xml and its relationships,
  shared strings (rich text; phonetic runs skipped; `_xHHHH_` escapes), styles for dates, and streams the sheet
  without ever holding it whole; the row count comes from `<dimension>` or one counting pass. A password-protected
  workbook (Excel stores it as an OLE2 file) is reported as such.

Excel numbers and dates are rendered by the static helpers `XlsFormat::numberText/dateText/isDateFormat`, which
`XlsxFormat` reuses so both show the same text for the same cell.

## Conventions

One class per header/source pair in namespace `manyrows::io`; add the sources to the `manyrows_files` library in
`demos/manyrows/CMakeLists.txt`; include as `"io/csvformat.h"`. A million-row file must stream - readers hand rows
to a callback, writers pull rows from the model; never build the whole table in memory. Every non-reflected type
under `demos/manyrows` carries `// @reflect ignore=true` (reflectgen scans the whole directory). Tests build real
file bytes in memory (`tests/test_xls_format.cpp`) or embed a small workbook (`tests/xlsx_sample.h`) - no fixtures on disk.

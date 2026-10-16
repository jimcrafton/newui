#pragma once

#include "io/tableformat.h"

namespace manyrows {
    namespace io {

        // Excel 2007+ workbooks (".xlsx", and macro-enabled ".xlsm"): a ZIP archive of XML parts. Read-only.
        //
        // open() lists the archive, reads the sheet list (workbook.xml and its relationships), the shared
        // string table and the cell styles (so a number in a date format shows as a date), then reads the first
        // sheet's <dimension> - an exact row and column count before any row is read - and its first rows to
        // decide whether row one is a header (every cell text, while a column below holds numbers). A sheet
        // with no <dimension> is scanned once to count them.
        //
        // The worksheet XML is decompressed and parsed as a stream (zlib's inflate into a push parser), so
        // memory use is the shared strings plus one row, however big the sheet. Cell text follows XlsFormat:
        // strings as they are, numbers in their shortest exact form, date-formatted numbers as
        // yyyy-mm-dd[ hh:mm:ss], booleans TRUE/FALSE, errors as written (#DIV/0!), formulas as their cached
        // result. Several sheets are listed in TableInfo::sheets and selectSheet() switches.
        //
        // Not supported (open() throws a clear std::runtime_error): password-protected workbooks, ZIP64
        // archives, and files that are not Excel workbooks. Writing is not supported (canWrite() is false).
        // @reflect ignore=true
        class XlsxFormat : public TableFormat {
        public:
            std::string name() const override { return "Excel"; }
            std::vector<std::string> extensions() const override { return {".xlsx", ".xlsm"}; }
            bool recognizes(const std::string& path) const override;
            std::unique_ptr<TableReader> open(const std::string& path) const override;
            bool canWrite() const override { return false; }
            void write(const std::string& path, std::size_t rows, std::size_t columns, const CellSource& cell) const override;

            // Excel stores some characters in text as _xHHHH_ (e.g. "_x000D_" for a carriage return); this
            // turns them back. Exposed for tests.
            static std::string decodeEscapes(const std::string& text);
            // "AB12" -> column 27, row 11 (both zero based). False for text that is not a cell reference.
            static bool parseReference(const std::string& reference, std::size_t& column, std::size_t& row);
        };

    }
}

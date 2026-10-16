#pragma once

#include "io/tableformat.h"

namespace manyrows {
    namespace io {

        // Excel 97-2003 workbooks (".xls": BIFF8 records inside a compound file). Read-only.
        //
        // open() loads the "Workbook" stream and reads the workbook-global records: the sheet list, the
        // shared string table, the cell formats (so a number in a date format shows as a date), the date
        // system. For the first worksheet it then reads the DIMENSIONS record - which is why this format can
        // report an EXACT row and column count before any row is read - and the first rows, to decide whether
        // row one is a header (every cell text, while a column below it holds numbers).
        //
        // Rows stream through readRows() in file order; rows with no cells come out empty so numbering matches
        // the sheet. Cell text: strings as they are, numbers in their shortest exact form (15 significant
        // digits), date-formatted numbers as yyyy-mm-dd (with hh:mm:ss when there is a time), booleans as
        // TRUE/FALSE, errors as #DIV/0! and so on, formulas as their last calculated value.
        //
        // Not supported (open() throws a clear std::runtime_error): password-protected workbooks and Excel 5/95
        // (BIFF5) files. Only the first worksheet is read; the others are listed in TableInfo::sheets and
        // selectSheet() switches. Writing is not supported (canWrite() is false).
        // @reflect ignore=true
        class XlsFormat : public TableFormat {
        public:
            std::string name() const override { return "Excel 97-2003"; }
            std::vector<std::string> extensions() const override { return {".xls"}; }
            bool recognizes(const std::string& path) const override;
            std::unique_ptr<TableReader> open(const std::string& path) const override;
            bool canWrite() const override { return false; }
            void write(const std::string& path, std::size_t rows, std::size_t columns, const CellSource& cell) const override;

            // How a number is shown: the shortest exact text (integers without a fraction, otherwise up to 15
            // significant digits). Exposed for tests.
            static std::string numberText(double value);
            // An Excel serial date as text: "yyyy-mm-dd", "yyyy-mm-dd hh:mm:ss", or "hh:mm:ss" for a time of day.
            static std::string dateText(double serial, bool date1904);
            // True for the number format ids Excel defines as dates/times (14-22, 27-36, 45-47, 50-58).
            static bool isBuiltInDateFormat(unsigned id);
            // True when an Excel number format string shows a date or time ("yyyy-mm-dd", "h:mm", not "0.00").
            static bool isDateFormat(const std::string& format);
        };

    }
}

#pragma once

#include "io/tableformat.h"

namespace manyrows {
    namespace io {

        // Comma-separated values (also tab/semicolon/pipe-separated: the delimiter is sniffed on read).
        //
        // Reading follows RFC 4180 with the usual leniencies: fields may be quoted with "..." (a doubled ""
        // is a literal quote, and a quoted field may contain delimiters and line breaks); records end at
        // CRLF, LF or a lone CR; a completely empty line is skipped; a quote in the middle of an unquoted
        // field is literal; a missing final line break is fine; an unterminated quote runs to the end of the
        // file. Text is passed through as UTF-8 (a UTF-8 BOM is skipped); UTF-16 files are refused.
        //
        // open() reads the first 64 KB and reports in TableInfo: the delimiter, the column count (the most
        // common field count among the first records), whether the first row looks like a header (all text,
        // while a column below it is numeric) and its titles, line endings, encoding, and the row count -
        // Exact when the whole file fit in that block, otherwise an Estimate from the file size and the
        // sampled average record length. Rows then stream through readRows() in 256 KB chunks.
        //
        // Writing quotes only fields that need it (contain the delimiter, a quote, CR/LF, or start/end with
        // a space) and ends records with CRLF by default.
        // @reflect ignore=true
        class CsvFormat : public TableFormat {
        public:
            // @reflect ignore=true
            struct Options {
                // Writing: the delimiter. 0 = from the file name (".tsv" tab, otherwise comma).
                // Reading ignores this and sniffs (unless forceDelimiter is set).
                char delimiter = 0;
                // Reading: use `delimiter` instead of sniffing it.
                bool forceDelimiter = false;
                // Writing: put a UTF-8 BOM first (Excel needs one to read non-ASCII CSV as UTF-8).
                bool writeBom = false;
                // Writing: record terminator.
                std::string lineEnding = "\r\n";
            };

            CsvFormat() = default;
            explicit CsvFormat(const Options& options) : options_(options) {}

            std::string name() const override { return "CSV"; }
            std::vector<std::string> extensions() const override { return {".csv", ".tsv"}; }
            bool recognizes(const std::string& path) const override;
            std::unique_ptr<TableReader> open(const std::string& path) const override;
            void write(const std::string& path, std::size_t rows, std::size_t columns, const CellSource& cell) const override;

            // Quotes `field` for the delimiter when needed - exposed for tests and other writers.
            static std::string escapeField(const std::string& field, char delimiter);

        private:
            Options options_;
        };

    }
}

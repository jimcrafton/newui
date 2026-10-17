#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace manyrows {
    namespace io {

        // How sure a format is about a number it reports before reading the rows.
        // @reflect ignore=true
        enum class Certainty {
            Unknown,    // the format cannot say (a plain CSV has no row count)
            Estimate,   // a guess from what was sniffed (file size / average row length)
            Exact       // stored in the file (an XLS dimension record, a header field)
        };

        // @reflect ignore=true
        template <typename T>
        struct Reported {
            std::optional<T> value;
            Certainty certainty = Certainty::Unknown;
        };

        // What a reader learned from the file's initial data - before any rows are read - so the app can
        // size the table, show titles and a progress bar, and ask the user about anything ambiguous
        // (delimiter, encoding, which sheet) first. Fields a format cannot determine stay Unknown/empty.
        // @reflect ignore=true
        struct TableInfo {
            Reported<std::size_t> rowCount;      // including the header row when hasHeaderRow
            Reported<std::size_t> columnCount;
            bool hasHeaderRow = false;           // first row holds column titles (sniffed for CSV, stored for XLS)
            std::vector<std::string> columnTitles;  // from the header row, when known
            // Anything else worth showing, in file order: "delimiter" -> ",", "encoding" -> "UTF-8",
            // "sheet" -> "Orders", "author" -> ..., "created" -> ... Keys are lower case.
            std::vector<std::pair<std::string, std::string>> metadata;
            // Sheets / tables inside the file, when it holds several (XLS). Empty for single-table formats.
            std::vector<std::string> sheets;
            std::size_t fileBytes = 0;
        };

        // An open file being read. Created by TableFormat::open(), which has already read the initial data
        // (header block / first few KB) so info() is available immediately; rows are then streamed.
        //
        // Streaming on purpose - manyrows tables hold a million rows, so rows go to a callback as they are
        // parsed and nothing holds the whole table. Implementations throw std::runtime_error
        // ("Class::method: ...") for unreadable / corrupt files.
        // @reflect ignore=true
        class TableReader {
        public:
            virtual ~TableReader() = default;

            // Valid from construction. May be refined (an Estimate becoming Exact) as rows are read.
            virtual const TableInfo& info() const = 0;

            // Which sheet readRows() reads, for formats with several (default: the first). Throws for an
            // index past info().sheets.
            virtual void selectSheet(std::size_t index) {}

            // Called once per row, in file order; `cells` is only valid during the call. Return false to
            // stop. The header row is passed too (see info().hasHeaderRow) so row numbers match the file.
            typedef std::function<bool(const std::vector<std::string>& cells)> RowCallback;
            // Called now and then with the bytes consumed so far, for a progress bar; return false to cancel.
            typedef std::function<bool(std::size_t bytesRead, std::size_t totalBytes)> ProgressCallback;

            // Reads from the start of the selected sheet. Returns the number of rows delivered. The
            // callbacks run on the calling thread - a caller wanting a responsive UI runs this on a worker.
            virtual std::size_t readRows(const RowCallback& onRow, const ProgressCallback& onProgress = nullptr) = 0;
        };

        // One file format (CSV, XLS, ...): a factory for readers and a writer.
        // @reflect ignore=true
        class TableFormat {
        public:
            virtual ~TableFormat() = default;

            // Short name for dialogs ("CSV"), and the extensions it handles, lower case with the dot (".csv").
            virtual std::string name() const = 0;
            virtual std::vector<std::string> extensions() const = 0;

            // True when the file's own bytes look like this format (a signature, or a successful sniff of the
            // first block) - lets the app pick a format for a file whose extension lies.
            virtual bool recognizes(const std::string& path) const = 0;

            // Opens `path` and reads its initial data (see TableReader). Never returns null; throws
            // std::runtime_error when the file cannot be opened or is not this format.
            virtual std::unique_ptr<TableReader> open(const std::string& path) const = 0;

            virtual bool canWrite() const { return true; }
            // Supplies cell text: write() asks for (row, column) in row-major order.
            typedef std::function<std::string(std::size_t row, std::size_t column)> CellSource;
            virtual void write(const std::string& path, std::size_t rows, std::size_t columns, const CellSource& cell) const = 0;
        };

    }
}

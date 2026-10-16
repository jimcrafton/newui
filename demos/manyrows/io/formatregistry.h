#pragma once

#include "io/tableformat.h"

#include <string>
#include <vector>

namespace manyrows {
    namespace io {

        // Every file format manyrows can read or write, in one place - File > Open / Save As ask it which
        // format handles a path. A new format is added to the list in formatregistry.cpp.
        // @reflect ignore=true
        class FormatRegistry {
        public:
            static const std::vector<const TableFormat*>& formats();

            // The format for a path: the one claiming its extension (case-insensitive), else - for an
            // existing file with an unknown extension - the first whose recognizes() accepts it, else nullptr.
            static const TableFormat* forPath(const std::string& path);

            // Every extension of every format with the dot, e.g. {".csv", ".tsv", ".xls"} - for file dialog
            // filters. `writableOnly` leaves out the read-only formats (Save As lists only those that can save).
            static std::vector<std::string> extensions(bool writableOnly = false);
        };

    }
}

#include "io/formatregistry.h"

#include "io/csvformat.h"
#include "io/xlsformat.h"
#include "io/xlsxformat.h"

#include <newui/utils.h>

#include <cctype>
#include <filesystem>

namespace manyrows {
    namespace io {

        const std::vector<const TableFormat*>& FormatRegistry::formats() {
            static const CsvFormat csv;
            static const XlsFormat xls;
            static const XlsxFormat xlsx;
            static const std::vector<const TableFormat*> all = {&csv, &xls, &xlsx};
            return all;
        }

        const TableFormat* FormatRegistry::forPath(const std::string& path) {
            std::string extension = std::filesystem::path(newui::utf8ToWide(path)).extension().string();
            for (char& c : extension) {
                c = char(std::tolower((unsigned char)c));
            }
            for (const TableFormat* format : formats()) {
                for (const std::string& candidate : format->extensions()) {
                    if (candidate == extension) {
                        return format;
                    }
                }
            }
            std::error_code ignored;
            if (std::filesystem::exists(std::filesystem::path(newui::utf8ToWide(path)), ignored)) {
                for (const TableFormat* format : formats()) {
                    if (format->recognizes(path)) {
                        return format;
                    }
                }
            }
            return nullptr;
        }

        std::vector<std::string> FormatRegistry::extensions(bool writableOnly) {
            std::vector<std::string> all;
            for (const TableFormat* format : formats()) {
                if (writableOnly && !format->canWrite()) {
                    continue;
                }
                for (const std::string& extension : format->extensions()) {
                    all.push_back(extension);
                }
            }
            return all;
        }

    }
}

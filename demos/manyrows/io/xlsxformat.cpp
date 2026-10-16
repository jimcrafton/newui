#include "io/xlsxformat.h"

#include "io/compoundfile.h"
#include "io/rowassembler.h"
#include "io/xlsformat.h"
#include "io/xmlparser.h"
#include "io/zipfile.h"

#include <newui/utils.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace manyrows {
    namespace io {

        namespace {
            constexpr std::size_t kHeaderSniffRows = 21;
            constexpr std::size_t kProgressEvery = 256 * 1024;   // uncompressed bytes between progress callbacks

            bool endsWith(const std::string& text, const char* suffix) {
                std::size_t n = std::char_traits<char>::length(suffix);
                return text.size() >= n && text.compare(text.size() - n, n, suffix) == 0;
            }

            void appendUtf8(std::string& out, unsigned cp) {
                if (cp < 0x80) {
                    out += char(cp);
                } else if (cp < 0x800) {
                    out += char(0xC0 | (cp >> 6));
                    out += char(0x80 | (cp & 0x3F));
                } else {
                    out += char(0xE0 | (cp >> 12));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                }
            }

            // A path inside the archive from a relationship target ("worksheets/sheet1.xml" is relative to xl/).
            std::string partPath(const std::string& target) {
                if (!target.empty() && target[0] == '/') {
                    return target.substr(1);
                }
                std::string path = "xl/" + target;
                while (path.compare(0, 6, "xl/../") == 0) {
                    path = path.substr(6);
                }
                return path;
            }

            // @reflect ignore=true
            struct SheetRef {
                std::string name;
                std::string path;   // the part inside the archive
            };

            // @reflect ignore=true
            struct Dimension {
                bool known = false;
                std::size_t rows = 0;
                std::size_t columns = 0;
            };

            // @reflect ignore=true
            class XlsxReader : public TableReader {
            public:
                explicit XlsxReader(const std::string& path) : path_(path) {
                    if (CompoundFile::hasSignature(path)) {
                        throw std::runtime_error("XlsxFormat: the workbook is password-protected (or is an old .xls file with the wrong name)");
                    }
                    zip_ = std::make_unique<ZipFile>(path);
                    info_.fileBytes = fileSize(path);
                    if (zip_->find("xl/workbook.xml") == nullptr) {
                        throw std::runtime_error("XlsxFormat: the file holds no Excel workbook (xl/workbook.xml is missing)");
                    }
                    readWorkbook();
                    if (sheets_.empty()) {
                        throw std::runtime_error("XlsxFormat: the workbook has no worksheets");
                    }
                    for (const SheetRef& sheet : sheets_) {
                        info_.sheets.push_back(sheet.name);
                    }
                    info_.metadata.push_back({"format", "Excel (Open XML)"});
                    prepareSheet(0);
                }

                const TableInfo& info() const override { return info_; }

                void selectSheet(std::size_t index) override {
                    if (index >= sheets_.size()) {
                        throw std::runtime_error("XlsxReader::selectSheet: no sheet " + std::to_string(index));
                    }
                    prepareSheet(index);
                }

                std::size_t readRows(const RowCallback& onRow, const ProgressCallback& onProgress) override {
                    std::size_t delivered = 0;
                    RowAssembler rows([&](std::size_t, const std::vector<Cell>& cells) {
                        std::vector<std::string> texts;
                        texts.reserve(cells.size());
                        for (const Cell& cell : cells) {
                            texts.push_back(cell.text);
                        }
                        ++delivered;
                        return onRow(texts);
                    });
                    bool whole = scan(
                        sheets_[sheet_],
                        [&](std::size_t row, std::size_t column, std::string&& text, bool numeric) {
                            return rows.add(row, column, std::move(text), numeric);
                        },
                        onProgress ? &onProgress : nullptr, nullptr, false);
                    if (whole && rows.finish()) {
                        if (onProgress) {
                            onProgress(1, 1);
                        }
                        info_.rowCount.value = delivered;
                        info_.rowCount.certainty = Certainty::Exact;
                    }
                    return delivered;
                }

            private:
                typedef std::function<bool(std::size_t row, std::size_t column, std::string&& text, bool numeric)> Sink;

                static std::size_t fileSize(const std::string& path) {
                    std::error_code ignored;
                    return std::size_t(std::filesystem::file_size(std::filesystem::path(newui::utf8ToWide(path)), ignored));
                }

                // Streams a part through the parser. False when a callback (or `progress`) stopped it.
                bool runXml(const ZipFile::Entry& entry, XmlParser& parser, const ProgressCallback* progress, bool* cancelled = nullptr) const {
                    std::size_t sinceReport = 0;
                    return zip_->extract(entry, [&](const std::uint8_t* data, std::size_t size, std::size_t consumed) {
                        if (!parser.feed(data, size)) {
                            return false;
                        }
                        sinceReport += size;
                        if (progress != nullptr && *progress && sinceReport >= kProgressEvery) {
                            sinceReport = 0;
                            if (!(*progress)(consumed, entry.compressedSize)) {
                                if (cancelled != nullptr) {
                                    *cancelled = true;
                                }
                                return false;
                            }
                        }
                        return true;
                    });
                }

                const ZipFile::Entry* require(const std::string& name) const {
                    const ZipFile::Entry* entry = zip_->find(name);
                    if (entry == nullptr) {
                        throw std::runtime_error("XlsxFormat: the workbook is missing \"" + name + "\"");
                    }
                    return entry;
                }

                void readWorkbook() {
                    // 1) the relationships: which part is which
                    struct Relation {
                        std::string target;
                        std::string type;
                    };
                    std::unordered_map<std::string, Relation> relations;
                    if (const ZipFile::Entry* rels = zip_->find("xl/_rels/workbook.xml.rels")) {
                        XmlParser parser;
                        parser.onStart = [&](const std::string& name, const XmlParser::Attributes& attributes) {
                            if (name == "Relationship") {
                                relations[XmlParser::attribute(attributes, "Id")] = {XmlParser::attribute(attributes, "Target"),
                                    XmlParser::attribute(attributes, "Type")};
                            }
                            return true;
                        };
                        runXml(*rels, parser, nullptr);
                    }
                    std::string sharedPath = "xl/sharedStrings.xml";
                    std::string stylesPath = "xl/styles.xml";
                    for (const auto& item : relations) {
                        if (endsWith(item.second.type, "/sharedStrings")) {
                            sharedPath = partPath(item.second.target);
                        } else if (endsWith(item.second.type, "/styles")) {
                            stylesPath = partPath(item.second.target);
                        }
                    }

                    // 2) the sheet list and the date system
                    XmlParser parser;
                    parser.onStart = [&](const std::string& name, const XmlParser::Attributes& attributes) {
                        if (name == "workbookPr") {
                            const std::string& flag = XmlParser::attribute(attributes, "date1904");
                            date1904_ = flag == "1" || flag == "true";
                        } else if (name == "sheet") {
                            auto found = relations.find(XmlParser::attribute(attributes, "id"));
                            if (found != relations.end() && endsWith(found->second.type, "/worksheet")) {
                                sheets_.push_back({XmlParser::attribute(attributes, "name"), partPath(found->second.target)});
                            }
                        }
                        return true;
                    };
                    runXml(*require("xl/workbook.xml"), parser, nullptr);

                    if (const ZipFile::Entry* styles = zip_->find(stylesPath)) {
                        readStyles(*styles);
                    }
                    if (const ZipFile::Entry* shared = zip_->find(sharedPath)) {
                        readSharedStrings(*shared);
                    }
                }

                void readStyles(const ZipFile::Entry& entry) {
                    bool inCellXfs = false;
                    XmlParser parser;
                    parser.onStart = [&](const std::string& name, const XmlParser::Attributes& attributes) {
                        if (name == "numFmt") {
                            formats_[unsigned(std::strtoul(XmlParser::attribute(attributes, "numFmtId").c_str(), nullptr, 10))] =
                                XmlParser::attribute(attributes, "formatCode");
                        } else if (name == "cellXfs") {
                            inCellXfs = true;
                        } else if (name == "xf" && inCellXfs) {
                            xfFormat_.push_back(unsigned(std::strtoul(XmlParser::attribute(attributes, "numFmtId").c_str(), nullptr, 10)));
                        }
                        return true;
                    };
                    parser.onEnd = [&](const std::string& name) {
                        if (name == "cellXfs") {
                            inCellXfs = false;
                        }
                        return true;
                    };
                    runXml(entry, parser, nullptr);
                }

                void readSharedStrings(const ZipFile::Entry& entry) {
                    std::string current;
                    bool inItem = false;
                    bool inText = false;
                    int phonetic = 0;   // inside <rPh>: reading hints, not the text
                    XmlParser parser;
                    parser.onStart = [&](const std::string& name, const XmlParser::Attributes&) {
                        if (name == "si") {
                            inItem = true;
                            current.clear();
                        } else if (name == "rPh") {
                            ++phonetic;
                        } else if (name == "t" && inItem && phonetic == 0) {
                            inText = true;
                        }
                        return true;
                    };
                    parser.onText = [&](const std::string& text) {
                        if (inText) {
                            current += text;
                        }
                        return true;
                    };
                    parser.onEnd = [&](const std::string& name) {
                        if (name == "t") {
                            inText = false;
                        } else if (name == "rPh") {
                            --phonetic;
                        } else if (name == "si") {
                            inItem = false;
                            shared_.push_back(XlsxFormat::decodeEscapes(current));
                        }
                        return true;
                    };
                    runXml(entry, parser, nullptr);
                }

                bool isDateStyle(std::size_t style) const {
                    if (style >= xfFormat_.size()) {
                        return false;
                    }
                    unsigned id = xfFormat_[style];
                    if (XlsFormat::isBuiltInDateFormat(id)) {
                        return true;
                    }
                    auto found = formats_.find(id);
                    return found != formats_.end() && XlsFormat::isDateFormat(found->second);
                }

                // Reads a worksheet, handing every cell with a value to `sink`. `dimension` (optional) receives
                // the <dimension> element; with `dimensionOnly` the read ends where the cell data begins.
                // Returns true when the sheet was read to its end (or the dimension found), false when the
                // sink or `progress` stopped it.
                bool scan(const SheetRef& sheet, const Sink& sink, const ProgressCallback* progress, Dimension* dimension,
                    bool dimensionOnly) const {
                    const ZipFile::Entry* entry = zip_->find(sheet.path);
                    if (entry == nullptr) {
                        throw std::runtime_error("XlsxFormat: the sheet \"" + sheet.name + "\" has no part \"" + sheet.path + "\"");
                    }
                    std::size_t nextRow = 0;
                    std::size_t row = 0;
                    std::size_t nextColumn = 0;
                    std::size_t column = 0;
                    bool inCell = false;
                    bool inValue = false;
                    bool inInline = false;   // inside <is>
                    bool inText = false;
                    int phonetic = 0;
                    std::string type;
                    std::size_t style = 0;
                    std::string value;
                    std::string inlineText;
                    bool reachedEnd = false;
                    bool stopped = false;

                    auto emitCell = [&]() {
                        std::string text;
                        bool numeric = false;
                        if (type == "s") {
                            std::size_t index = std::size_t(std::strtoull(value.c_str(), nullptr, 10));
                            if (index < shared_.size()) {
                                text = shared_[index];
                            }
                        } else if (type == "str") {
                            text = XlsxFormat::decodeEscapes(value);
                        } else if (type == "inlineStr") {
                            text = XlsxFormat::decodeEscapes(inlineText);
                        } else if (type == "b") {
                            text = value == "1" ? "TRUE" : "FALSE";
                        } else if (type == "e" || type == "d") {
                            text = value;
                        } else if (!value.empty()) {
                            char* end = nullptr;
                            double number = std::strtod(value.c_str(), &end);
                            if (end == value.c_str() || *end != '\0') {
                                text = value;
                            } else if (isDateStyle(style)) {
                                text = XlsFormat::dateText(number, date1904_);
                            } else {
                                text = XlsFormat::numberText(number);
                                numeric = true;
                            }
                        }
                        if (text.empty()) {
                            return true;
                        }
                        if (!sink(row, column, std::move(text), numeric)) {
                            stopped = true;
                            return false;
                        }
                        return true;
                    };

                    XmlParser parser;
                    parser.onStart = [&](const std::string& name, const XmlParser::Attributes& attributes) {
                        if (name == "c") {
                            inCell = true;
                            std::size_t refColumn = 0;
                            std::size_t refRow = 0;
                            if (XlsxFormat::parseReference(XmlParser::attribute(attributes, "r"), refColumn, refRow)) {
                                column = refColumn;
                            } else {
                                column = nextColumn;
                            }
                            nextColumn = column + 1;
                            type = XmlParser::attribute(attributes, "t");
                            const std::string& s = XmlParser::attribute(attributes, "s");
                            style = s.empty() ? 0 : std::size_t(std::strtoull(s.c_str(), nullptr, 10));
                            value.clear();
                            inlineText.clear();
                        } else if (name == "v") {
                            inValue = true;
                        } else if (name == "is") {
                            inInline = true;
                        } else if (name == "rPh") {
                            ++phonetic;
                        } else if (name == "t" && inInline && phonetic == 0) {
                            inText = true;
                        } else if (name == "row") {
                            const std::string& r = XmlParser::attribute(attributes, "r");
                            row = r.empty() ? nextRow : std::size_t(std::strtoull(r.c_str(), nullptr, 10)) - (r == "0" ? 0 : 1);
                            nextRow = row + 1;
                            nextColumn = 0;
                        } else if (name == "dimension" && dimension != nullptr) {
                            const std::string& ref = XmlParser::attribute(attributes, "ref");
                            std::size_t colon = ref.find(':');
                            std::size_t lastColumn = 0;
                            std::size_t lastRow = 0;
                            if (XlsxFormat::parseReference(colon == std::string::npos ? ref : ref.substr(colon + 1), lastColumn, lastRow)) {
                                dimension->known = true;
                                dimension->rows = lastRow + 1;
                                dimension->columns = lastColumn + 1;
                            }
                        } else if (name == "sheetData" && dimensionOnly) {
                            reachedEnd = true;
                            return false;
                        }
                        return true;
                    };
                    parser.onText = [&](const std::string& text) {
                        if (inValue) {
                            value += text;
                        } else if (inText) {
                            inlineText += text;
                        }
                        return true;
                    };
                    parser.onEnd = [&](const std::string& name) {
                        if (name == "v") {
                            inValue = false;
                        } else if (name == "t") {
                            inText = false;
                        } else if (name == "is") {
                            inInline = false;
                        } else if (name == "rPh") {
                            --phonetic;
                        } else if (name == "c" && inCell) {
                            inCell = false;
                            return emitCell();
                        } else if (name == "sheetData") {
                            reachedEnd = true;
                            return false;   // nothing after the cell data matters
                        }
                        return true;
                    };
                    bool cancelled = false;
                    bool whole = runXml(*entry, parser, progress, &cancelled);
                    return (whole || reachedEnd) && !stopped && !cancelled;
                }

                void prepareSheet(std::size_t index) {
                    sheet_ = index;
                    const SheetRef& sheet = sheets_[index];
                    Dimension dimension;
                    scan(sheet, [](std::size_t, std::size_t, std::string&&, bool) { return true; }, nullptr, &dimension, true);
                    if (!dimension.known) {
                        // No <dimension>: count by reading the sheet once.
                        std::size_t maxRow = 0;
                        std::size_t maxColumn = 0;
                        bool any = false;
                        scan(sheet, [&](std::size_t row, std::size_t column, std::string&&, bool) {
                            any = true;
                            maxRow = row > maxRow ? row : maxRow;
                            maxColumn = column > maxColumn ? column : maxColumn;
                            return true;
                        }, nullptr, nullptr, false);
                        dimension.known = true;
                        dimension.rows = any ? maxRow + 1 : 0;
                        dimension.columns = any ? maxColumn + 1 : 0;
                    }
                    std::size_t columns = dimension.columns < kMaxSheetColumns ? dimension.columns : kMaxSheetColumns;
                    info_.rowCount = {dimension.rows, Certainty::Exact};
                    info_.columnCount = {columns, Certainty::Exact};
                    info_.hasHeaderRow = false;
                    info_.columnTitles.clear();
                    for (auto it = info_.metadata.begin(); it != info_.metadata.end();) {
                        it = it->first == "sheet" ? info_.metadata.erase(it) : it + 1;
                    }
                    info_.metadata.push_back({"sheet", sheet.name});

                    std::vector<std::vector<Cell>> top;
                    RowAssembler rows([&](std::size_t, const std::vector<Cell>& cells) {
                        top.push_back(cells);
                        return top.size() < kHeaderSniffRows;
                    });
                    bool whole = scan(sheet, [&](std::size_t row, std::size_t column, std::string&& text, bool numeric) {
                        return rows.add(row, column, std::move(text), numeric);
                    }, nullptr, nullptr, false);
                    if (whole) {
                        rows.finish();
                    }
                    info_.hasHeaderRow = sniffHeader(top, columns, info_.columnTitles);
                }

                std::string path_;
                std::unique_ptr<ZipFile> zip_;
                TableInfo info_;
                std::vector<SheetRef> sheets_;
                std::size_t sheet_ = 0;
                std::vector<std::string> shared_;
                std::vector<unsigned> xfFormat_;   // numFmtId of each cell style (cellXfs)
                std::unordered_map<unsigned, std::string> formats_;
                bool date1904_ = false;
            };
        }

        std::string XlsxFormat::decodeEscapes(const std::string& text) {
            if (text.find("_x") == std::string::npos) {
                return text;
            }
            std::string out;
            out.reserve(text.size());
            for (std::size_t i = 0; i < text.size(); ++i) {
                if (text[i] == '_' && i + 6 < text.size() + 0 && text[i + 1] == 'x' && text[i + 6] == '_') {
                    bool hex = true;
                    unsigned cp = 0;
                    for (std::size_t k = 2; k < 6; ++k) {
                        char c = text[i + k];
                        if (!std::isxdigit((unsigned char)c)) {
                            hex = false;
                            break;
                        }
                        cp = cp * 16 + unsigned(std::isdigit((unsigned char)c) ? c - '0' : (std::tolower((unsigned char)c) - 'a' + 10));
                    }
                    if (hex) {
                        if (cp == 0x5F) {
                            out += '_';
                        } else if (cp != 0) {
                            appendUtf8(out, cp);
                        }
                        i += 6;
                        continue;
                    }
                }
                out += text[i];
            }
            return out;
        }

        bool XlsxFormat::parseReference(const std::string& reference, std::size_t& column, std::size_t& row) {
            std::size_t i = 0;
            std::size_t letters = 0;
            std::size_t col = 0;
            while (i < reference.size() && std::isalpha((unsigned char)reference[i])) {
                col = col * 26 + std::size_t(std::toupper((unsigned char)reference[i]) - 'A' + 1);
                ++i;
                if (++letters > 3) {
                    return false;
                }
            }
            if (letters == 0 || i == reference.size()) {
                return false;
            }
            std::size_t number = 0;
            for (; i < reference.size(); ++i) {
                if (!std::isdigit((unsigned char)reference[i])) {
                    return false;
                }
                number = number * 10 + std::size_t(reference[i] - '0');
                if (number > 100000000) {
                    return false;
                }
            }
            if (number == 0) {
                return false;
            }
            column = col - 1;
            row = number - 1;
            return true;
        }

        bool XlsxFormat::recognizes(const std::string& path) const {
            if (!ZipFile::hasSignature(path)) {
                return false;
            }
            try {
                return ZipFile(path).find("xl/workbook.xml") != nullptr;
            } catch (const std::exception&) {
                return false;
            }
        }

        std::unique_ptr<TableReader> XlsxFormat::open(const std::string& path) const {
            return std::make_unique<XlsxReader>(path);
        }

        void XlsxFormat::write(const std::string&, std::size_t, std::size_t, const CellSource&) const {
            throw std::runtime_error("XlsxFormat::write: writing Excel files is not supported - save as .csv");
        }

    }
}

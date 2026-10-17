#include "io/xlsformat.h"

#include "io/compoundfile.h"
#include "io/rowassembler.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>
#include <unordered_map>

namespace manyrows {
    namespace io {

        namespace {
            typedef std::vector<std::uint8_t> Bytes;

            std::uint16_t rd16(const std::uint8_t* p) { return std::uint16_t(p[0] | (p[1] << 8)); }
            std::uint32_t rd32(const std::uint8_t* p) {
                return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
            }
            double rdDouble(const std::uint8_t* p) {
                double value;
                std::memcpy(&value, p, 8);
                return value;
            }

            // BIFF record ids.
            constexpr unsigned kDateMode = 0x0022;
            constexpr unsigned kFilePass = 0x002F;
            constexpr unsigned kContinue = 0x003C;
            constexpr unsigned kFormula = 0x0006;
            constexpr unsigned kEof = 0x000A;
            constexpr unsigned kBoundSheet = 0x0085;
            constexpr unsigned kMulRk = 0x00BD;
            constexpr unsigned kSst = 0x00FC;
            constexpr unsigned kLabelSst = 0x00FD;
            constexpr unsigned kXf = 0x00E0;
            constexpr unsigned kDimensions = 0x0200;
            constexpr unsigned kLabel = 0x0204;
            constexpr unsigned kBoolErr = 0x0205;
            constexpr unsigned kNumber = 0x0203;
            constexpr unsigned kString = 0x0207;
            constexpr unsigned kRk = 0x027E;
            constexpr unsigned kBof = 0x0809;
            constexpr unsigned kFormat = 0x041E;

            constexpr std::size_t kReportEvery = 256 * 1024;   // bytes between progress callbacks
            constexpr std::size_t kHeaderSniffRows = 21;
            constexpr std::size_t kMaxColumns = kMaxSheetColumns;

            void appendUtf8(std::string& out, unsigned cp) {
                if (cp < 0x80) {
                    out += char(cp);
                } else if (cp < 0x800) {
                    out += char(0xC0 | (cp >> 6));
                    out += char(0x80 | (cp & 0x3F));
                } else if (cp < 0x10000) {
                    out += char(0xE0 | (cp >> 12));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                } else {
                    out += char(0xF0 | (cp >> 18));
                    out += char(0x80 | ((cp >> 12) & 0x3F));
                    out += char(0x80 | ((cp >> 6) & 0x3F));
                    out += char(0x80 | (cp & 0x3F));
                }
            }

            // Bytes of one logical record: the first record's data and the CONTINUE records after it. A string
            // that straddles two parts repeats a one-byte "wide or not" flag at the start of the next part.
            class Chunks {
            public:
                Chunks(const std::uint8_t* data, std::size_t size) { parts_.push_back({data, size}); }
                explicit Chunks(std::vector<std::pair<const std::uint8_t*, std::size_t>> parts) : parts_(std::move(parts)) {}

                // True when the current part has no bytes left (more parts may follow).
                bool exhausted() const { return part_ >= parts_.size() || pos_ >= parts_[part_].second; }
                // True when no byte is left anywhere.
                bool atEnd() {
                    while (part_ < parts_.size() && pos_ >= parts_[part_].second) {
                        ++part_;
                        pos_ = 0;
                    }
                    return part_ >= parts_.size();
                }
                // Moves to the next part's first byte; throws when there is none.
                void advance() {
                    ++part_;
                    pos_ = 0;
                    if (part_ >= parts_.size()) {
                        throw std::runtime_error("XlsFormat: a record is truncated");
                    }
                }
                std::uint8_t byte() {
                    while (exhausted()) {
                        advance();
                    }
                    return parts_[part_].first[pos_++];
                }
                std::uint16_t u16() {
                    std::uint8_t low = byte();
                    return std::uint16_t(low | (byte() << 8));
                }
                std::uint32_t u32() {
                    std::uint32_t low = u16();
                    return low | (std::uint32_t(u16()) << 16);
                }
                void skip(std::size_t count) {
                    while (count > 0) {
                        if (exhausted()) {
                            advance();
                        }
                        std::size_t take = parts_[part_].second - pos_;
                        take = take < count ? take : count;
                        pos_ += take;
                        count -= take;
                    }
                }

            private:
                std::vector<std::pair<const std::uint8_t*, std::size_t>> parts_;
                std::size_t part_ = 0;
                std::size_t pos_ = 0;
            };

            // `count` characters, 8-bit (the low byte of UTF-16, i.e. Latin-1) or 16-bit; crossing into a
            // CONTINUE part re-reads the flag byte, which may switch between the two.
            std::string readChars(Chunks& chunks, std::size_t count, bool wide) {
                std::string out;
                auto unit = [&]() {
                    if (chunks.exhausted()) {
                        chunks.advance();
                        wide = (chunks.byte() & 1) != 0;
                    }
                    unsigned value = chunks.byte();
                    if (wide) {
                        value |= unsigned(chunks.byte()) << 8;
                    }
                    return value;
                };
                for (std::size_t i = 0; i < count; ++i) {
                    unsigned value = unit();
                    if (value >= 0xD800 && value < 0xDC00 && i + 1 < count) {
                        unsigned low = unit();
                        ++i;
                        if (low >= 0xDC00 && low < 0xE000) {
                            appendUtf8(out, 0x10000 + ((value - 0xD800) << 10) + (low - 0xDC00));
                        }
                        continue;
                    }
                    appendUtf8(out, value);
                }
                return out;
            }

            // BIFF8 "unicode string": 16-bit length, flags (1 wide, 4 phonetic data, 8 rich text), optional run
            // and phonetic sizes, the characters, then the run/phonetic bytes (skipped).
            std::string readUnicodeString(Chunks& chunks) {
                std::size_t count = chunks.u16();
                std::uint8_t flags = chunks.byte();
                std::size_t runs = (flags & 8) != 0 ? chunks.u16() : 0;
                std::size_t phonetic = (flags & 4) != 0 ? chunks.u32() : 0;
                std::string text = readChars(chunks, count, (flags & 1) != 0);
                chunks.skip(runs * 4 + phonetic);
                return text;
            }

            const char* errorText(unsigned code) {
                switch (code) {
                    case 0x00: return "#NULL!";
                    case 0x07: return "#DIV/0!";
                    case 0x0F: return "#VALUE!";
                    case 0x17: return "#REF!";
                    case 0x1D: return "#NAME?";
                    case 0x24: return "#NUM!";
                    case 0x2A: return "#N/A";
                    default: return "#ERROR!";
                }
            }

            double rkValue(std::uint32_t rk) {
                double value;
                if ((rk & 2) != 0) {
                    value = double(std::int32_t(rk) >> 2);
                } else {
                    std::uint64_t bits = std::uint64_t(rk & 0xFFFFFFFCu) << 32;
                    std::memcpy(&value, &bits, 8);
                }
                return (rk & 1) != 0 ? value / 100.0 : value;
            }

            // @reflect ignore=true
            struct SheetRef {
                std::string name;
                std::size_t offset = 0;   // of the sheet's BOF in the Workbook stream
            };

            // @reflect ignore=true
            class XlsReader : public TableReader {
            public:
                XlsReader(const std::string& path) {
                    CompoundFile file(path);
                    info_.fileBytes = file.fileBytes();
                    if (file.hasStream("Workbook")) {
                        book_ = file.readStream("Workbook");
                    } else if (file.hasStream("Book")) {
                        throw std::runtime_error("XlsFormat: Excel 5/95 workbooks (BIFF5) are not supported");
                    } else {
                        throw std::runtime_error("XlsFormat: the file holds no Excel workbook");
                    }
                    parseGlobals();
                    if (sheets_.empty()) {
                        throw std::runtime_error("XlsFormat: the workbook has no worksheets");
                    }
                    for (const SheetRef& sheet : sheets_) {
                        info_.sheets.push_back(sheet.name);
                    }
                    info_.metadata.push_back({"format", "Excel 97-2003 (BIFF8)"});
                    prepareSheet(0);
                }

                const TableInfo& info() const override { return info_; }

                void selectSheet(std::size_t index) override {
                    if (index >= sheets_.size()) {
                        throw std::runtime_error("XlsReader::selectSheet: no sheet " + std::to_string(index));
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
                    bool whole = walk(
                        sheets_[sheet_].offset,
                        [&](std::size_t row, std::size_t column, std::string&& text, bool numeric) {
                            return rows.add(row, column, std::move(text), numeric);
                        },
                        onProgress);
                    if (whole && rows.finish()) {
                        if (onProgress) {
                            onProgress(book_.size(), book_.size());
                        }
                        info_.rowCount.value = delivered;
                        info_.rowCount.certainty = Certainty::Exact;
                    }
                    return delivered;
                }

            private:
                typedef std::function<bool(std::size_t row, std::size_t column, std::string&& text, bool numeric)> Sink;

                // The record at `pos`: id, data pointer, length; advances pos past it.
                bool nextRecord(std::size_t& pos, unsigned& id, const std::uint8_t*& data, std::size_t& length) const {
                    if (pos + 4 > book_.size()) {
                        return false;
                    }
                    id = rd16(&book_[pos]);
                    length = rd16(&book_[pos + 2]);
                    if (pos + 4 + length > book_.size()) {
                        throw std::runtime_error("XlsFormat: the workbook is truncated");
                    }
                    data = book_.data() + pos + 4;
                    pos += 4 + length;
                    return true;
                }

                void parseGlobals() {
                    std::size_t pos = 0;
                    unsigned id = 0;
                    const std::uint8_t* d = nullptr;
                    std::size_t len = 0;
                    if (!nextRecord(pos, id, d, len) || id != kBof || len < 4) {
                        throw std::runtime_error("XlsFormat: the workbook does not start with a BOF record");
                    }
                    unsigned version = rd16(d);
                    if (version != 0x0600) {
                        throw std::runtime_error(version == 0x0500 ? "XlsFormat: Excel 5/95 workbooks (BIFF5) are not supported"
                                                                   : "XlsFormat: unknown workbook version");
                    }
                    int depth = 1;
                    while (nextRecord(pos, id, d, len)) {
                        if (id == kBof) {
                            ++depth;
                        } else if (id == kEof) {
                            if (--depth <= 0) {
                                break;
                            }
                        } else if (depth != 1) {
                            continue;
                        } else if (id == kFilePass) {
                            throw std::runtime_error("XlsFormat: the workbook is password-protected");
                        } else if (id == kDateMode && len >= 2) {
                            date1904_ = rd16(d) == 1;
                        } else if (id == kBoundSheet && len >= 8) {
                            if (d[5] == 0) {   // a worksheet (not a chart or macro sheet)
                                Chunks name(d + 6, len - 6);
                                std::size_t count = name.byte();
                                bool wide = (name.byte() & 1) != 0;
                                SheetRef sheet;
                                sheet.name = readChars(name, count, wide);
                                sheet.offset = rd32(d);
                                sheets_.push_back(std::move(sheet));
                            }
                        } else if (id == kXf && len >= 4) {
                            xfFormat_.push_back(rd16(d + 2));
                        } else if (id == kFormat && len >= 5) {
                            Chunks text(d + 2, len - 2);
                            formats_[rd16(d)] = readUnicodeString(text);
                        } else if (id == kSst) {
                            std::vector<std::pair<const std::uint8_t*, std::size_t>> parts;
                            parts.push_back({d, len});
                            while (pos + 4 <= book_.size() && rd16(&book_[pos]) == kContinue) {
                                std::size_t more = rd16(&book_[pos + 2]);
                                if (pos + 4 + more > book_.size()) {
                                    throw std::runtime_error("XlsFormat: the workbook is truncated");
                                }
                                parts.push_back({book_.data() + pos + 4, more});
                                pos += 4 + more;
                            }
                            parseSharedStrings(std::move(parts));
                        }
                    }
                }

                void parseSharedStrings(std::vector<std::pair<const std::uint8_t*, std::size_t>> parts) {
                    Chunks chunks(std::move(parts));
                    try {
                        chunks.u32();   // total references
                        std::size_t unique = chunks.u32();
                        sst_.reserve(unique < 10000000 ? unique : 10000000);
                        for (std::size_t i = 0; i < unique && !chunks.atEnd(); ++i) {
                            sst_.push_back(readUnicodeString(chunks));
                        }
                    } catch (const std::runtime_error&) {
                        // a damaged table keeps the strings read so far; cells past them read as blank
                    }
                }

                bool isDateXf(std::size_t xf) const {
                    if (xf >= xfFormat_.size()) {
                        return false;
                    }
                    if (xfDate_.size() != xfFormat_.size()) {
                        xfDate_.assign(xfFormat_.size(), -1);
                    }
                    signed char& known = xfDate_[xf];
                    if (known < 0) {
                        unsigned format = xfFormat_[xf];
                        bool date = XlsFormat::isBuiltInDateFormat(format);
                        if (!date) {
                            auto found = formats_.find(std::uint16_t(format));
                            date = found != formats_.end() && XlsFormat::isDateFormat(found->second);
                        }
                        known = date ? 1 : 0;
                    }
                    return known == 1;
                }

                bool emitNumber(const Sink& sink, std::size_t row, std::size_t column, std::size_t xf, double value) const {
                    if (isDateXf(xf)) {
                        return sink(row, column, XlsFormat::dateText(value, date1904_), false);
                    }
                    return sink(row, column, XlsFormat::numberText(value), true);
                }

                // Reads the worksheet substream at `offset`, handing every cell with a value to `sink` (false
                // stops). Returns false when stopped by the sink or by `progress`, true when it ran to the EOF.
                bool walk(std::size_t offset, const Sink& sink, const ProgressCallback& progress) const {
                    std::size_t pos = offset;
                    std::size_t nextReport = pos + kReportEvery;
                    int depth = 0;
                    bool pendingString = false;
                    std::size_t pendingRow = 0;
                    std::size_t pendingColumn = 0;
                    unsigned id = 0;
                    const std::uint8_t* d = nullptr;
                    std::size_t len = 0;
                    while (nextRecord(pos, id, d, len)) {
                        if (id == kBof) {
                            ++depth;
                            continue;
                        }
                        if (id == kEof) {
                            if (--depth <= 0) {
                                return true;
                            }
                            continue;
                        }
                        if (depth != 1) {
                            continue;   // a chart or other nested substream
                        }
                        if (progress && pos >= nextReport) {
                            nextReport = pos + kReportEvery;
                            if (!progress(pos, book_.size())) {
                                return false;
                            }
                        }
                        bool more = true;
                        switch (id) {
                            case kNumber:
                                if (len >= 14) {
                                    more = emitNumber(sink, rd16(d), rd16(d + 2), rd16(d + 4), rdDouble(d + 6));
                                }
                                break;
                            case kRk:
                                if (len >= 10) {
                                    more = emitNumber(sink, rd16(d), rd16(d + 2), rd16(d + 4), rkValue(rd32(d + 6)));
                                }
                                break;
                            case kMulRk:
                                if (len >= 12) {
                                    std::size_t row = rd16(d);
                                    std::size_t first = rd16(d + 2);
                                    std::size_t count = (len - 6) / 6;
                                    for (std::size_t i = 0; i < count && more; ++i) {
                                        more = emitNumber(sink, row, first + i, rd16(d + 4 + i * 6), rkValue(rd32(d + 6 + i * 6)));
                                    }
                                }
                                break;
                            case kLabelSst:
                                if (len >= 10) {
                                    std::size_t index = rd32(d + 6);
                                    if (index < sst_.size()) {
                                        more = sink(rd16(d), rd16(d + 2), std::string(sst_[index]), false);
                                    }
                                }
                                break;
                            case kLabel:
                                if (len >= 8) {
                                    Chunks text(d + 6, len - 6);
                                    more = sink(rd16(d), rd16(d + 2), readUnicodeString(text), false);
                                }
                                break;
                            case kBoolErr:
                                if (len >= 8) {
                                    std::string text = d[7] != 0 ? errorText(d[6]) : (d[6] != 0 ? "TRUE" : "FALSE");
                                    more = sink(rd16(d), rd16(d + 2), std::move(text), false);
                                }
                                break;
                            case kFormula:
                                if (len >= 14) {
                                    std::size_t row = rd16(d);
                                    std::size_t column = rd16(d + 2);
                                    const std::uint8_t* result = d + 6;
                                    if (rd16(result + 6) == 0xFFFF) {
                                        switch (result[0]) {
                                            case 0:   // text: the STRING record that follows holds it
                                                pendingString = true;
                                                pendingRow = row;
                                                pendingColumn = column;
                                                break;
                                            case 1:
                                                more = sink(row, column, result[2] != 0 ? "TRUE" : "FALSE", false);
                                                break;
                                            case 2:
                                                more = sink(row, column, errorText(result[2]), false);
                                                break;
                                            default:   // empty string result
                                                break;
                                        }
                                    } else {
                                        more = emitNumber(sink, row, column, rd16(d + 4), rdDouble(result));
                                    }
                                }
                                break;
                            case kString:
                                if (pendingString) {
                                    pendingString = false;
                                    Chunks text(d, len);
                                    more = sink(pendingRow, pendingColumn, readUnicodeString(text), false);
                                }
                                break;
                            default:
                                break;
                        }
                        if (!more) {
                            return false;
                        }
                    }
                    return true;
                }

                // The sheet's size from its DIMENSIONS record: rows and columns, each counted from zero.
                void readDimensions(std::size_t offset, std::size_t& rows, std::size_t& columns) const {
                    rows = 0;
                    columns = 0;
                    std::size_t pos = offset;
                    int depth = 0;
                    unsigned id = 0;
                    const std::uint8_t* d = nullptr;
                    std::size_t len = 0;
                    while (nextRecord(pos, id, d, len)) {
                        if (id == kBof) {
                            ++depth;
                        } else if (id == kEof) {
                            return;
                        } else if (id == kDimensions && depth == 1 && len >= 14) {
                            rows = rd32(d + 4);      // last row + 1
                            columns = rd16(d + 10);  // last column + 1
                            return;
                        }
                    }
                }

                void prepareSheet(std::size_t index) {
                    sheet_ = index;
                    const std::size_t offset = sheets_[index].offset;
                    if (offset >= book_.size()) {
                        throw std::runtime_error("XlsFormat: a sheet points outside the workbook");
                    }
                    std::size_t rows = 0;
                    std::size_t columns = 0;
                    readDimensions(offset, rows, columns);
                    columns = columns < kMaxColumns ? columns : kMaxColumns;
                    info_.rowCount = {rows, Certainty::Exact};
                    info_.columnCount = {columns, Certainty::Exact};
                    info_.hasHeaderRow = false;
                    info_.columnTitles.clear();
                    info_.metadata.erase(
                        std::remove_if(info_.metadata.begin(), info_.metadata.end(),
                            [](const std::pair<std::string, std::string>& item) { return item.first == "sheet"; }),
                        info_.metadata.end());
                    info_.metadata.push_back({"sheet", sheets_[index].name});

                    // The first rows decide whether row one is a header.
                    std::vector<std::vector<Cell>> top;
                    RowAssembler rows21([&](std::size_t, const std::vector<Cell>& cells) {
                        top.push_back(cells);
                        return top.size() < kHeaderSniffRows;
                    });
                    bool whole = walk(offset, [&](std::size_t row, std::size_t column, std::string&& text, bool numeric) {
                        return rows21.add(row, column, std::move(text), numeric);
                    }, nullptr);
                    if (whole) {
                        rows21.finish();
                    }
                    info_.hasHeaderRow = sniffHeader(top, columns, info_.columnTitles);
                }

                TableInfo info_;
                Bytes book_;
                std::vector<SheetRef> sheets_;
                std::size_t sheet_ = 0;
                std::vector<std::string> sst_;
                std::vector<std::uint16_t> xfFormat_;
                mutable std::vector<signed char> xfDate_;   // per XF: -1 unknown, 0 no, 1 date
                std::unordered_map<std::uint16_t, std::string> formats_;
                bool date1904_ = false;
            };

            // Days since 1970-01-01 -> calendar date (Howard Hinnant's civil_from_days).
            void civilFromDays(long long z, int& year, unsigned& month, unsigned& day) {
                z += 719468;
                long long era = (z >= 0 ? z : z - 146096) / 146097;
                unsigned dayOfEra = unsigned(z - era * 146097);
                unsigned yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
                long long y = (long long)yearOfEra + era * 400;
                unsigned dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
                unsigned mp = (5 * dayOfYear + 2) / 153;
                day = dayOfYear - (153 * mp + 2) / 5 + 1;
                month = mp < 10 ? mp + 3 : mp - 9;
                year = int(y + (month <= 2 ? 1 : 0));
            }
        }

        std::string XlsFormat::numberText(double value) {
            if (value == 0.0) {
                return "0";
            }
            if (!std::isfinite(value)) {
                return "#NUM!";
            }
            char buffer[64];
            if (value == std::floor(value) && std::fabs(value) < 1e15) {
                std::snprintf(buffer, sizeof(buffer), "%.0f", value);
            } else {
                std::snprintf(buffer, sizeof(buffer), "%.15g", value);
            }
            return buffer;
        }

        std::string XlsFormat::dateText(double serial, bool date1904) {
            if (!std::isfinite(serial) || serial < 0 || serial > 2958465.0) {
                return numberText(serial);
            }
            double whole = std::floor(serial);
            long long seconds = std::llround((serial - whole) * 86400.0);
            if (seconds >= 86400) {
                whole += 1;
                seconds -= 86400;
            }
            long long days = (long long)whole;
            char buffer[48];
            if (!date1904 && days == 60) {
                // Excel's phantom leap day, 1900-02-29.
                std::snprintf(buffer, sizeof(buffer), "1900-02-29");
            } else if (!date1904 && days == 0) {
                // A time of day (no date part).
                std::snprintf(buffer, sizeof(buffer), "%02lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
                return buffer;
            } else {
                long long unixDays = date1904 ? days - 24107 : (days >= 61 ? days - 25569 : days - 25568);
                int year = 0;
                unsigned month = 0;
                unsigned day = 0;
                civilFromDays(unixDays, year, month, day);
                std::snprintf(buffer, sizeof(buffer), "%04d-%02u-%02u", year, month, day);
            }
            std::string text = buffer;
            if (seconds != 0) {
                std::snprintf(buffer, sizeof(buffer), " %02lld:%02lld:%02lld", seconds / 3600, (seconds / 60) % 60, seconds % 60);
                text += buffer;
            }
            return text;
        }

        bool XlsFormat::isBuiltInDateFormat(unsigned id) {
            return (id >= 14 && id <= 22) || (id >= 27 && id <= 36) || (id >= 45 && id <= 47) || (id >= 50 && id <= 58);
        }

        bool XlsFormat::isDateFormat(const std::string& format) {
            // Only the first section matters; text in quotes, [color] / [$currency] tags and escaped
            // characters are not format letters.
            for (std::size_t i = 0; i < format.size(); ++i) {
                char c = format[i];
                if (c == '"') {
                    i = format.find('"', i + 1);
                    if (i == std::string::npos) {
                        break;
                    }
                } else if (c == '[') {
                    std::size_t close = format.find(']', i + 1);
                    if (close == std::string::npos) {
                        break;
                    }
                    char first = close > i + 1 ? char(std::tolower((unsigned char)format[i + 1])) : ' ';
                    if (first == 'h' || first == 'm' || first == 's') {
                        return true;   // [h]:mm - elapsed time
                    }
                    i = close;
                } else if (c == '\\' || c == '_' || c == '*') {
                    ++i;
                } else if (c == ';') {
                    break;
                } else {
                    char lowerCase = char(std::tolower((unsigned char)c));
                    if (lowerCase == 'y' || lowerCase == 'd' || lowerCase == 'h' || lowerCase == 's' || lowerCase == 'm') {
                        return true;
                    }
                }
            }
            return false;
        }

        bool XlsFormat::recognizes(const std::string& path) const {
            return CompoundFile::hasSignature(path);
        }

        std::unique_ptr<TableReader> XlsFormat::open(const std::string& path) const {
            return std::make_unique<XlsReader>(path);
        }

        void XlsFormat::write(const std::string&, std::size_t, std::size_t, const CellSource&) const {
            throw std::runtime_error("XlsFormat::write: writing Excel 97-2003 files is not supported - save as .csv");
        }

    }
}

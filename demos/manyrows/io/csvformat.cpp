#include "io/csvformat.h"

#include <newui/utils.h>

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <map>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace manyrows {
    namespace io {

        namespace {
            constexpr std::size_t kSampleBytes = 64 * 1024;
            constexpr std::size_t kChunkBytes = 256 * 1024;
            constexpr std::size_t kSampleRecords = 100;

            std::filesystem::path pathOf(const std::string& utf8) {
                return std::filesystem::path(newui::utf8ToWide(utf8));
            }

            std::string trimmed(const std::string& text) {
                std::size_t first = 0;
                std::size_t last = text.size();
                while (first < last && (text[first] == ' ' || text[first] == '\t')) {
                    ++first;
                }
                while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
                    --last;
                }
                return text.substr(first, last - first);
            }

            bool looksNumeric(const std::string& field) {
                std::string text = trimmed(field);
                if (text.empty()) {
                    return false;
                }
                char* end = nullptr;
                std::strtod(text.c_str(), &end);
                return end != text.c_str() && *end == '\0';
            }

            bool validUtf8(const std::string& bytes, bool truncated) {
                std::size_t i = 0;
                while (i < bytes.size()) {
                    unsigned char c = (unsigned char)bytes[i];
                    std::size_t extra = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : 4;
                    if (extra == 4) {
                        return false;
                    }
                    if (i + extra >= bytes.size() && extra > 0) {
                        // A sequence cut off by the end of a truncated sample is not an error.
                        return truncated;
                    }
                    for (std::size_t k = 1; k <= extra; ++k) {
                        if (((unsigned char)bytes[i + k] & 0xC0) != 0x80) {
                            return false;
                        }
                    }
                    i += extra + 1;
                }
                return true;
            }

            std::string delimiterName(char delimiter) {
                switch (delimiter) {
                    case ',': return "comma";
                    case ';': return "semicolon";
                    case '\t': return "tab";
                    case '|': return "pipe";
                    default: return std::string(1, delimiter);
                }
            }

            // Incremental RFC 4180 record splitter. feed() takes bytes in any chunking; each completed
            // record goes to `emit(const std::vector<std::string>&) -> bool` (false stops everything).
            class RecordParser {
            public:
                explicit RecordParser(char delimiter) : delimiter_(delimiter) {}

                bool stopped() const { return stopped_; }

                template <typename Emit>
                bool feed(const char* data, std::size_t size, Emit&& emit) {
                    for (std::size_t i = 0; i < size && !stopped_; ++i) {
                        step(data[i], emit);
                    }
                    return !stopped_;
                }

                // End of input: a final record without a terminator is delivered.
                template <typename Emit>
                bool finish(Emit&& emit) {
                    if (!stopped_ && state_ != State::AfterCR && (hasContent_ || !fields_.empty() || !field_.empty())) {
                        endRecord(emit);
                    }
                    state_ = State::FieldStart;
                    return !stopped_;
                }

            private:
                enum class State { FieldStart, Unquoted, Quoted, QuoteInQuoted, AfterCR };

                void endField() {
                    fields_.push_back(std::move(field_));
                    field_.clear();
                    state_ = State::FieldStart;
                    hasContent_ = true;
                }

                template <typename Emit>
                void endRecord(Emit& emit) {
                    // A completely empty line is not a record.
                    if (hasContent_ || !fields_.empty() || !field_.empty()) {
                        fields_.push_back(std::move(field_));
                        if (!emit(static_cast<const std::vector<std::string>&>(fields_))) {
                            stopped_ = true;
                        }
                    }
                    fields_.clear();
                    field_.clear();
                    hasContent_ = false;
                    state_ = State::FieldStart;
                }

                template <typename Emit>
                void step(char c, Emit& emit) {
                    if (state_ == State::AfterCR) {
                        state_ = State::FieldStart;
                        if (c == '\n') {
                            return;
                        }
                    }
                    switch (state_) {
                        case State::FieldStart:
                            if (c == '"') {
                                state_ = State::Quoted;
                                hasContent_ = true;
                            } else if (c == delimiter_) {
                                endField();
                            } else if (c == '\r') {
                                endRecord(emit);
                                state_ = State::AfterCR;
                            } else if (c == '\n') {
                                endRecord(emit);
                            } else {
                                field_ += c;
                                state_ = State::Unquoted;
                                hasContent_ = true;
                            }
                            break;
                        case State::Unquoted:
                            if (c == delimiter_) {
                                endField();
                            } else if (c == '\r') {
                                endRecord(emit);
                                state_ = State::AfterCR;
                            } else if (c == '\n') {
                                endRecord(emit);
                            } else {
                                field_ += c;
                            }
                            break;
                        case State::Quoted:
                            if (c == '"') {
                                state_ = State::QuoteInQuoted;
                            } else {
                                field_ += c;
                            }
                            break;
                        case State::QuoteInQuoted:
                            if (c == '"') {
                                field_ += '"';
                                state_ = State::Quoted;
                            } else if (c == delimiter_) {
                                endField();
                            } else if (c == '\r') {
                                endRecord(emit);
                                state_ = State::AfterCR;
                            } else if (c == '\n') {
                                endRecord(emit);
                            } else {
                                field_ += c;   // stray text after a closing quote: keep it
                                state_ = State::Unquoted;
                            }
                            break;
                        case State::AfterCR:
                            break;
                    }
                }

                char delimiter_;
                State state_ = State::FieldStart;
                std::vector<std::string> fields_;
                std::string field_;
                bool hasContent_ = false;
                bool stopped_ = false;
            };

            // Parses `bytes` as one block: every record that ended inside it (and, with `atEnd`, a final
            // unterminated one), up to `limit` records.
            std::vector<std::vector<std::string>> sampleRecords(const std::string& bytes, char delimiter, bool atEnd, std::size_t limit) {
                std::vector<std::vector<std::string>> records;
                RecordParser parser(delimiter);
                auto emit = [&](const std::vector<std::string>& fields) {
                    records.push_back(fields);
                    return records.size() < limit;
                };
                parser.feed(bytes.data(), bytes.size(), emit);
                if (atEnd) {
                    parser.finish(emit);
                }
                return records;
            }

            // The most common field count in `records` and how many records have it.
            std::pair<std::size_t, std::size_t> modalColumnCount(const std::vector<std::vector<std::string>>& records) {
                std::map<std::size_t, std::size_t> histogram;
                for (const auto& record : records) {
                    ++histogram[record.size()];
                }
                std::size_t bestCount = 0;
                std::size_t bestFrequency = 0;
                for (const auto& [count, frequency] : histogram) {
                    if (frequency > bestFrequency || (frequency == bestFrequency && count > bestCount)) {
                        bestCount = count;
                        bestFrequency = frequency;
                    }
                }
                return {bestCount, bestFrequency};
            }

            char sniffDelimiter(const std::string& bytes, bool atEnd) {
                static const char kCandidates[] = {',', ';', '\t', '|'};
                char best = ',';
                double bestScore = 0.0;
                for (char candidate : kCandidates) {
                    auto records = sampleRecords(bytes, candidate, atEnd, kSampleRecords);
                    if (records.empty()) {
                        continue;
                    }
                    auto [columns, frequency] = modalColumnCount(records);
                    if (columns < 2) {
                        continue;
                    }
                    // Consistent record widths beat a high count of ragged ones; wider is a tiebreak.
                    double score = double(frequency) / double(records.size()) * 1000.0 + double(columns < 50 ? columns : 50);
                    if (score > bestScore) {
                        bestScore = score;
                        best = candidate;
                    }
                }
                return best;
            }

            bool looksLikeHeader(const std::vector<std::vector<std::string>>& records, std::size_t columns) {
                if (records.size() < 2 || records[0].size() != columns) {
                    return false;
                }
                for (const std::string& title : records[0]) {
                    if (trimmed(title).empty() || looksNumeric(title)) {
                        return false;
                    }
                }
                // Something below must differ in kind: a column whose sampled values are all numeric.
                for (std::size_t column = 0; column < columns; ++column) {
                    bool allNumeric = true;
                    std::size_t seen = 0;
                    for (std::size_t r = 1; r < records.size(); ++r) {
                        if (column >= records[r].size() || trimmed(records[r][column]).empty()) {
                            continue;
                        }
                        ++seen;
                        if (!looksNumeric(records[r][column])) {
                            allNumeric = false;
                            break;
                        }
                    }
                    if (seen > 0 && allNumeric) {
                        return true;
                    }
                }
                return false;
            }

            class CsvReader : public TableReader {
            public:
                CsvReader(const std::string& path, char forcedDelimiter, bool force) : path_(path) {
                    std::ifstream file(pathOf(path), std::ios::binary);
                    if (!file) {
                        throw std::runtime_error("CsvFormat::open: cannot open \"" + path + "\"");
                    }
                    file.seekg(0, std::ios::end);
                    info_.fileBytes = std::size_t(file.tellg());
                    file.seekg(0, std::ios::beg);

                    std::string sample(kSampleBytes, '\0');
                    file.read(&sample[0], std::streamsize(kSampleBytes));
                    sample.resize(std::size_t(file.gcount()));
                    bool wholeFile = sample.size() == info_.fileBytes;

                    if (sample.size() >= 2 && (((unsigned char)sample[0] == 0xFF && (unsigned char)sample[1] == 0xFE) ||
                                                  ((unsigned char)sample[0] == 0xFE && (unsigned char)sample[1] == 0xFF))) {
                        throw std::runtime_error("CsvFormat::open: UTF-16 files are not supported (\"" + path + "\")");
                    }
                    bool bom = sample.size() >= 3 && (unsigned char)sample[0] == 0xEF && (unsigned char)sample[1] == 0xBB &&
                               (unsigned char)sample[2] == 0xBF;
                    bomSize_ = bom ? 3 : 0;
                    if (bom) {
                        sample.erase(0, 3);
                    }
                    if (sample.find('\0') != std::string::npos) {
                        throw std::runtime_error("CsvFormat::open: \"" + path + "\" looks like a binary file");
                    }

                    delimiter_ = force ? forcedDelimiter : sniffDelimiter(sample, wholeFile);
                    auto records = sampleRecords(sample, delimiter_, wholeFile, kSampleRecords);
                    auto [columns, frequency] = modalColumnCount(records);
                    (void)frequency;

                    info_.columnCount.value = columns;
                    info_.columnCount.certainty = records.empty() ? Certainty::Unknown : Certainty::Estimate;
                    info_.hasHeaderRow = looksLikeHeader(records, columns);
                    if (info_.hasHeaderRow) {
                        info_.columnTitles = records[0];
                    }

                    // Row count: exact when the whole file was in the sample, else file size / average record size.
                    if (wholeFile) {
                        auto all = sampleRecords(sample, delimiter_, true, std::size_t(-1));
                        info_.rowCount.value = all.size();
                        info_.rowCount.certainty = Certainty::Exact;
                        info_.columnCount.certainty = Certainty::Exact;
                        std::size_t widest = 0;
                        for (const auto& record : all) {
                            widest = record.size() > widest ? record.size() : widest;
                        }
                        info_.columnCount.value = widest;   // an exact scan can report the true width
                    } else if (!records.empty()) {
                        // Every complete record in the block (not just the first kSampleRecords), plus half
                        // for the partial one cut off at the end: close enough for a progress bar.
                        std::size_t complete = sampleRecords(sample, delimiter_, false, std::size_t(-1)).size();
                        double average = double(sample.size()) / (double(complete) + 0.5);
                        double estimate = average > 0.0 ? double(info_.fileBytes - bomSize_) / average : 0.0;
                        std::size_t rows = std::size_t(std::llround(estimate));
                        info_.rowCount.value = rows > records.size() ? rows : records.size();
                        info_.rowCount.certainty = Certainty::Estimate;
                    }

                    info_.metadata.emplace_back("delimiter", delimiterName(delimiter_));
                    bool utf8 = validUtf8(sample, !wholeFile);
                    info_.metadata.emplace_back("encoding", utf8 ? (bom ? "UTF-8 (BOM)" : "UTF-8") : "8-bit (not valid UTF-8)");
                    std::size_t cr = sample.find('\r');
                    std::size_t lf = sample.find('\n');
                    info_.metadata.emplace_back("line-ending", cr == std::string::npos ? (lf == std::string::npos ? "none" : "lf")
                                                              : (lf == cr + 1 ? "crlf" : "cr"));
                }

                const TableInfo& info() const override { return info_; }

                std::size_t readRows(const RowCallback& onRow, const ProgressCallback& onProgress) override {
                    std::ifstream file(pathOf(path_), std::ios::binary);
                    if (!file) {
                        throw std::runtime_error("CsvReader::readRows: cannot open \"" + path_ + "\"");
                    }
                    file.seekg(std::streamoff(bomSize_), std::ios::beg);

                    RecordParser parser(delimiter_);
                    std::size_t delivered = 0;
                    auto emit = [&](const std::vector<std::string>& fields) {
                        ++delivered;
                        return onRow(fields);
                    };
                    std::vector<char> buffer(kChunkBytes);
                    std::size_t consumed = bomSize_;
                    bool cancelled = false;
                    while (file && !cancelled) {
                        file.read(buffer.data(), std::streamsize(buffer.size()));
                        std::size_t got = std::size_t(file.gcount());
                        if (got == 0) {
                            break;
                        }
                        if (!parser.feed(buffer.data(), got, emit)) {
                            cancelled = true;
                            break;
                        }
                        consumed += got;
                        if (onProgress && !onProgress(consumed, info_.fileBytes)) {
                            cancelled = true;
                        }
                    }
                    if (!cancelled) {
                        parser.finish(emit);
                        if (onProgress) {
                            onProgress(info_.fileBytes, info_.fileBytes);
                        }
                        // Now the count is known for sure.
                        info_.rowCount.value = delivered;
                        info_.rowCount.certainty = Certainty::Exact;
                    }
                    return delivered;
                }

            private:
                std::string path_;
                char delimiter_ = ',';
                std::size_t bomSize_ = 0;
                TableInfo info_;
            };
        }

        std::string CsvFormat::escapeField(const std::string& field, char delimiter) {
            bool needsQuotes = !field.empty() && (field.front() == ' ' || field.back() == ' ');
            for (char c : field) {
                if (c == delimiter || c == '"' || c == '\r' || c == '\n') {
                    needsQuotes = true;
                    break;
                }
            }
            if (!needsQuotes) {
                return field;
            }
            std::string quoted = "\"";
            for (char c : field) {
                if (c == '"') {
                    quoted += '"';
                }
                quoted += c;
            }
            quoted += '"';
            return quoted;
        }

        bool CsvFormat::recognizes(const std::string& path) const {
            try {
                CsvReader reader(path, options_.delimiter, options_.forceDelimiter);
                return reader.info().columnCount.value.has_value() && *reader.info().columnCount.value > 0;
            } catch (const std::runtime_error&) {
                return false;
            }
        }

        std::unique_ptr<TableReader> CsvFormat::open(const std::string& path) const {
            return std::make_unique<CsvReader>(path, options_.delimiter, options_.forceDelimiter);
        }

        void CsvFormat::write(const std::string& path, std::size_t rows, std::size_t columns, const CellSource& cell) const {
            char delimiter = options_.delimiter;
            if (delimiter == 0) {
                std::string extension = pathOf(path).extension().string();
                for (char& c : extension) {
                    c = char(std::tolower((unsigned char)c));
                }
                delimiter = extension == ".tsv" ? '\t' : ',';
            }
            std::ofstream file(pathOf(path), std::ios::binary | std::ios::trunc);
            if (!file) {
                throw std::runtime_error("CsvFormat::write: cannot create \"" + path + "\"");
            }
            if (options_.writeBom) {
                file.write("\xEF\xBB\xBF", 3);
            }
            std::string line;
            for (std::size_t row = 0; row < rows; ++row) {
                line.clear();
                for (std::size_t column = 0; column < columns; ++column) {
                    if (column > 0) {
                        line += delimiter;
                    }
                    line += escapeField(cell(row, column), delimiter);
                }
                line += options_.lineEnding;
                file.write(line.data(), std::streamsize(line.size()));
            }
            file.flush();
            if (!file) {
                throw std::runtime_error("CsvFormat::write: failed writing \"" + path + "\"");
            }
        }

    }
}

#include "io/csvformat.h"
#include "manyrowsmodel.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace manyrows;
using namespace manyrows::io;

namespace {
    // A temp file that deletes itself.
    class TempFile {
    public:
        explicit TempFile(const std::string& name, const std::string& content = std::string(), bool write = true) {
            path_ = (std::filesystem::temp_directory_path() / ("manyrows_test_" + name)).string();
            if (write) {
                std::ofstream file(path_, std::ios::binary | std::ios::trunc);
                file << content;
            }
        }
        ~TempFile() { std::error_code ignored; std::filesystem::remove(path_, ignored); }
        const std::string& path() const { return path_; }
    private:
        std::string path_;
    };

    std::vector<std::vector<std::string>> readAll(const std::string& path, TableInfo* info = nullptr) {
        CsvFormat format;
        auto reader = format.open(path);
        if (info != nullptr) {
            *info = reader->info();
        }
        std::vector<std::vector<std::string>> rows;
        reader->readRows([&rows](const std::vector<std::string>& cells) { rows.push_back(cells); return true; });
        return rows;
    }

    std::string metadata(const TableInfo& info, const std::string& key) {
        for (const auto& entry : info.metadata) {
            if (entry.first == key) {
                return entry.second;
            }
        }
        return std::string();
    }
}

TEST(CsvRead, PlainRecordsAndTheInfoReadUpFront) {
    TempFile file("plain.csv", "name,qty,price\r\nbolt,10,0.25\r\nnut,200,0.05\r\n");
    TableInfo info;
    auto rows = readAll(file.path(), &info);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[1], (std::vector<std::string>{"bolt", "10", "0.25"}));
    EXPECT_EQ(*info.rowCount.value, 3u);
    EXPECT_EQ(info.rowCount.certainty, Certainty::Exact);
    EXPECT_EQ(*info.columnCount.value, 3u);
    EXPECT_TRUE(info.hasHeaderRow);
    EXPECT_EQ(info.columnTitles, (std::vector<std::string>{"name", "qty", "price"}));
    EXPECT_EQ(metadata(info, "delimiter"), "comma");
    EXPECT_EQ(metadata(info, "line-ending"), "crlf");
    EXPECT_EQ(metadata(info, "encoding"), "UTF-8");
}

TEST(CsvRead, QuotedFieldsWithDelimitersQuotesAndLineBreaks) {
    TempFile file("quoted.csv", "a,b\n\"x,y\",\"say \"\"hi\"\"\"\n\"line1\nline2\",z\n");
    auto rows = readAll(file.path());
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[1][0], "x,y");
    EXPECT_EQ(rows[1][1], "say \"hi\"");
    EXPECT_EQ(rows[2][0], "line1\nline2");
    EXPECT_EQ(rows[2][1], "z");
}

TEST(CsvRead, LineEndingsBlankLinesAndMissingFinalNewline) {
    TempFile file("endings.csv", "a,b\r1,2\n\n3,4\r\n5,6");
    auto rows = readAll(file.path());
    ASSERT_EQ(rows.size(), 4u);   // the blank line is skipped, the unterminated last record kept
    EXPECT_EQ(rows[2][0], "3");
    EXPECT_EQ(rows[3][1], "6");
}

TEST(CsvRead, EmptyFieldsAreKeptAndAQuotedEmptyIsARecord) {
    TempFile file("empties.csv", "a,,c\n,,\n\"\"\n");
    auto rows = readAll(file.path());
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0], (std::vector<std::string>{"a", "", "c"}));
    EXPECT_EQ(rows[1], (std::vector<std::string>{"", "", ""}));
    EXPECT_EQ(rows[2], (std::vector<std::string>{""}));
}

TEST(CsvRead, Utf8BomIsSkippedAndReported) {
    TempFile file("bom.csv", "\xEF\xBB\xBFh1,h2\nv1,v2\n");
    TableInfo info;
    auto rows = readAll(file.path(), &info);
    EXPECT_EQ(rows[0][0], "h1");
    EXPECT_EQ(metadata(info, "encoding"), "UTF-8 (BOM)");
}

TEST(CsvRead, DelimiterIsSniffed) {
    {
        TempFile file("semi.csv", "a;b;c\n1;2;3\n4;5;6\n");
        TableInfo info;
        auto rows = readAll(file.path(), &info);
        EXPECT_EQ(metadata(info, "delimiter"), "semicolon");
        EXPECT_EQ(rows[1][2], "3");
    }
    {
        TempFile file("tabs.tsv", "a\tb\n1\t2\n3\t4\n");
        TableInfo info;
        readAll(file.path(), &info);
        EXPECT_EQ(metadata(info, "delimiter"), "tab");
    }
    {
        // A decimal comma must not make "," win over ";" when the rows are consistent.
        TempFile file("decimal.csv", "item;price\nbolt;0,25\nnut;0,05\n");
        TableInfo info;
        readAll(file.path(), &info);
        EXPECT_EQ(metadata(info, "delimiter"), "semicolon");
        EXPECT_EQ(*info.columnCount.value, 2u);
    }
}

TEST(CsvRead, HeaderDetection) {
    {
        TempFile file("noheader.csv", "1,2,3\n4,5,6\n");
        TableInfo info;
        readAll(file.path(), &info);
        EXPECT_FALSE(info.hasHeaderRow);
        EXPECT_TRUE(info.columnTitles.empty());
    }
    {
        // All text below too: nothing says the first row is a header.
        TempFile file("alltext.csv", "a,b\nc,d\ne,f\n");
        TableInfo info;
        readAll(file.path(), &info);
        EXPECT_FALSE(info.hasHeaderRow);
    }
}

TEST(CsvRead, LargeFileGivesAnEstimateThenTheExactCount) {
    std::ostringstream text;
    text << "id,name,score\n";
    const std::size_t kRows = 20000;   // ~400 KB: well past the 64 KB sample
    for (std::size_t i = 0; i < kRows; ++i) {
        text << i << ",name" << i << "," << (i % 100) << "\n";
    }
    TempFile file("large.csv", text.str());
    CsvFormat format;
    auto reader = format.open(file.path());
    const TableInfo& info = reader->info();
    EXPECT_EQ(info.rowCount.certainty, Certainty::Estimate);
    ASSERT_TRUE(info.rowCount.value.has_value());
    // The estimate is within 15% of the truth.
    EXPECT_GT(double(*info.rowCount.value), double(kRows + 1) * 0.85);
    EXPECT_LT(double(*info.rowCount.value), double(kRows + 1) * 1.15);
    EXPECT_EQ(*info.columnCount.value, 3u);
    EXPECT_TRUE(info.hasHeaderRow);

    std::size_t seen = 0;
    std::size_t lastProgress = 0;
    std::size_t delivered = reader->readRows(
        [&seen](const std::vector<std::string>&) { ++seen; return true; },
        [&lastProgress](std::size_t bytesRead, std::size_t) { lastProgress = bytesRead; return true; });
    EXPECT_EQ(delivered, kRows + 1);
    EXPECT_EQ(seen, kRows + 1);
    EXPECT_EQ(lastProgress, info.fileBytes);
    EXPECT_EQ(reader->info().rowCount.certainty, Certainty::Exact);
    EXPECT_EQ(*reader->info().rowCount.value, kRows + 1);
}

TEST(CsvRead, CallbackCanStopEarlyAndProgressCanCancel) {
    TempFile file("stop.csv", "a,b\n1,2\n3,4\n5,6\n");
    CsvFormat format;
    auto reader = format.open(file.path());
    std::size_t seen = 0;
    reader->readRows([&seen](const std::vector<std::string>&) { return ++seen < 2; });
    EXPECT_EQ(seen, 2u);

    seen = 0;
    reader->readRows([&seen](const std::vector<std::string>&) { ++seen; return true; },
        [](std::size_t, std::size_t) { return false; });
    EXPECT_GT(seen, 0u);   // the first chunk was already parsed when progress cancelled
}

TEST(CsvRead, RefusesBinaryUtf16AndMissingFiles) {
    TempFile binary("binary.csv", std::string("ab\0cd,ef\n", 9));
    CsvFormat format;
    EXPECT_THROW(format.open(binary.path()), std::runtime_error);
    EXPECT_FALSE(format.recognizes(binary.path()));
    TempFile utf16("utf16.csv", std::string("\xFF\xFE" "a\0,\0b\0\n\0", 10));
    EXPECT_THROW(format.open(utf16.path()), std::runtime_error);
    TempFile missing("does_not_exist.csv", "", false);
    EXPECT_THROW(format.open(missing.path()), std::runtime_error);
}

TEST(CsvRead, RecognizesTextAndEmptyFileHasNoRows) {
    TempFile file("ok.csv", "a,b\n1,2\n");
    CsvFormat format;
    EXPECT_TRUE(format.recognizes(file.path()));
    TempFile empty("empty.csv", "");
    EXPECT_TRUE(readAll(empty.path()).empty());
    EXPECT_FALSE(format.recognizes(empty.path()));
}

TEST(CsvWrite, QuotesOnlyWhenNeeded) {
    EXPECT_EQ(CsvFormat::escapeField("plain", ','), "plain");
    EXPECT_EQ(CsvFormat::escapeField("a,b", ','), "\"a,b\"");
    EXPECT_EQ(CsvFormat::escapeField("a,b", ';'), "a,b");
    EXPECT_EQ(CsvFormat::escapeField("say \"hi\"", ','), "\"say \"\"hi\"\"\"");
    EXPECT_EQ(CsvFormat::escapeField("two\nlines", ','), "\"two\nlines\"");
    EXPECT_EQ(CsvFormat::escapeField(" padded", ','), "\" padded\"");
    EXPECT_EQ(CsvFormat::escapeField("", ','), "");
}

TEST(CsvWrite, RoundTripsAwkwardCellsAndTheModel) {
    TempFile file("roundtrip.csv", "", false);
    std::vector<std::vector<std::string>> cells = {
        {"id", "text", "note"},
        {"1", "a,b", "say \"hi\""},
        {"2", "line1\nline2", ""},
        {"3", " leading", "unicode \xC3\xA9\xE2\x82\xAC"},
    };
    CsvFormat format;
    format.write(file.path(), cells.size(), 3, [&cells](std::size_t r, std::size_t c) { return cells[r][c]; });
    EXPECT_EQ(readAll(file.path()), cells);

    // A slice of the manyrows model survives too.
    ManyRowsModel model;
    TempFile modelFile("model.csv", "", false);
    format.write(modelFile.path(), 500, ManyRowsModel::kColumnCount,
        [&model](std::size_t r, std::size_t c) { return model.cellText(r, c); });
    auto rows = readAll(modelFile.path());
    ASSERT_EQ(rows.size(), 500u);
    for (std::size_t r = 0; r < 500; r += 37) {
        for (std::size_t c = 0; c < ManyRowsModel::kColumnCount; ++c) {
            EXPECT_EQ(rows[r][c], model.cellText(r, c));
        }
    }
}

TEST(CsvWrite, TsvExtensionUsesTabsAndOptionsApply) {
    TempFile tsv("out.tsv", "", false);
    CsvFormat format;
    format.write(tsv.path(), 2, 2, [](std::size_t r, std::size_t c) { return std::to_string(r * 2 + c); });
    std::ifstream in(tsv.path(), std::ios::binary);
    std::stringstream content;
    content << in.rdbuf();
    EXPECT_EQ(content.str(), "0\t1\r\n2\t3\r\n");

    CsvFormat::Options options;
    options.delimiter = ';';
    options.writeBom = true;
    options.lineEnding = "\n";
    TempFile semi("out.csv", "", false);
    CsvFormat(options).write(semi.path(), 1, 2, [](std::size_t, std::size_t c) { return c == 0 ? "a;b" : "c"; });
    std::ifstream in2(semi.path(), std::ios::binary);
    std::stringstream content2;
    content2 << in2.rdbuf();
    EXPECT_EQ(content2.str(), "\xEF\xBB\xBF\"a;b\";c\n");
}

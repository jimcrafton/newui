#include "io/formatregistry.h"
#include "io/xlsxformat.h"
#include "io/xmlparser.h"
#include "io/zipfile.h"
#include "manyrowsloader.h"
#include "xlsx_sample.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace manyrows;
using namespace manyrows::io;

namespace {
    typedef std::vector<std::uint8_t> Bytes;

    class TempFile {
    public:
        TempFile(const std::string& name, const Bytes& content) {
            path_ = (std::filesystem::temp_directory_path() / ("manyrows_xlsx_" + name)).string();
            std::ofstream file(path_, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(content.data()), std::streamsize(content.size()));
        }
        ~TempFile() {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
        const std::string& path() const { return path_; }
    private:
        std::string path_;
    };

    Bytes sampleBytes() { return Bytes(kSampleXlsx, kSampleXlsx + sizeof(kSampleXlsx)); }

    void put16(Bytes& out, unsigned v) { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); }
    void put32(Bytes& out, std::uint32_t v) { put16(out, v & 0xFFFF); put16(out, v >> 16); }

    // A ZIP whose entries are all stored (no compression) - enough for the reader (it ignores the CRC).
    Bytes storedZip(const std::vector<std::pair<std::string, std::string>>& files) {
        Bytes zip;
        std::vector<std::uint32_t> offsets;
        for (const auto& file : files) {
            offsets.push_back(std::uint32_t(zip.size()));
            put32(zip, 0x04034B50u);
            put16(zip, 20); put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0);
            put32(zip, 0);
            put32(zip, std::uint32_t(file.second.size()));
            put32(zip, std::uint32_t(file.second.size()));
            put16(zip, unsigned(file.first.size()));
            put16(zip, 0);
            zip.insert(zip.end(), file.first.begin(), file.first.end());
            zip.insert(zip.end(), file.second.begin(), file.second.end());
        }
        std::uint32_t directoryStart = std::uint32_t(zip.size());
        for (std::size_t i = 0; i < files.size(); ++i) {
            put32(zip, 0x02014B50u);
            put16(zip, 20); put16(zip, 20); put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0);
            put32(zip, 0);
            put32(zip, std::uint32_t(files[i].second.size()));
            put32(zip, std::uint32_t(files[i].second.size()));
            put16(zip, unsigned(files[i].first.size()));
            put16(zip, 0); put16(zip, 0); put16(zip, 0); put16(zip, 0);
            put32(zip, 0);
            put32(zip, offsets[i]);
            zip.insert(zip.end(), files[i].first.begin(), files[i].first.end());
        }
        std::uint32_t directorySize = std::uint32_t(zip.size()) - directoryStart;
        put32(zip, 0x06054B50u);
        put16(zip, 0); put16(zip, 0);
        put16(zip, unsigned(files.size())); put16(zip, unsigned(files.size()));
        put32(zip, directorySize);
        put32(zip, directoryStart);
        put16(zip, 0);
        return zip;
    }

    typedef std::vector<std::vector<std::string>> Rows;

    Rows readAll(const std::string& path, TableInfo* infoOut = nullptr, std::size_t sheet = 0) {
        std::unique_ptr<TableReader> reader = XlsxFormat().open(path);
        if (sheet != 0) {
            reader->selectSheet(sheet);
        }
        if (infoOut != nullptr) {
            *infoOut = reader->info();
        }
        Rows rows;
        reader->readRows([&rows](const std::vector<std::string>& cells) { rows.push_back(cells); return true; });
        return rows;
    }

    const char* kMainNs = "xmlns=\"http://schemas.openxmlformats.org/spreadsheetml/2006/main\"";

    // Minimal workbook parts around one sheet body (for the hand-built cases).
    Bytes minimalWorkbook(const std::string& sheetXml, const std::string& extraEntries = std::string(), bool withRels = true) {
        std::string workbook = std::string("<workbook ") + kMainNs +
            " xmlns:r=\"r\"><sheets><sheet name=\"S\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
        std::string rels = "<Relationships><Relationship Id=\"rId1\" Type=\"http://x/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/></Relationships>";
        std::vector<std::pair<std::string, std::string>> files = {{"xl/workbook.xml", workbook}, {"xl/worksheets/sheet1.xml", sheetXml}};
        if (withRels) {
            files.push_back({"xl/_rels/workbook.xml.rels", rels});
        }
        return storedZip(files);
    }
}

TEST(XmlParser, ElementsAttributesTextAndEntities) {
    XmlParser parser;
    std::string log;
    parser.onStart = [&log](const std::string& name, const XmlParser::Attributes& attributes) {
        log += "<" + name;
        for (const auto& a : attributes) log += " " + a.name + "=" + a.value;
        log += ">";
        return true;
    };
    parser.onEnd = [&log](const std::string& name) { log += "</" + name + ">"; return true; };
    parser.onText = [&log](const std::string& text) { log += "[" + text + "]"; return true; };
    // fed in awkward pieces: splits inside a tag, an attribute value, an entity and text
    const std::string xml = "<?xml version=\"1.0\"?><!-- c --><x:a r:id='7' k=\"a&gt;b\"><b/>T&amp;U &#65;&#x42;<![CDATA[<raw>]]></x:a>";
    for (std::size_t i = 0; i < xml.size(); i += 5) {
        std::string piece = xml.substr(i, 5);
        ASSERT_TRUE(parser.feed(reinterpret_cast<const std::uint8_t*>(piece.data()), piece.size()));
    }
    EXPECT_EQ(log, "<a id=7 k=a>b><b></b>[T&U AB][<raw>]</a>");
}

TEST(XmlParser, CallbackCanStopTheParse) {
    XmlParser parser;
    int starts = 0;
    parser.onStart = [&starts](const std::string&, const XmlParser::Attributes&) { return ++starts < 2; };
    const std::string xml = "<a><b/><c/></a>";
    EXPECT_FALSE(parser.feed(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size()));
    EXPECT_EQ(starts, 2);
    EXPECT_FALSE(parser.feed(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size()));
}

TEST(XlsxFormat, ReferencesAndEscapes) {
    std::size_t column = 0, row = 0;
    ASSERT_TRUE(XlsxFormat::parseReference("A1", column, row));
    EXPECT_EQ(column, 0u);
    EXPECT_EQ(row, 0u);
    ASSERT_TRUE(XlsxFormat::parseReference("AB12", column, row));
    EXPECT_EQ(column, 27u);
    EXPECT_EQ(row, 11u);
    ASSERT_TRUE(XlsxFormat::parseReference("XFD1048576", column, row));
    EXPECT_EQ(column, 16383u);
    EXPECT_EQ(row, 1048575u);
    EXPECT_FALSE(XlsxFormat::parseReference("", column, row));
    EXPECT_FALSE(XlsxFormat::parseReference("12", column, row));
    EXPECT_FALSE(XlsxFormat::parseReference("A", column, row));
    EXPECT_FALSE(XlsxFormat::parseReference("A0", column, row));
    EXPECT_FALSE(XlsxFormat::parseReference("A1B", column, row));

    EXPECT_EQ(XlsxFormat::decodeEscapes("a_x000D_b_x005F_c"), "a\rb_c");
    EXPECT_EQ(XlsxFormat::decodeEscapes("plain_x text"), "plain_x text");
    EXPECT_EQ(XlsxFormat::decodeEscapes("_xZZZZ_"), "_xZZZZ_");
}

TEST(ZipFile, ListsAndExtractsDeflatedAndStoredEntries) {
    TempFile deflated("sample.xlsx", sampleBytes());
    ZipFile zip(deflated.path());
    ASSERT_NE(zip.find("XL/Workbook.xml"), nullptr);   // case-insensitive
    const ZipFile::Entry* sheet = zip.find("xl/worksheets/sheet3.xml");
    ASSERT_NE(sheet, nullptr);
    EXPECT_EQ(sheet->method, 8);
    std::size_t total = 0;
    std::size_t pieces = 0;
    EXPECT_TRUE(zip.extract(*sheet, [&](const std::uint8_t*, std::size_t size, std::size_t) { total += size; ++pieces; return true; }));
    EXPECT_EQ(total, sheet->size);
    EXPECT_GT(pieces, 1u);   // delivered in pieces
    std::size_t first = 0;
    EXPECT_FALSE(zip.extract(*sheet, [&](const std::uint8_t*, std::size_t size, std::size_t) { first = size; return false; }));
    EXPECT_GT(first, 0u);

    TempFile stored("stored.zip", storedZip({{"a.txt", "hello"}}));
    ZipFile plain(stored.path());
    std::string text;
    plain.extract(*plain.find("a.txt"), [&](const std::uint8_t* d, std::size_t n, std::size_t) { text.append(reinterpret_cast<const char*>(d), n); return true; });
    EXPECT_EQ(text, "hello");
}

TEST(ZipFile, RefusesWhatIsNotAZip) {
    TempFile text("notzip.xlsx", Bytes{'h', 'i'});
    EXPECT_THROW(ZipFile(text.path()), std::runtime_error);
    TempFile big("notzip2.xlsx", Bytes(1000, 'x'));
    EXPECT_THROW(ZipFile(big.path()), std::runtime_error);
}

TEST(XlsxFormat, ReadsAnExcelStyleWorkbook) {
    TempFile file("orders.xlsx", sampleBytes());
    TableInfo info;
    Rows rows = readAll(file.path(), &info);
    ASSERT_EQ(info.sheets.size(), 3u);
    EXPECT_EQ(info.sheets[0], "Orders");
    EXPECT_EQ(info.sheets[2], "Big");
    EXPECT_EQ(info.rowCount.certainty, Certainty::Exact);
    EXPECT_EQ(*info.rowCount.value, 5u);
    EXPECT_EQ(*info.columnCount.value, 3u);
    EXPECT_TRUE(info.hasHeaderRow);
    ASSERT_EQ(info.columnTitles.size(), 3u);
    EXPECT_EQ(info.columnTitles[2], "Day");

    ASSERT_EQ(rows.size(), 5u);                    // row 4 is absent in the file: an empty row keeps the numbering
    EXPECT_EQ(rows[0][0], "Item");
    EXPECT_EQ(rows[1][0], "12.5");
    EXPECT_EQ(rows[1][1], "42");
    EXPECT_EQ(rows[1][2], "2023-03-15");           // built-in date format 14
    EXPECT_EQ(rows[2][0], "0.1");
    EXPECT_EQ(rows[2][1], "TRUE");                 // t="b"
    EXPECT_EQ(rows[2][2], "formula text");         // t="str"
    EXPECT_TRUE(rows[3].empty());
    EXPECT_EQ(rows[4][0], "inline");               // t="inlineStr"
    EXPECT_EQ(rows[4][1], "#DIV/0!");              // t="e"
    EXPECT_EQ(rows[4][2], "2023-03-15 12:00:00");  // custom date format 164, with a time
}

TEST(XlsxFormat, SharedStringsRichTextPhoneticAndEscapes) {
    TempFile file("notes.xlsx", sampleBytes());
    Rows rows = readAll(file.path(), nullptr, 1);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0][0], "Tom & Jerry");          // &amp; decoded
    EXPECT_EQ(rows[0][1], "Rich text");            // runs joined, phonetic hint dropped
    EXPECT_EQ(rows[1][0], "a\rb_c");               // _x000D_ and _x005F_
}

TEST(XlsxFormat, ALargeSheetStreamsWithProgressAndCanBeCancelled) {
    TempFile file("big.xlsx", sampleBytes());
    std::unique_ptr<TableReader> reader = XlsxFormat().open(file.path());
    reader->selectSheet(2);
    EXPECT_EQ(*reader->info().rowCount.value, 4000u);
    EXPECT_EQ(*reader->info().columnCount.value, 2u);
    EXPECT_FALSE(reader->info().hasHeaderRow);   // first row is numbers

    std::size_t count = 0;
    std::string lastA, lastB;
    std::size_t reports = 0;
    std::size_t lastRead = 0, lastTotal = 0;
    reader->readRows(
        [&](const std::vector<std::string>& cells) { ++count; lastA = cells[0]; lastB = cells[1]; return true; },
        [&](std::size_t read, std::size_t total) { ++reports; lastRead = read; lastTotal = total; return true; });
    EXPECT_EQ(count, 4000u);
    EXPECT_EQ(lastA, "4000");
    EXPECT_EQ(lastB, "0");   // (4000 % 50) * 4
    EXPECT_GE(reports, 2u);  // at least one mid-way report and the final one
    EXPECT_EQ(lastRead, lastTotal);

    // cancel from the progress callback: far fewer rows come out
    std::unique_ptr<TableReader> again = XlsxFormat().open(file.path());
    again->selectSheet(2);
    std::size_t partial = 0;
    again->readRows([&](const std::vector<std::string>&) { ++partial; return true; },
        [](std::size_t, std::size_t) { return false; });
    EXPECT_GT(partial, 0u);
    EXPECT_LT(partial, 4000u);

    // stop from the row callback
    std::unique_ptr<TableReader> third = XlsxFormat().open(file.path());
    third->selectSheet(2);
    std::size_t seen = 0;
    third->readRows([&](const std::vector<std::string>&) { return ++seen < 10; });
    EXPECT_EQ(seen, 10u);
}

TEST(XlsxFormat, DimensionlessSheetIsCountedByReadingIt) {
    std::string sheet = std::string("<worksheet ") + kMainNs +
        "><sheetData><row r=\"2\"><c r=\"C2\"><v>5</v></c></row><row><c><v>6</v></c><c><v>7</v></c></row></sheetData></worksheet>";
    TempFile file("nodim.xlsx", minimalWorkbook(sheet));
    TableInfo info;
    Rows rows = readAll(file.path(), &info);
    EXPECT_EQ(*info.rowCount.value, 3u);     // row 2 given, then an unnumbered row: 3
    EXPECT_EQ(*info.columnCount.value, 3u);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_TRUE(rows[0].empty());
    EXPECT_EQ(rows[1][2], "5");
    EXPECT_EQ(rows[2][0], "6");              // unnumbered cells follow each other from column A
    EXPECT_EQ(rows[2][1], "7");
}

TEST(XlsxFormat, PrefixedElementNamesAndInlineTextWork) {
    // a writer that prefixes everything with "x:" and keeps text inline (no sharedStrings part)
    std::string sheet = "<x:worksheet xmlns:x=\"main\"><x:dimension ref=\"B1\"/><x:sheetData><x:row r=\"1\">"
        "<x:c r=\"B1\" t=\"inlineStr\"><x:is><x:t>pre &lt;fixed&gt;</x:t></x:is></x:c></x:row></x:sheetData></x:worksheet>";
    TempFile file("prefixed.xlsx", minimalWorkbook(sheet));
    TableInfo info;
    Rows rows = readAll(file.path(), &info);
    EXPECT_EQ(*info.columnCount.value, 2u);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0][1], "pre <fixed>");
}

TEST(XlsxFormat, The1904DateSystemAndAnUnknownStyleIndex) {
    std::string workbook = std::string("<workbook ") + kMainNs +
        " xmlns:r=\"r\"><workbookPr date1904=\"1\"/><sheets><sheet name=\"S\" sheetId=\"1\" r:id=\"rId1\"/></sheets></workbook>";
    std::string rels = "<Relationships><Relationship Id=\"rId1\" Type=\"http://x/relationships/worksheet\" Target=\"worksheets/sheet1.xml\"/>"
                       "<Relationship Id=\"rId2\" Type=\"http://x/relationships/styles\" Target=\"styles.xml\"/></Relationships>";
    std::string styles = std::string("<styleSheet ") + kMainNs + "><cellXfs><xf numFmtId=\"14\"/></cellXfs></styleSheet>";
    std::string sheet = std::string("<worksheet ") + kMainNs +
        "><dimension ref=\"A1:B1\"/><sheetData><row r=\"1\"><c r=\"A1\" s=\"0\"><v>43538</v></c><c r=\"B1\" s=\"99\"><v>43538</v></c></row></sheetData></worksheet>";
    TempFile file("d1904.xlsx", storedZip({{"xl/workbook.xml", workbook}, {"xl/_rels/workbook.xml.rels", rels},
        {"xl/styles.xml", styles}, {"xl/worksheets/sheet1.xml", sheet}}));
    Rows rows = readAll(file.path());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0][0], "2023-03-15");
    EXPECT_EQ(rows[0][1], "43538");   // a style index past the table is just a number
}

TEST(XlsxFormat, RefusesWhatItCannotRead) {
    TempFile notZip("a.xlsx", Bytes(40, 'p'));
    EXPECT_THROW(XlsxFormat().open(notZip.path()), std::runtime_error);

    TempFile notBook("b.xlsx", storedZip({{"readme.txt", "hi"}}));
    try {
        XlsxFormat().open(notBook.path());
        FAIL() << "expected a throw";
    } catch (const std::runtime_error& problem) {
        EXPECT_NE(std::string(problem.what()).find("workbook"), std::string::npos);
    }

    // an encrypted .xlsx is an OLE2 container, not a ZIP
    Bytes ole = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
    ole.resize(600, 0);
    TempFile encrypted("c.xlsx", ole);
    try {
        XlsxFormat().open(encrypted.path());
        FAIL() << "expected a throw";
    } catch (const std::runtime_error& problem) {
        EXPECT_NE(std::string(problem.what()).find("password"), std::string::npos);
    }

    std::string noSheets = std::string("<workbook ") + kMainNs + "><sheets/></workbook>";
    TempFile empty("d.xlsx", storedZip({{"xl/workbook.xml", noSheets}}));
    EXPECT_THROW(XlsxFormat().open(empty.path()), std::runtime_error);
}

TEST(XlsxFormat, RegistryAndLoader) {
    EXPECT_NE(FormatRegistry::forPath("C:\\x\\sales.XLSX"), nullptr);
    EXPECT_NE(FormatRegistry::forPath("C:\\x\\macros.xlsm"), nullptr);
    EXPECT_FALSE(FormatRegistry::forPath("C:\\x\\sales.xlsx")->canWrite());
    std::vector<std::string> writable = FormatRegistry::extensions(true);
    EXPECT_EQ(std::find(writable.begin(), writable.end(), ".xlsx"), writable.end());

    // an .xlsx with a wrong extension is recognised by its content
    TempFile disguised("sample.dat", sampleBytes());
    const TableFormat* format = FormatRegistry::forPath(disguised.path());
    ASSERT_NE(format, nullptr);
    EXPECT_EQ(format->name(), "Excel");

    TempFile file("loader.xlsx", sampleBytes());
    ManyRowsModel model(0);
    std::string sheetName;
    ASSERT_TRUE(ManyRowsLoader::readInto(file.path(), model, nullptr, nullptr, nullptr, nullptr,
        [](const std::vector<std::string>& sheets) { return std::optional<std::size_t>(sheets.size() - 1); }, &sheetName));
    EXPECT_EQ(sheetName, "Big");
    EXPECT_EQ(model.rowCount(), 4000u);
    EXPECT_EQ(model.cellText(3999, 0), "4000");

    // through the worker thread, picking the first sheet
    ManyRowsLoader loader(file.path());
    for (int i = 0; i < 2000 && loader.state() != ManyRowsLoader::State::AwaitingSheet; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(loader.state(), ManyRowsLoader::State::AwaitingSheet);
    ASSERT_EQ(loader.sheetNames().size(), 3u);
    loader.chooseSheet(0);
    for (int i = 0; i < 2000 && !loader.finished(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_EQ(loader.state(), ManyRowsLoader::State::Done);
    EXPECT_EQ(loader.table().columnTitle(1), "Qty");
    EXPECT_EQ(loader.table().rowCount(), 4u);
}

// Timing probe for a big workbook: set MANYROWS_PERF_XLSX to a path (skipped otherwise).
TEST(XlsxFormatPerf, ReadsAFileNamedByTheEnvironment) {
    const char* path = std::getenv("MANYROWS_PERF_XLSX");
    if (path == nullptr) {
        GTEST_SKIP() << "MANYROWS_PERF_XLSX not set";
    }
    auto start = std::chrono::steady_clock::now();
    ManyRowsModel model(0);
    ASSERT_TRUE(ManyRowsLoader::readInto(path, model));
    double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("read %zu rows x %zu columns in %.2f s\n", model.rowCount(), model.columnCount(), seconds);
    EXPECT_GT(model.rowCount(), 0u);
}

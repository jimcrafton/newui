#include "io/formatregistry.h"
#include "io/xlsformat.h"
#include "manyrowsloader.h"

#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <thread>
#include <chrono>

using namespace manyrows;
using namespace manyrows::io;

namespace {
    typedef std::vector<std::uint8_t> Bytes;

    void put16(Bytes& out, unsigned v) { out.push_back(std::uint8_t(v)); out.push_back(std::uint8_t(v >> 8)); }
    void put32(Bytes& out, std::uint32_t v) { put16(out, v & 0xFFFF); put16(out, v >> 16); }
    void putDouble(Bytes& out, double v) {
        std::uint8_t raw[8];
        std::memcpy(raw, &v, 8);
        out.insert(out.end(), raw, raw + 8);
    }

    // One BIFF record.
    Bytes rec(unsigned id, const Bytes& data) {
        Bytes out;
        put16(out, id);
        put16(out, unsigned(data.size()));
        out.insert(out.end(), data.begin(), data.end());
        return out;
    }
    void append(Bytes& to, const Bytes& more) { to.insert(to.end(), more.begin(), more.end()); }

    // A BIFF8 "unicode string" with a 16-bit length (8-bit characters), as SST / LABEL / STRING hold it.
    Bytes ustr(const std::string& text) {
        Bytes out;
        put16(out, unsigned(text.size()));
        out.push_back(0);
        out.insert(out.end(), text.begin(), text.end());
        return out;
    }

    Bytes bof(unsigned type, unsigned version = 0x0600) {
        Bytes d;
        put16(d, version);
        put16(d, type);
        put16(d, 0x0DBB);
        put16(d, 0x07CC);
        put32(d, 0);
        put32(d, 6);
        return rec(0x0809, d);
    }

    Bytes cellHead(unsigned row, unsigned col, unsigned xf) {
        Bytes d;
        put16(d, row);
        put16(d, col);
        put16(d, xf);
        return d;
    }
    Bytes numberCell(unsigned row, unsigned col, double v, unsigned xf = 0) {
        Bytes d = cellHead(row, col, xf);
        putDouble(d, v);
        return rec(0x0203, d);
    }
    Bytes rkCell(unsigned row, unsigned col, std::int32_t integer, unsigned xf = 0) {
        Bytes d = cellHead(row, col, xf);
        put32(d, (std::uint32_t(integer) << 2) | 2);
        return rec(0x027E, d);
    }
    Bytes sstCell(unsigned row, unsigned col, unsigned index) {
        Bytes d = cellHead(row, col, 0);
        put32(d, index);
        return rec(0x00FD, d);
    }
    Bytes dimensions(unsigned rows, unsigned cols) {
        Bytes d;
        put32(d, 0);
        put32(d, rows);
        put16(d, 0);
        put16(d, cols);
        put16(d, 0);
        return rec(0x0200, d);
    }

    struct SheetSpec {
        std::string name;
        Bytes body;   // records between the sheet's BOF and EOF (DIMENSIONS first)
    };

    // A BIFF8 workbook stream: globals (XF 0 General, XF 1 date format 14, XF 2 a custom "yyyy" format),
    // one SST, the sheets. `sstRecords` replaces the generated SST when not empty.
    Bytes workbook(const std::vector<std::string>& strings, const std::vector<SheetSpec>& sheets, bool date1904 = false,
        const Bytes& sstRecords = Bytes()) {
        auto globals = [&](const std::vector<std::size_t>& offsets) {
            Bytes g = bof(0x0005);
            if (date1904) {
                Bytes d;
                put16(d, 1);
                append(g, rec(0x0022, d));
            }
            Bytes fmt;
            put16(fmt, 164);
            append(fmt, ustr("yyyy\\-mm"));
            append(g, rec(0x041E, fmt));
            const unsigned formats[] = {0, 14, 164};
            for (unsigned f : formats) {
                Bytes xf;
                put16(xf, 0);
                put16(xf, f);
                put32(xf, 0);
                put32(xf, 0);
                append(g, rec(0x00E0, xf));
            }
            for (std::size_t i = 0; i < sheets.size(); ++i) {
                Bytes d;
                put32(d, std::uint32_t(offsets[i]));
                d.push_back(0);
                d.push_back(0);
                d.push_back(std::uint8_t(sheets[i].name.size()));
                d.push_back(0);
                d.insert(d.end(), sheets[i].name.begin(), sheets[i].name.end());
                append(g, rec(0x0085, d));
            }
            if (!sstRecords.empty()) {
                append(g, sstRecords);
            } else {
                Bytes sst;
                put32(sst, unsigned(strings.size()));
                put32(sst, unsigned(strings.size()));
                for (const std::string& s : strings) {
                    append(sst, ustr(s));
                }
                append(g, rec(0x00FC, sst));
            }
            append(g, rec(0x000A, Bytes()));
            return g;
        };
        std::vector<std::size_t> offsets(sheets.size(), 0);
        std::size_t size = globals(offsets).size();
        Bytes book;
        std::vector<Bytes> bodies;
        for (std::size_t i = 0; i < sheets.size(); ++i) {
            Bytes b = bof(0x0010);
            append(b, sheets[i].body);
            append(b, rec(0x000A, Bytes()));
            offsets[i] = size;
            size += b.size();
            bodies.push_back(b);
        }
        book = globals(offsets);
        for (const Bytes& b : bodies) {
            append(book, b);
        }
        return book;
    }

    void put32at(Bytes& out, std::size_t at, std::uint32_t v) {
        for (int i = 0; i < 4; ++i) {
            out[at + std::size_t(i)] = std::uint8_t(v >> (8 * i));
        }
    }

    // A compound file holding one stream called "Workbook" - in the mini stream when it is under 4096 bytes.
    Bytes compound(Bytes stream) {
        const bool mini = stream.size() < 4096;
        const std::uint32_t END = 0xFFFFFFFE, FREE = 0xFFFFFFFF, FATSECT = 0xFFFFFFFD;
        std::size_t dataBytes = mini ? ((stream.size() + 63) / 64) * 64 : stream.size();
        std::size_t dataSectors = (dataBytes + 511) / 512;
        std::size_t miniSectors = mini ? 1 : 0;
        std::size_t fatSectors = 1;
        while ((2 + miniSectors + dataSectors + fatSectors + 127) / 128 > fatSectors) {
            ++fatSectors;
        }
        // layout: [FAT sectors][directory][mini FAT][data...]
        const std::size_t dirSector = fatSectors;
        const std::size_t miniFatSector = fatSectors + 1;
        const std::size_t dataStart = fatSectors + 1 + miniSectors;
        const std::size_t total = dataStart + dataSectors;

        std::vector<std::uint32_t> fat(fatSectors * 128, FREE);
        for (std::size_t i = 0; i < fatSectors; ++i) fat[i] = FATSECT;
        fat[dirSector] = END;
        if (mini) fat[miniFatSector] = END;
        for (std::size_t i = 0; i < dataSectors; ++i) fat[dataStart + i] = i + 1 < dataSectors ? std::uint32_t(dataStart + i + 1) : END;

        Bytes file(512, 0);
        const std::uint8_t sig[8] = {0xD0, 0xCF, 0x11, 0xE0, 0xA1, 0xB1, 0x1A, 0xE1};
        std::memcpy(file.data(), sig, 8);
        put32at(file, 0x18, 0x0003003E);   // minor 0x3E, major 3 (little endian pair)
        file[0x1C] = 0xFE; file[0x1D] = 0xFF;
        file[0x1E] = 9; file[0x20] = 6;
        put32at(file, 0x2C, std::uint32_t(fatSectors));
        put32at(file, 0x30, std::uint32_t(dirSector));
        put32at(file, 0x38, 4096);
        put32at(file, 0x3C, mini ? std::uint32_t(miniFatSector) : END);
        put32at(file, 0x40, std::uint32_t(miniSectors));
        put32at(file, 0x44, END);
        for (std::size_t i = 0; i < 109; ++i) put32at(file, 0x4C + i * 4, i < fatSectors ? std::uint32_t(i) : FREE);

        Bytes body(total * 512, 0);
        for (std::size_t i = 0; i < fat.size(); ++i) put32at(body, i * 4, fat[i]);
        // directory: root, Workbook
        auto entry = [&](std::size_t index, const std::string& name, std::uint8_t type, std::uint32_t child, std::uint32_t start, std::uint32_t size) {
            std::size_t at = dirSector * 512 + index * 128;
            for (std::size_t i = 0; i < name.size(); ++i) body[at + i * 2] = std::uint8_t(name[i]);
            body[at + 0x40] = std::uint8_t((name.size() + 1) * 2);
            body[at + 0x42] = type;
            put32at(body, at + 0x44, FREE);
            put32at(body, at + 0x48, FREE);
            put32at(body, at + 0x4C, child);
            put32at(body, at + 0x74, start);
            put32at(body, at + 0x78, size);
        };
        entry(0, "Root Entry", 5, 1, mini ? std::uint32_t(dataStart) : END, mini ? std::uint32_t(dataBytes) : 0);
        entry(1, "Workbook", 2, FREE, mini ? 0 : std::uint32_t(dataStart), std::uint32_t(stream.size()));
        for (std::size_t i = 2; i < 4; ++i) {
            std::size_t at = dirSector * 512 + i * 128;
            put32at(body, at + 0x44, FREE);
            put32at(body, at + 0x48, FREE);
            put32at(body, at + 0x4C, FREE);
        }
        if (mini) {
            std::size_t minis = dataBytes / 64;
            for (std::size_t i = 0; i < 128; ++i) {
                std::uint32_t v = FREE;
                if (i < minis) v = i + 1 < minis ? std::uint32_t(i + 1) : END;
                put32at(body, miniFatSector * 512 + i * 4, v);
            }
        }
        std::memcpy(body.data() + dataStart * 512, stream.data(), stream.size());
        file.insert(file.end(), body.begin(), body.end());
        return file;
    }

    class TempXls {
    public:
        TempXls(const std::string& name, const Bytes& content) {
            path_ = (std::filesystem::temp_directory_path() / ("manyrows_xls_" + name)).string();
            std::ofstream file(path_, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(content.data()), std::streamsize(content.size()));
        }
        ~TempXls() {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
        const std::string& path() const { return path_; }
    private:
        std::string path_;
    };

    typedef std::vector<std::vector<std::string>> Rows;

    Rows readAll(const std::string& path, TableInfo* infoOut = nullptr) {
        std::unique_ptr<TableReader> reader = XlsFormat().open(path);
        if (infoOut != nullptr) {
            *infoOut = reader->info();
        }
        Rows rows;
        reader->readRows([&rows](const std::vector<std::string>& cells) { rows.push_back(cells); return true; });
        return rows;
    }

    SheetSpec ordersSheet() {
        // Header row (SST 0..2), then two data rows: a number, a RK integer, a date and a text.
        SheetSpec sheet;
        sheet.name = "Orders";
        append(sheet.body, dimensions(3, 3));
        append(sheet.body, sstCell(0, 0, 0));
        append(sheet.body, sstCell(0, 1, 1));
        append(sheet.body, sstCell(0, 2, 2));
        append(sheet.body, numberCell(1, 0, 12.5));
        append(sheet.body, rkCell(1, 1, 42));
        append(sheet.body, numberCell(1, 2, 45000.0, 1));   // XF 1 = built-in date format
        append(sheet.body, numberCell(2, 0, 0.1));
        append(sheet.body, rkCell(2, 1, -7));
        append(sheet.body, sstCell(2, 2, 3));
        return sheet;
    }
}

TEST(XlsFormat, NumberAndDateText) {
    EXPECT_EQ(XlsFormat::numberText(42.0), "42");
    EXPECT_EQ(XlsFormat::numberText(-0.0), "0");
    EXPECT_EQ(XlsFormat::numberText(0.1), "0.1");
    EXPECT_EQ(XlsFormat::numberText(1.0 / 3.0), "0.333333333333333");
    EXPECT_EQ(XlsFormat::dateText(45000.0, false), "2023-03-15");
    EXPECT_EQ(XlsFormat::dateText(45000.5, false), "2023-03-15 12:00:00");
    EXPECT_EQ(XlsFormat::dateText(0.75, false), "18:00:00");
    EXPECT_EQ(XlsFormat::dateText(1.0, false), "1900-01-01");
    EXPECT_EQ(XlsFormat::dateText(61.0, false), "1900-03-01");
    EXPECT_EQ(XlsFormat::dateText(60.0, false), "1900-02-29");   // Excel's famous phantom day
    EXPECT_EQ(XlsFormat::dateText(0.0, true), "1904-01-01");
    EXPECT_EQ(XlsFormat::dateText(43538.0, true), "2023-03-15");
}

TEST(XlsFormat, DetectsDateFormats) {
    EXPECT_TRUE(XlsFormat::isDateFormat("yyyy-mm-dd"));
    EXPECT_TRUE(XlsFormat::isDateFormat("h:mm AM/PM"));
    EXPECT_TRUE(XlsFormat::isDateFormat("[h]:mm:ss"));
    EXPECT_TRUE(XlsFormat::isDateFormat("[$-409]d-mmm-yy"));
    EXPECT_FALSE(XlsFormat::isDateFormat("0.00"));
    EXPECT_FALSE(XlsFormat::isDateFormat("#,##0.00"));
    EXPECT_FALSE(XlsFormat::isDateFormat("General"));
    EXPECT_FALSE(XlsFormat::isDateFormat("0.00E+00"));
    EXPECT_FALSE(XlsFormat::isDateFormat("\"days\" 0"));   // letters inside quotes are text
    EXPECT_FALSE(XlsFormat::isDateFormat("[Red]0.0"));
}

TEST(XlsFormat, ReadsSheetCellsHeaderAndExactDimensions) {
    TempXls file("orders.xls", compound(workbook({"Item", "Qty", "Day", "later"}, {ordersSheet()})));   // small: mini stream
    TableInfo info;
    Rows rows = readAll(file.path(), &info);
    EXPECT_EQ(info.rowCount.certainty, Certainty::Exact);
    EXPECT_EQ(*info.rowCount.value, 3u);
    EXPECT_EQ(info.columnCount.certainty, Certainty::Exact);
    EXPECT_EQ(*info.columnCount.value, 3u);
    EXPECT_TRUE(info.hasHeaderRow);
    ASSERT_EQ(info.columnTitles.size(), 3u);
    EXPECT_EQ(info.columnTitles[1], "Qty");
    ASSERT_EQ(info.sheets.size(), 1u);
    EXPECT_EQ(info.sheets[0], "Orders");

    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0][0], "Item");
    EXPECT_EQ(rows[1][0], "12.5");
    EXPECT_EQ(rows[1][1], "42");
    EXPECT_EQ(rows[1][2], "2023-03-15");
    EXPECT_EQ(rows[2][0], "0.1");
    EXPECT_EQ(rows[2][1], "-7");
    EXPECT_EQ(rows[2][2], "later");
}

TEST(XlsFormat, ALargeWorkbookUsesRegularSectors) {
    SheetSpec sheet;
    sheet.name = "Big";
    const unsigned rows = 3000;
    append(sheet.body, dimensions(rows, 2));
    for (unsigned r = 0; r < rows; ++r) {
        append(sheet.body, numberCell(r, 0, r * 1.5));
        append(sheet.body, rkCell(r, 1, std::int32_t(r)));
    }
    TempXls file("big.xls", compound(workbook({}, {sheet})));
    TableInfo info;
    Rows all = readAll(file.path(), &info);
    ASSERT_EQ(all.size(), rows);
    EXPECT_FALSE(info.hasHeaderRow);
    EXPECT_EQ(all[2999][0], "4498.5");
    EXPECT_EQ(all[2999][1], "2999");
}

TEST(XlsFormat, EmptyRowsKeepTheNumbering) {
    SheetSpec sheet;
    sheet.name = "Gaps";
    append(sheet.body, dimensions(5, 2));
    append(sheet.body, numberCell(1, 1, 5));
    append(sheet.body, numberCell(4, 0, 9));
    TempXls file("gaps.xls", compound(workbook({}, {sheet})));
    Rows rows = readAll(file.path());
    ASSERT_EQ(rows.size(), 5u);
    EXPECT_TRUE(rows[0].empty());
    EXPECT_EQ(rows[1][1], "5");
    EXPECT_TRUE(rows[2].empty());
    EXPECT_EQ(rows[4][0], "9");
}

TEST(XlsFormat, MulRkBooleansErrorsAndFormulas) {
    SheetSpec sheet;
    sheet.name = "Mixed";
    append(sheet.body, dimensions(1, 8));
    Bytes mul;
    put16(mul, 0);
    put16(mul, 0);   // first column
    for (std::int32_t v : {10, 20}) {
        put16(mul, 0);
        put32(mul, (std::uint32_t(v) << 2) | 2);
    }
    put16(mul, 1);   // last column
    append(sheet.body, rec(0x00BD, mul));
    Bytes boolErr = cellHead(0, 2, 0);
    boolErr.push_back(1); boolErr.push_back(0);
    append(sheet.body, rec(0x0205, boolErr));
    Bytes err = cellHead(0, 3, 0);
    err.push_back(0x07); err.push_back(1);
    append(sheet.body, rec(0x0205, err));
    Bytes formulaNumber = cellHead(0, 4, 0);
    putDouble(formulaNumber, 2.5);
    put16(formulaNumber, 0);
    put32(formulaNumber, 0);
    append(sheet.body, rec(0x0006, formulaNumber));
    Bytes formulaText = cellHead(0, 5, 0);
    const std::uint8_t textResult[8] = {0, 0, 0, 0, 0, 0, 0xFF, 0xFF};
    formulaText.insert(formulaText.end(), textResult, textResult + 8);
    put16(formulaText, 0);
    put32(formulaText, 0);
    append(sheet.body, rec(0x0006, formulaText));
    append(sheet.body, rec(0x0207, ustr("computed")));
    Bytes label = cellHead(0, 6, 0);
    append(label, ustr("direct"));
    append(sheet.body, rec(0x0204, label));
    TempXls file("mixed.xls", compound(workbook({}, {sheet})));
    Rows rows = readAll(file.path());
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_EQ(rows[0].size(), 7u);
    EXPECT_EQ(rows[0][0], "10");
    EXPECT_EQ(rows[0][1], "20");
    EXPECT_EQ(rows[0][2], "TRUE");
    EXPECT_EQ(rows[0][3], "#DIV/0!");
    EXPECT_EQ(rows[0][4], "2.5");
    EXPECT_EQ(rows[0][5], "computed");
    EXPECT_EQ(rows[0][6], "direct");
}

TEST(XlsFormat, SharedStringsSpanningAContinueRecordSwitchWidth) {
    // "Héllo wörld": the first record holds "Hél" 8-bit, the CONTINUE (flag 1) the rest as UTF-16.
    Bytes first;
    put32(first, 1);
    put32(first, 1);
    put16(first, 11);
    first.push_back(0);
    first.push_back('H'); first.push_back(0xE9); first.push_back('l');
    Bytes more;
    more.push_back(1);   // the rest is 16-bit
    for (char16_t c : std::u16string(u"lo w\u00F6rld")) {
        put16(more, c);
    }
    Bytes sstRecords = rec(0x00FC, first);
    append(sstRecords, rec(0x003C, more));

    SheetSpec sheet;
    sheet.name = "S";
    append(sheet.body, dimensions(1, 1));
    append(sheet.body, sstCell(0, 0, 0));
    TempXls file("continue.xls", compound(workbook({}, {sheet}, false, sstRecords)));
    Rows rows = readAll(file.path());
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0][0], "H\xC3\xA9llo w\xC3\xB6rld");   // UTF-8
}

TEST(XlsFormat, CustomDateFormatAnd1904System) {
    SheetSpec sheet;
    sheet.name = "D";
    append(sheet.body, dimensions(1, 2));
    append(sheet.body, numberCell(0, 0, 43538.0, 2));   // XF 2 = custom "yyyy\-mm"
    append(sheet.body, numberCell(0, 1, 43538.0, 0));   // General: stays a number
    TempXls file("d1904.xls", compound(workbook({}, {sheet}, true)));
    Rows rows = readAll(file.path());
    EXPECT_EQ(rows[0][0], "2023-03-15");
    EXPECT_EQ(rows[0][1], "43538");
}

TEST(XlsFormat, SeveralSheetsCanBeSelected) {
    SheetSpec a;
    a.name = "First";
    append(a.body, dimensions(1, 1));
    append(a.body, numberCell(0, 0, 1));
    SheetSpec b;
    b.name = "Second";
    append(b.body, dimensions(2, 1));
    append(b.body, numberCell(0, 0, 2));
    append(b.body, numberCell(1, 0, 3));
    TempXls file("two.xls", compound(workbook({}, {a, b})));
    std::unique_ptr<TableReader> reader = XlsFormat().open(file.path());
    ASSERT_EQ(reader->info().sheets.size(), 2u);
    EXPECT_EQ(*reader->info().rowCount.value, 1u);
    reader->selectSheet(1);
    EXPECT_EQ(*reader->info().rowCount.value, 2u);
    Rows rows;
    reader->readRows([&rows](const std::vector<std::string>& cells) { rows.push_back(cells); return true; });
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[1][0], "3");
    EXPECT_THROW(reader->selectSheet(2), std::runtime_error);
}

TEST(XlsFormat, RefusesWhatItCannotRead) {
    TempXls text("notxls.xls", Bytes{'a', ',', 'b', '\n'});
    EXPECT_THROW(XlsFormat().open(text.path()), std::runtime_error);

    Bytes protectedBook = bof(0x0005);
    append(protectedBook, rec(0x002F, Bytes{1, 0, 1, 0, 1, 0}));
    append(protectedBook, rec(0x000A, Bytes()));
    TempXls locked("locked.xls", compound(protectedBook));
    try {
        XlsFormat().open(locked.path());
        FAIL() << "expected a throw";
    } catch (const std::runtime_error& problem) {
        EXPECT_NE(std::string(problem.what()).find("password"), std::string::npos);
    }

    Bytes old = bof(0x0005, 0x0500);
    append(old, rec(0x000A, Bytes()));
    TempXls biff5("biff5.xls", compound(old));
    EXPECT_THROW(XlsFormat().open(biff5.path()), std::runtime_error);
}

TEST(XlsFormat, RegistryAndLoaderHandleXlsFilesButCannotWriteThem) {
    EXPECT_NE(FormatRegistry::forPath("C:\\x\\sales.XLS"), nullptr);
    EXPECT_FALSE(FormatRegistry::forPath("C:\\x\\sales.xls")->canWrite());
    std::vector<std::string> all = FormatRegistry::extensions();
    EXPECT_NE(std::find(all.begin(), all.end(), ".xls"), all.end());
    std::vector<std::string> writable = FormatRegistry::extensions(true);
    EXPECT_EQ(std::find(writable.begin(), writable.end(), ".xls"), writable.end());

    TempXls file("loader.xls", compound(workbook({"Item", "Qty", "Day", "later"}, {ordersSheet()})));
    ManyRowsModel model(0);
    ASSERT_TRUE(ManyRowsLoader::readInto(file.path(), model));
    EXPECT_EQ(model.rowCount(), 2u);   // the header became titles
    EXPECT_EQ(model.columnTitle(0), "Item");
    EXPECT_EQ(model.cellText(0, 2), "2023-03-15");
    EXPECT_EQ(model.columnTypeOf(1), ManyRowsModel::ColumnType::Integer);
}

namespace {
    // A two-sheet workbook file.
    Bytes twoSheetBook() {
        SheetSpec a;
        a.name = "First";
        append(a.body, dimensions(1, 1));
        append(a.body, numberCell(0, 0, 1));
        SheetSpec b;
        b.name = "Second";
        append(b.body, dimensions(2, 1));
        append(b.body, numberCell(0, 0, 2));
        append(b.body, numberCell(1, 0, 3));
        return compound(workbook({}, {a, b}));
    }

    bool waitFor(const ManyRowsLoader& loader, ManyRowsLoader::State wanted) {
        for (int i = 0; i < 2000 && loader.state() != wanted; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return loader.state() == wanted;
    }
}

TEST(ManyRowsLoaderSheets, PausesForTheSheetChoiceThenReadsThatSheet) {
    TempXls file("pick.xls", twoSheetBook());
    ManyRowsLoader loader(file.path());
    ASSERT_TRUE(waitFor(loader, ManyRowsLoader::State::AwaitingSheet));
    EXPECT_FALSE(loader.finished());
    ASSERT_EQ(loader.sheetNames().size(), 2u);
    EXPECT_EQ(loader.sheetNames()[1], "Second");
    loader.chooseSheet(1);
    ASSERT_TRUE(waitFor(loader, ManyRowsLoader::State::Done));
    EXPECT_EQ(loader.sheetName(), "Second");
    EXPECT_EQ(loader.table().rowCount(), 2u);
    EXPECT_EQ(loader.table().cellText(1, 0), "3");
}

TEST(ManyRowsLoaderSheets, CancellingTheChoiceCancelsTheLoad) {
    TempXls file("pickcancel.xls", twoSheetBook());
    ManyRowsLoader loader(file.path());
    ASSERT_TRUE(waitFor(loader, ManyRowsLoader::State::AwaitingSheet));
    loader.cancel();
    ASSERT_TRUE(waitFor(loader, ManyRowsLoader::State::Cancelled));
    EXPECT_EQ(loader.table().rowCount(), 0u);
}

TEST(ManyRowsLoaderSheets, DestroyingALoaderThatWaitsForTheChoiceDoesNotHang) {
    TempXls file("pickdrop.xls", twoSheetBook());
    {
        ManyRowsLoader loader(file.path());
        ASSERT_TRUE(waitFor(loader, ManyRowsLoader::State::AwaitingSheet));
    }
    SUCCEED();
}

TEST(ManyRowsLoaderSheets, ASingleSheetFileNeverAsks) {
    SheetSpec only;
    only.name = "Only";
    append(only.body, dimensions(1, 1));
    append(only.body, numberCell(0, 0, 7));
    TempXls file("single.xls", compound(workbook({}, {only})));
    ManyRowsLoader loader(file.path());
    ASSERT_TRUE(waitFor(loader, ManyRowsLoader::State::Done));
    EXPECT_EQ(loader.table().cellText(0, 0), "7");
}

TEST(ManyRowsLoaderSheets, TheSynchronousReadTakesTheFirstSheet) {
    TempXls file("sync.xls", twoSheetBook());
    ManyRowsModel model(0);
    ASSERT_TRUE(ManyRowsLoader::readInto(file.path(), model));
    EXPECT_EQ(model.rowCount(), 1u);
}

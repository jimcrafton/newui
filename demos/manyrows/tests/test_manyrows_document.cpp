#include "manyrowsdocument.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace manyrows;

namespace {
    class TempFile {
    public:
        explicit TempFile(const std::string& name, const std::string& content = std::string(), bool write = true) {
            path_ = (std::filesystem::temp_directory_path() / ("manyrows_doc_" + name)).string();
            if (write) {
                std::ofstream file(path_, std::ios::binary | std::ios::trunc);
                file << content;
            }
        }
        ~TempFile() {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
            std::filesystem::remove(path_ + ".bak", ignored);
        }
        const std::string& path() const { return path_; }
    private:
        std::string path_;
    };

    std::string slurp(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream content;
        content << in.rdbuf();
        return content.str();
    }
}

TEST(ManyRowsDocument, LoadsRowsAndUsesTheHeaderAsColumnTitles) {
    TempFile file("people.csv", "name,age,city\nAda,36,London\nLin,41,Taipei\nSam,29,\n");
    ManyRowsModel model;
    ManyRowsDocument document(model);
    ASSERT_TRUE(document.load(file.path()));
    EXPECT_FALSE(model.isGenerated());
    EXPECT_EQ(model.rowCount(), 3u);       // the header is not a data row
    EXPECT_EQ(model.columnCount(), 3u);
    EXPECT_EQ(model.columnTitle(1), "age");
    EXPECT_TRUE(model.hasTitles());
    EXPECT_EQ(model.cellText(0, 0), "Ada");
    EXPECT_EQ(model.cellText(2, 2), "");
    EXPECT_EQ(model.cellText(1, 1), "41");
    EXPECT_EQ(model.columnTypeOf(1), ManyRowsModel::ColumnType::Integer);   // inferred
    EXPECT_EQ(model.columnTypeOf(0), ManyRowsModel::ColumnType::Text);
    EXPECT_FALSE(document.isModified());
    EXPECT_EQ(document.filePath(), file.path());
}

TEST(ManyRowsDocument, HeaderlessFileKeepsEveryRowAndHasNoTitles) {
    TempFile file("numbers.csv", "1,2.5\n3,4.5\n");
    ManyRowsModel model;
    ManyRowsDocument document(model);
    ASSERT_TRUE(document.load(file.path()));
    EXPECT_EQ(model.rowCount(), 2u);
    EXPECT_FALSE(model.hasTitles());
    EXPECT_EQ(model.columnTitle(0), "");
    EXPECT_EQ(model.columnTypeOf(1), ManyRowsModel::ColumnType::Number);
}

TEST(ManyRowsDocument, RaggedRowsReadAsBlankPastTheirEnd) {
    TempFile file("ragged.csv", "a,b,c\n1,2,3\n4,5\n6,7,8,9\n");
    ManyRowsModel model;
    ManyRowsDocument document(model);
    ASSERT_TRUE(document.load(file.path()));
    EXPECT_EQ(model.columnCount(), 4u);     // a small file is scanned whole: the widest row sets the width
    EXPECT_EQ(model.cellText(1, 2), "");
    EXPECT_EQ(model.cellText(2, 3), "9");
    EXPECT_EQ(model.columnTitle(3), "");    // the header only names three
}

TEST(ManyRowsDocument, EditsAreSavedAndAReloadSeesThem) {
    TempFile file("edit.csv", "k,v\nx,1\ny,2\n");
    ManyRowsModel model;
    ManyRowsDocument document(model);
    ASSERT_TRUE(document.load(file.path()));
    model.setValueAt(1, 0, std::string("two, with comma"));
    document.markModified();
    EXPECT_TRUE(document.isModified());

    ASSERT_TRUE(document.save());
    EXPECT_FALSE(document.isModified());
    EXPECT_EQ(slurp(file.path()), "k,v\r\nx,1\r\n\"two, with comma\",2\r\n");

    ManyRowsModel again;
    ManyRowsDocument second(again);
    ASSERT_TRUE(second.load(file.path()));
    EXPECT_EQ(again.cellText(1, 0), "two, with comma");
    EXPECT_EQ(again.editedCellCount(), 0u);   // an edit of the old session is now plain content
}

TEST(ManyRowsDocument, MarkUnmodifiedClearsTheFlagAndNotifies) {
    ManyRowsModel model;
    ManyRowsDocument document(model);
    int changes = 0;
    document.onModifiedChanged += [&changes](newui::Document&) { ++changes; return newui::SyncReturn::Handled; };
    document.markModified();
    document.markUnmodified();
    document.markUnmodified();   // already clean: no second notification
    EXPECT_FALSE(document.isModified());
    EXPECT_EQ(changes, 2);
}

TEST(ManyRowsDocument, SavesTheGeneratedTableWithItsTitlesAndNoHeaderForABlankSheet) {
    ManyRowsModel model(5);
    ManyRowsDocument document(model);
    TempFile out("generated.csv", "", false);
    ASSERT_TRUE(document.save(out.path()));
    std::string text = slurp(out.path());
    EXPECT_EQ(text.rfind("Order,Item,Category,Region,Status,Qty,Price,Total,Date,Rating\r\n", 0), 0u);
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 6);   // header + 5 rows

    model.makeBlank(2, 3);
    TempFile blank("blank.csv", "", false);
    ASSERT_TRUE(document.save(blank.path()));
    EXPECT_EQ(slurp(blank.path()), ",,\r\n,,\r\n");
}

TEST(ManyRowsDocument, RoundTripsTheGeneratedTableThroughAFile) {
    ManyRowsModel model(2000);
    ManyRowsDocument document(model);
    TempFile file("roundtrip.csv", "", false);
    ASSERT_TRUE(document.save(file.path()));

    ManyRowsModel loaded;
    ManyRowsDocument second(loaded);
    ASSERT_TRUE(second.load(file.path()));
    EXPECT_EQ(loaded.rowCount(), 2000u);
    EXPECT_EQ(loaded.columnCount(), ManyRowsModel::kColumnCount);
    EXPECT_EQ(loaded.columnTitle(6), "Price");
    for (std::size_t r = 0; r < 2000; r += 123) {
        for (std::size_t c = 0; c < ManyRowsModel::kColumnCount; ++c) {
            EXPECT_EQ(loaded.cellText(r, c), model.cellText(r, c)) << r << "," << c;
        }
    }
    EXPECT_EQ(loaded.columnTypeOf(6), ManyRowsModel::ColumnType::Number);
}

TEST(ManyRowsDocument, FailedLoadsReportWhyAndLeaveTheModelAlone) {
    ManyRowsModel model;
    ManyRowsDocument document(model);
    TempFile missing("nope.csv", "", false);
    EXPECT_FALSE(document.load(missing.path()));
    EXPECT_FALSE(document.lastError().empty());
    EXPECT_TRUE(model.isGenerated());
    EXPECT_EQ(model.rowCount(), 1000000u);

    TempFile unknown("data.xyz", "", false);
    EXPECT_FALSE(document.load(unknown.path()));
    EXPECT_TRUE(model.isGenerated());
}

TEST(ManyRowsModelLoaded, MakeBlankAndMakeGeneratedSwitchModes) {
    ManyRowsModel model;
    int fired = 0;
    model.onChanged.add([&fired](newui::Model&) { ++fired; return newui::SyncReturn::Handled; });
    model.makeBlank(4, 3);
    EXPECT_FALSE(model.isGenerated());
    EXPECT_EQ(model.rowCount(), 4u);
    EXPECT_EQ(model.columnCount(), 3u);
    EXPECT_EQ(model.cellText(3, 2), "");
    EXPECT_EQ(fired, 1);

    model.setValueAt(3, 2, std::string("x"));   // columns beyond 15 bits of the key must still work
    EXPECT_EQ(model.cellText(3, 2), "x");

    model.makeGenerated();
    EXPECT_TRUE(model.isGenerated());
    EXPECT_EQ(model.rowCount(), 1000000u);
    EXPECT_EQ(model.editedCellCount(), 0u);
}

TEST(ManyRowsModelLoaded, ReplacingContentClearsOldEdits) {
    ManyRowsModel model;
    model.setValueAt(1, 1, std::string("edit"));
    model.beginLoad(2, {});
    model.appendRow({"a", "b"});
    model.endLoad();
    EXPECT_EQ(model.editedCellCount(), 0u);
    EXPECT_EQ(model.cellText(0, 1), "b");
    EXPECT_THROW(model.beginLoad(ManyRowsModel::kMaxColumns + 1, {}), std::runtime_error);
}

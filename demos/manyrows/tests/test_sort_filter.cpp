#include "manyrowssaver.h"
#include "spreadsheeteditor.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace manyrows;
using Op = FilterRule::Op;

namespace {
    // Five people: Qty is numeric (one blank), Day is yyyy-mm-dd text.
    //   0 bob 10 2024-03-01   1 Alice 2 2024-01-15   2 carol - 2024-02-10   3 dave 30 2024-01-15   4 Eve 2 2024-05-05
    void fill(ManyRowsModel& model) {
        model.beginLoad(3, {"Name", "Qty", "Day"});
        model.appendRow({"bob", "10", "2024-03-01"});
        model.appendRow({"Alice", "2", "2024-01-15"});
        model.appendRow({"carol", "", "2024-02-10"});
        model.appendRow({"dave", "30", "2024-01-15"});
        model.appendRow({"Eve", "2", "2024-05-05"});
        model.endLoad();
    }

    std::vector<std::size_t> sources(const ManyRowsModel& model) {
        std::vector<std::size_t> rows;
        for (std::size_t d = 0; d < model.rowCount(); ++d) {
            rows.push_back(model.sourceRow(d));
        }
        return rows;
    }

    using Rows = std::vector<std::size_t>;

    std::string names(const ManyRowsModel& model) {
        std::string text;
        for (std::size_t d = 0; d < model.rowCount(); ++d) {
            text += (d ? "," : "") + model.cellText(d, 0);
        }
        return text;
    }
}

TEST(FilterRule, TextIsCaseInsensitiveAndNumbersAreNumeric) {
    EXPECT_TRUE(FilterRule(0, Op::Equals, "north").matches("North", false));
    EXPECT_FALSE(FilterRule(0, Op::Equals, "north").matches("Northern", false));
    EXPECT_TRUE(FilterRule(0, Op::Contains, "ORT").matches("North", false));
    EXPECT_TRUE(FilterRule(0, Op::NotContains, "x").matches("North", false));
    EXPECT_TRUE(FilterRule(0, Op::Equals, "5").matches("5.00", true));    // numeric column: 5 equals 5.00
    EXPECT_FALSE(FilterRule(0, Op::Equals, "5").matches("5.00", false));  // text column: it does not
    EXPECT_TRUE(FilterRule(0, Op::Greater, "9").matches("10", true));     // 10 > 9 numerically (text order says no)
    EXPECT_FALSE(FilterRule(0, Op::Greater, "9").matches("10", false));
    EXPECT_TRUE(FilterRule(0, Op::LessOrEqual, "10").matches("10", true));
    EXPECT_TRUE(FilterRule(0, Op::GreaterOrEqual, "2024-02-01").matches("2024-03-01", false));
}

TEST(FilterRule, BlanksNeverCompareButCanBeTested) {
    EXPECT_TRUE(FilterRule(0, Op::Blank).matches("", true));
    EXPECT_TRUE(FilterRule(0, Op::Blank).matches("  ", true));
    EXPECT_TRUE(FilterRule(0, Op::NotBlank).matches("0", true));
    EXPECT_FALSE(FilterRule(0, Op::Less, "5").matches("", true));
    EXPECT_FALSE(FilterRule(0, Op::Greater, "5").matches("", true));
    EXPECT_TRUE(FilterRule(0, Op::NotEquals, "5").matches("", true));
    EXPECT_TRUE(FilterRule(0, Op::Equals, "").matches("", false));
    EXPECT_EQ(FilterRule(2, Op::Greater, "50").describe("Qty"), "Qty > 50");
    EXPECT_EQ(FilterRule(2, Op::Blank).describe("Qty"), "Qty is blank");
}

TEST(RowIndex, MapsDisplayToStoredRowsAndBack) {
    RowIndex index;
    EXPECT_EQ(index.rowCount(5), 5u);
    EXPECT_EQ(index.sourceRow(3), 3u);
    index.setOrder({4, 3, 2, 1, 0});
    EXPECT_EQ(index.sourceRow(0), 4u);
    EXPECT_EQ(index.displayRowOf(4, 5), 0u);
    index.setView({3, 1});   // filtered: shows stored rows 3 and 1
    EXPECT_EQ(index.rowCount(5), 2u);
    EXPECT_EQ(index.sourceRow(1), 1u);
    EXPECT_EQ(index.orderedSourceRow(0), 4u);   // saving ignores the filter
    EXPECT_EQ(index.displayRowOf(1, 5), 1u);
    EXPECT_FALSE(index.displayRowOf(0, 5).has_value());   // hidden
    EXPECT_FALSE(index.displayRowOf(9, 5).has_value());   // out of range
    index.reset();
    EXPECT_EQ(index.rowCount(5), 5u);
    EXPECT_EQ(index.displayRowOf(2, 5), 2u);
}

TEST(ManyRowsSort, NumbersAscendingAndDescendingWithBlanksLast) {
    ManyRowsModel model(0);
    fill(model);
    model.sortBy(1, true);
    EXPECT_EQ(sources(model), (Rows{1, 4, 0, 3, 2}));   // ties (Alice, Eve) keep their order; the blank is last
    EXPECT_EQ(names(model), "Alice,Eve,bob,dave,carol");
    model.sortBy(1, false);
    EXPECT_EQ(sources(model), (Rows{3, 0, 1, 4, 2}));   // still last
    EXPECT_TRUE(model.isSorted());
}

TEST(ManyRowsSort, TextIgnoresCaseAndSecondSortKeepsTheFirstAsTieBreak) {
    ManyRowsModel model(0);
    fill(model);
    model.sortBy(0, true);
    EXPECT_EQ(names(model), "Alice,bob,carol,dave,Eve");
    model.sortBy(2, true);   // Day: Alice and dave tie on 2024-01-15 and stay in name order
    EXPECT_EQ(sources(model), (Rows{1, 3, 2, 0, 4}));
    model.clearSort();
    EXPECT_EQ(sources(model), (Rows{0, 1, 2, 3, 4}));
    EXPECT_FALSE(model.isSorted());
}

TEST(ManyRowsSort, ColumnTitleShowsAnArrowButTitleTextStaysPlain) {
    ManyRowsModel model(0);
    fill(model);
    model.sortBy(1, true);
    EXPECT_EQ(model.titleText(1), "Qty");
    EXPECT_EQ(model.columnTitle(1), "Qty \xE2\x96\xB2");
    EXPECT_EQ(model.columnTitle(0), "Name");
    model.sortBy(1, false);
    EXPECT_EQ(model.columnTitle(1), "Qty \xE2\x96\xBC");
    EXPECT_THROW(model.sortBy(9, true), std::runtime_error);
}

TEST(ManyRowsFilter, RulesAreAnded) {
    ManyRowsModel model(0);
    fill(model);
    model.setFilters({FilterRule(1, Op::Greater, "5")});
    EXPECT_EQ(sources(model), (Rows{0, 3}));
    EXPECT_TRUE(model.isFiltered());
    model.setFilters({FilterRule(1, Op::Equals, "2"), FilterRule(0, Op::Contains, "e")});
    EXPECT_EQ(sources(model), (Rows{1, 4}));   // Alice and Eve both have 2 and an e
    model.setFilters({FilterRule(1, Op::Blank)});
    EXPECT_EQ(sources(model), (Rows{2}));
    model.setFilters({FilterRule(1, Op::NotBlank), FilterRule(2, Op::Less, "2024-02-01")});
    EXPECT_EQ(sources(model), (Rows{1, 3}));
    model.clearFilters();
    EXPECT_EQ(model.rowCount(), 5u);
    EXPECT_THROW(model.setFilters({FilterRule(7, Op::Blank)}), std::runtime_error);
    EXPECT_EQ(model.rowCount(), 5u);   // a bad rule changes nothing
}

TEST(ManyRowsFilter, FilterAndSortCompose) {
    ManyRowsModel model(0);
    fill(model);
    model.setFilters({FilterRule(1, Op::NotBlank)});
    model.sortBy(0, false);   // sorting re-applies the filter in the new order
    EXPECT_EQ(names(model), "Eve,dave,bob,Alice");
    model.setFilters({FilterRule(1, Op::NotBlank), FilterRule(1, Op::LessOrEqual, "10")});
    EXPECT_EQ(names(model), "Eve,bob,Alice");   // filtering keeps the sort
    model.clearSort();
    EXPECT_EQ(names(model), "bob,Alice,Eve");
    EXPECT_EQ(model.sourceRowCount(), 5u);
}

TEST(ManyRowsFilter, ReadingAndWritingByDisplayRowFollowsTheView) {
    ManyRowsModel model(0);
    fill(model);
    model.setFilters({FilterRule(1, Op::Greater, "5")});
    EXPECT_EQ(model.cellText(1, 0), "dave");
    EXPECT_EQ(model.cellText(2, 0), "");   // past the visible rows
    model.setValueAt(1, 0, std::string("DAVE"));
    model.clearFilters();
    EXPECT_EQ(model.cellText(3, 0), "DAVE");   // the edit landed on the stored row
}

TEST(SpreadsheetSortedEdits, EditsAndUndoSurviveResorting) {
    ManyRowsModel model(0);
    fill(model);
    SpreadsheetEditor editor(model);
    model.sortBy(1, true);                    // Alice, Eve, bob, dave, carol
    ASSERT_TRUE(editor.setCell(0, 0, "Zed"));  // display row 0 is stored row 1
    EXPECT_EQ(model.cellText(0, 0), "Zed");
    model.sortBy(0, true);                    // Zed now sorts last-but-one: bob, carol, dave, Eve, Zed
    EXPECT_EQ(names(model), "bob,carol,dave,Eve,Zed");
    std::optional<newui::CellRange> shown;
    editor.onReplayed = [&shown](const newui::CellRange& block) { shown = block; };
    editor.undo();                            // reverts stored row 1 wherever it is now
    EXPECT_EQ(model.cellText(model.displayRowOf(1).value(), 0), "Alice");
    ASSERT_TRUE(shown.has_value());
    EXPECT_EQ(shown->firstRow, model.displayRowOf(1).value());
    editor.redo();
    EXPECT_EQ(model.cellText(model.displayRowOf(1).value(), 0), "Zed");
}

TEST(SpreadsheetSortedEdits, ReplayOfAHiddenRowSelectsNothing) {
    ManyRowsModel model(0);
    fill(model);
    SpreadsheetEditor editor(model);
    editor.setCell(0, 0, "x");                          // stored row 0
    model.setFilters({FilterRule(1, Op::Equals, "2")});  // hides stored row 0
    bool called = false;
    editor.onReplayed = [&called](const newui::CellRange&) { called = true; };
    editor.undo();
    EXPECT_FALSE(called);
    model.clearFilters();
    EXPECT_EQ(model.cellText(0, 0), "bob");   // the undo still happened
}

TEST(SpreadsheetSortedEdits, PasteFillAndClearActOnDisplayRows) {
    ManyRowsModel model(0);
    fill(model);
    SpreadsheetEditor editor(model);
    model.sortBy(0, true);   // Alice, bob, carol, dave, Eve
    EXPECT_TRUE(editor.fillDown([] {
        newui::CellRange r;
        r.firstRow = 0; r.lastRow = 2; r.firstColumn = 2; r.lastColumn = 2;   // Day of Alice, bob, carol
        return r;
    }()));
    model.clearSort();
    EXPECT_EQ(model.cellText(0, 2), "2024-01-15");   // bob (stored 0) got Alice's day
    EXPECT_EQ(model.cellText(2, 2), "2024-01-15");   // carol too
    EXPECT_EQ(model.cellText(3, 2), "2024-01-15");   // dave already had it
    EXPECT_EQ(model.cellText(4, 2), "2024-05-05");   // Eve untouched
}

TEST(ManyRowsSort, ALoadReplacesTheViewWholesale) {
    ManyRowsModel model(0);
    fill(model);
    model.sortBy(1, true);
    model.setFilters({FilterRule(1, Op::NotBlank)});
    fill(model);   // a new load
    EXPECT_FALSE(model.isSorted());
    EXPECT_FALSE(model.isFiltered());
    EXPECT_EQ(model.rowCount(), 5u);
    EXPECT_EQ(model.columnTitle(1), "Qty");
}

namespace {
    std::string slurp(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream text;
        text << in.rdbuf();
        return text.str();
    }
}

TEST(ManyRowsSaverView, WritesEveryRowInSortOrderWithPlainTitles) {
    ManyRowsModel model(0);
    fill(model);
    model.sortBy(0, true);
    model.setFilters({FilterRule(1, Op::Greater, "5")});   // hides everything but bob and dave on screen
    std::filesystem::path path = std::filesystem::temp_directory_path() / "manyrows_sorted_save.csv";
    ASSERT_TRUE(ManyRowsSaver::writeTable(model, path.string()));
    EXPECT_EQ(slurp(path), "Name,Qty,Day\r\nAlice,2,2024-01-15\r\nbob,10,2024-03-01\r\ncarol,,2024-02-10\r\ndave,30,2024-01-15\r\nEve,2,2024-05-05\r\n");
    std::filesystem::remove(path);
}

TEST(ManyRowsSortPerf, SortsAndFiltersAMillionGeneratedRows) {
    ManyRowsModel model;   // 1,000,000 generated rows
    auto start = std::chrono::steady_clock::now();
    model.sortBy(7, false);   // Total, descending
    double sortSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    EXPECT_GE(std::stod(model.cellText(0, 7)), std::stod(model.cellText(1, 7)));
    EXPECT_GE(std::stod(model.cellText(1, 7)), std::stod(model.cellText(999999, 7)));
    start = std::chrono::steady_clock::now();
    model.setFilters({FilterRule(3, Op::Equals, "North"), FilterRule(5, Op::Greater, "90")});
    double filterSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("sorted 1,000,000 rows by Total in %.2f s; filtered to %zu rows in %.2f s\n", sortSeconds, model.rowCount(), filterSeconds);
    ASSERT_GT(model.rowCount(), 0u);
    for (std::size_t d = 0; d < 20 && d < model.rowCount(); ++d) {
        EXPECT_EQ(model.cellText(d, 3), "North");
        EXPECT_GT(std::stoi(model.cellText(d, 5)), 90);
    }
}

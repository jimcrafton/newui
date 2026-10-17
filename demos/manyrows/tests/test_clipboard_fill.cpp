#include "spreadsheeteditor.h"
#include "tablecliptext.h"

#include <gtest/gtest.h>

using namespace manyrows;

namespace {
    newui::CellRange block(std::size_t r0, std::size_t c0, std::size_t r1, std::size_t c1) {
        newui::CellRange range;
        range.firstRow = r0;
        range.firstColumn = c0;
        range.lastRow = r1;
        range.lastColumn = c1;
        return range;
    }

    // A blank 5 x 4 loaded table (any text goes in a loaded table).
    ManyRowsModel blank() {
        ManyRowsModel model(0);
        model.makeBlank(5, 4);
        return model;
    }

    using Grid = std::vector<std::vector<std::string>>;
}

TEST(TableClipText, EncodesTabsAndRowsAndQuotesAwkwardCells) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    editor.setCell(0, 0, "a");
    editor.setCell(0, 1, "b\tc");
    editor.setCell(1, 0, "say \"hi\"");
    editor.setCell(1, 1, "two\nlines");
    EXPECT_EQ(TableClipText::encode(model, block(0, 0, 1, 1)), "a\t\"b\tc\"\r\n\"say \"\"hi\"\"\"\t\"two\nlines\"\r\n");
    EXPECT_THROW(TableClipText::encode(model, block(0, 0, 4, 3), 10), std::runtime_error);
}

TEST(TableClipText, ParsesLineEndsQuotesAndTrailingBreak) {
    EXPECT_TRUE(TableClipText::parse("").empty());
    EXPECT_EQ(TableClipText::parse("a\tb\r\nc\td\r\n"), (Grid{{"a", "b"}, {"c", "d"}}));
    EXPECT_EQ(TableClipText::parse("a\tb\nc"), (Grid{{"a", "b"}, {"c"}}));
    EXPECT_EQ(TableClipText::parse("a\rb"), (Grid{{"a"}, {"b"}}));
    EXPECT_EQ(TableClipText::parse("\"x\ty\"\t\"q\"\"r\"\n"), (Grid{{"x\ty", "q\"r"}}));
    EXPECT_EQ(TableClipText::parse("\"\"\tb"), (Grid{{"", "b"}}));   // a quoted empty cell is still a cell
    EXPECT_EQ(TableClipText::parse("\t\n"), (Grid{{"", ""}}));       // blank cells keep their place
    EXPECT_EQ(TableClipText::parse("a\n\nb"), (Grid{{"a"}, {""}, {"b"}}));
    EXPECT_EQ(TableClipText::parse("5\"x"), (Grid{{"5\"x"}}));       // a quote mid-cell is plain text
}

TEST(TableClipText, EncodeThenParseRoundTrips) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    editor.setCell(0, 0, "tab\there");
    editor.setCell(0, 2, "\"quoted\"");
    editor.setCell(1, 1, "multi\r\nline");
    Grid parsed = TableClipText::parse(TableClipText::encode(model, block(0, 0, 1, 2)));
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0], (std::vector<std::string>{"tab\there", "", "\"quoted\""}));
    EXPECT_EQ(parsed[1], (std::vector<std::string>{"", "multi\r\nline", ""}));
}

TEST(SpreadsheetPaste, PastesABlockAsOneUndoStep) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    SpreadsheetEditor::PasteOutcome out = editor.paste(block(1, 1, 1, 1), Grid{{"a", "b"}, {"c", "d"}}, false);
    EXPECT_TRUE(out.error.empty());
    EXPECT_EQ(out.changed, 4u);
    EXPECT_FALSE(out.clipped);
    EXPECT_EQ(out.block.lastRow, 2u);
    EXPECT_EQ(out.block.lastColumn, 2u);
    EXPECT_EQ(model.cellText(2, 2), "d");
    EXPECT_EQ(editor.undoDescription(), "Paste B2:C3");
    editor.undo();
    EXPECT_EQ(model.cellText(1, 1), "");
    EXPECT_EQ(model.cellText(2, 2), "");
    EXPECT_FALSE(editor.canUndo());
    editor.redo();
    EXPECT_EQ(model.cellText(1, 2), "b");
}

TEST(SpreadsheetPaste, RaggedRowsPadWithBlanksAndOverwrite) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    editor.setCell(1, 1, "old");
    editor.paste(block(0, 0, 0, 0), Grid{{"a", "b"}, {"c"}}, false);
    EXPECT_EQ(model.cellText(1, 0), "c");
    EXPECT_EQ(model.cellText(1, 1), "");   // the short row blanked the cell it covers
}

TEST(SpreadsheetPaste, ClipsAtTheTableEdge) {
    ManyRowsModel model = blank();   // 5 x 4
    SpreadsheetEditor editor(model);
    SpreadsheetEditor::PasteOutcome out = editor.paste(block(4, 3, 4, 3), Grid{{"a", "b"}, {"c", "d"}}, false);
    EXPECT_TRUE(out.clipped);
    EXPECT_EQ(out.changed, 1u);
    EXPECT_EQ(model.cellText(4, 3), "a");
}

TEST(SpreadsheetPaste, OneCellFillsTheWholeSelection) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    SpreadsheetEditor::PasteOutcome out = editor.paste(block(0, 0, 2, 1), Grid{{"x"}}, false);
    EXPECT_EQ(out.changed, 6u);
    EXPECT_EQ(model.cellText(2, 1), "x");
    EXPECT_EQ(model.cellText(3, 1), "");
    // A multi-cell clipboard keeps its own shape, whatever is selected.
    out = editor.paste(block(0, 0, 3, 3), Grid{{"p", "q"}}, false);
    EXPECT_EQ(out.block.lastRow, 0u);
    EXPECT_EQ(out.block.lastColumn, 1u);
}

TEST(SpreadsheetPaste, SameTextChangesNothingAndPushesNothing) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    SpreadsheetEditor::PasteOutcome out = editor.paste(block(0, 0, 0, 0), Grid{{"", ""}}, false);
    EXPECT_TRUE(out.error.empty());
    EXPECT_EQ(out.changed, 0u);
    EXPECT_FALSE(editor.canUndo());
}

TEST(SpreadsheetPaste, RefusesEmptyHugeAndOutOfRange) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    EXPECT_FALSE(editor.paste(block(0, 0, 0, 0), Grid{}, false).error.empty());
    EXPECT_FALSE(editor.paste(block(0, 0, 0, 0), Grid{{}}, false).error.empty());
    EXPECT_FALSE(editor.paste(block(9, 0, 9, 0), Grid{{"a"}}, false).error.empty());
    ManyRowsModel big(0);
    big.makeBlank(2000, 100);   // 200,000 cells selected, one cell on the clipboard: over the limit
    SpreadsheetEditor bigEditor(big);
    EXPECT_FALSE(bigEditor.paste(block(0, 0, 1999, 99), Grid{{"a"}}, false).error.empty());
    EXPECT_EQ(big.editedCellCount(), 0u);
}

TEST(SpreadsheetPaste, TypedTableRefusesTheWholePasteOnOneBadCell) {
    ManyRowsModel model;   // generated: column 5 Qty is a whole number
    SpreadsheetEditor editor(model);
    std::string before = model.cellText(0, 5);
    SpreadsheetEditor::PasteOutcome out = editor.paste(block(0, 5, 0, 5), Grid{{"7"}, {"seven"}}, true);
    EXPECT_NE(out.error.find("F2"), std::string::npos);
    EXPECT_EQ(model.cellText(0, 5), before);
    EXPECT_FALSE(editor.canUndo());
    out = editor.paste(block(0, 5, 0, 5), Grid{{" 7 "}, {"8"}}, true);
    EXPECT_TRUE(out.error.empty());
    EXPECT_EQ(model.cellText(0, 5), "7");
}

TEST(SpreadsheetFill, DownCopiesTheTopRowOverTheRest) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    editor.setCell(0, 0, "a");
    editor.setCell(0, 1, "b");
    editor.setCell(1, 0, "zap");
    EXPECT_TRUE(editor.fillDown(block(0, 0, 3, 1)));
    EXPECT_EQ(model.cellText(1, 0), "a");
    EXPECT_EQ(model.cellText(3, 1), "b");
    EXPECT_EQ(model.cellText(4, 0), "");
    EXPECT_EQ(editor.undoDescription(), "Fill down A2:B4");
    editor.undo();
    EXPECT_EQ(model.cellText(1, 0), "zap");
    EXPECT_EQ(model.cellText(2, 0), "");
}

TEST(SpreadsheetFill, OneRowCopiesFromAbove) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    editor.setCell(1, 2, "up");
    EXPECT_TRUE(editor.fillDown(block(2, 2, 2, 2)));
    EXPECT_EQ(model.cellText(2, 2), "up");
    EXPECT_FALSE(editor.fillDown(block(0, 0, 0, 0)));   // first row: nothing above
    EXPECT_FALSE(editor.fillDown(block(2, 2, 2, 2)));   // already equal
}

TEST(SpreadsheetFill, RightCopiesTheLeftColumn) {
    ManyRowsModel model = blank();
    SpreadsheetEditor editor(model);
    editor.setCell(0, 0, "l");
    editor.setCell(1, 0, "m");
    EXPECT_TRUE(editor.fillRight(block(0, 0, 1, 3)));
    EXPECT_EQ(model.cellText(0, 3), "l");
    EXPECT_EQ(model.cellText(1, 2), "m");
    EXPECT_EQ(editor.undoDescription(), "Fill right B1:D2");
    EXPECT_TRUE(editor.fillRight(block(0, 3, 0, 3)) == false);   // equal to its left neighbour already
    editor.setCell(0, 3, "z");
    EXPECT_TRUE(editor.fillRight(block(0, 3, 0, 3)));            // one column: copy from the left
    EXPECT_EQ(model.cellText(0, 3), "l");
}

TEST(SpreadsheetFill, RightSkipsCellsATypedColumnWouldRefuse) {
    ManyRowsModel model;   // generated: column 4 Status (text list), column 5 Qty (whole number)
    SpreadsheetEditor editor(model);
    std::string qty = model.cellText(0, 5);
    editor.fillRight(block(0, 4, 0, 5));   // copies Status's text into Qty: not a whole number, so skipped
    EXPECT_EQ(model.cellText(0, 5), qty);
}

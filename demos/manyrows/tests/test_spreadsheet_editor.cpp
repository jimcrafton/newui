#include "spreadsheeteditor.h"

#include <gtest/gtest.h>

using namespace manyrows;

namespace {
    std::string validated(std::size_t column, std::string text, std::string* error = nullptr) {
        std::string message = SpreadsheetEditor::validate(column, text);
        if (error != nullptr) {
            *error = message;
        }
        return text;
    }
}

TEST(SpreadsheetValidate, IntegersNumbersDatesAndStatuses) {
    std::string error;
    EXPECT_EQ(validated(5, "  42 ", &error), "42");
    EXPECT_TRUE(error.empty());
    validated(5, "4.5", &error);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(validated(6, "12.5", &error), "12.50");   // price: two decimals
    EXPECT_TRUE(error.empty());
    EXPECT_EQ(validated(9, "4", &error), "4.0");         // rating: one decimal
    validated(6, "abc", &error);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(validated(8, "2024-02-29", &error), "2024-02-29");
    EXPECT_TRUE(error.empty());
    validated(8, "29/02/2024", &error);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(validated(4, "shipped", &error), "Shipped");   // statuses are canonical
    EXPECT_TRUE(error.empty());
    validated(4, "lost", &error);
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(validated(1, "  Anything  ", &error), "Anything");
    EXPECT_EQ(validated(5, "", &error), "");                 // blank is always allowed
    EXPECT_TRUE(error.empty());
}

TEST(SpreadsheetEditor, CellNames) {
    EXPECT_EQ(SpreadsheetEditor::cellName(0, 0), "A1");
    EXPECT_EQ(SpreadsheetEditor::cellName(9, 25), "Z10");
    EXPECT_EQ(SpreadsheetEditor::cellName(0, 26), "AA1");
}

TEST(SpreadsheetEditor, SetCellUndoRedoRestoresTheGeneratedValue) {
    ManyRowsModel model;
    SpreadsheetEditor editor(model);
    std::string original = model.cellText(3, 1);

    EXPECT_TRUE(editor.setCell(3, 1, "Custom"));
    EXPECT_EQ(model.cellText(3, 1), "Custom");
    EXPECT_TRUE(model.isEdited(3, 1));
    EXPECT_EQ(editor.undoDescription(), "Edit B4");

    editor.undo();
    EXPECT_EQ(model.cellText(3, 1), original);
    EXPECT_FALSE(model.isEdited(3, 1));   // not pinned as an edit
    EXPECT_EQ(model.editedCellCount(), 0u);
    EXPECT_TRUE(editor.canRedo());

    editor.redo();
    EXPECT_EQ(model.cellText(3, 1), "Custom");
}

TEST(SpreadsheetEditor, SettingTheSameTextIsNotAnEdit) {
    ManyRowsModel model;
    SpreadsheetEditor editor(model);
    EXPECT_FALSE(editor.setCell(0, 0, model.cellText(0, 0)));
    EXPECT_FALSE(editor.canUndo());
}

TEST(SpreadsheetEditor, TwoEditsOfOneCellUndoInOrder) {
    ManyRowsModel model;
    SpreadsheetEditor editor(model);
    editor.setCell(2, 2, "first");
    editor.setCell(2, 2, "second");
    editor.undo();
    EXPECT_EQ(model.cellText(2, 2), "first");
    editor.undo();
    EXPECT_FALSE(model.isEdited(2, 2));
}

TEST(SpreadsheetEditor, NewEditDiscardsRedo) {
    ManyRowsModel model;
    SpreadsheetEditor editor(model);
    editor.setCell(0, 1, "a");
    editor.undo();
    EXPECT_TRUE(editor.canRedo());
    editor.setCell(0, 1, "b");
    EXPECT_FALSE(editor.canRedo());
}

TEST(SpreadsheetEditor, ClearRangeIsOneUndoStep) {
    ManyRowsModel model;
    SpreadsheetEditor editor(model);
    newui::CellRange range;
    range.firstRow = 1; range.lastRow = 3;
    range.firstColumn = 2; range.lastColumn = 4;
    std::string keep = model.cellText(1, 1);
    std::string before = model.cellText(2, 3);

    EXPECT_TRUE(editor.clearRange(range));
    EXPECT_EQ(model.cellText(2, 3), "");
    EXPECT_EQ(model.cellText(1, 1), keep);   // outside the block
    EXPECT_EQ(editor.undoDescription(), "Clear C2:E4");
    EXPECT_FALSE(editor.clearRange(range));  // already blank

    editor.undo();
    EXPECT_EQ(model.cellText(2, 3), before);
    EXPECT_EQ(model.editedCellCount(), 0u);
}

TEST(SpreadsheetEditor, ClearRangeRefusesAHugeBlock) {
    ManyRowsModel model;
    SpreadsheetEditor editor(model);
    newui::CellRange range;
    range.firstRow = 0; range.lastRow = 999999;
    range.firstColumn = 0; range.lastColumn = 9;
    EXPECT_FALSE(editor.clearRange(range));
    EXPECT_EQ(model.editedCellCount(), 0u);
    EXPECT_FALSE(editor.canUndo());
}

TEST(SpreadsheetEditor, ReplayReportsTheChangedBlockAndHistoryChanges) {
    ManyRowsModel model;
    SpreadsheetEditor editor(model);
    int history = 0;
    editor.onHistoryChanged = [&history]() { ++history; };
    newui::CellRange seen;
    bool replayed = false;
    editor.onReplayed = [&](const newui::CellRange& block) { seen = block; replayed = true; };

    newui::CellRange range;
    range.firstRow = 5; range.lastRow = 6;
    range.firstColumn = 1; range.lastColumn = 2;
    editor.clearRange(range);
    EXPECT_FALSE(replayed);   // the original edit is not a replay
    editor.undo();
    EXPECT_TRUE(replayed);
    EXPECT_EQ(seen.firstRow, 5u);
    EXPECT_EQ(seen.lastRow, 6u);
    EXPECT_EQ(seen.firstColumn, 1u);
    EXPECT_EQ(seen.lastColumn, 2u);
    EXPECT_EQ(history, 2);
}

TEST(ManyRowsModelEdits, ApplyEditsFiresOnceAndRejectsBadCellsAtomically) {
    ManyRowsModel model;
    int fired = 0;
    model.onChanged.add([&fired](newui::Model&) { ++fired; return newui::SyncReturn::Handled; });
    std::vector<CellEdit> edits(3);
    for (std::size_t i = 0; i < 3; ++i) {
        edits[i].row = i;
        edits[i].column = 1;
        edits[i].after = "x";
    }
    model.applyEdits(edits, true);
    EXPECT_EQ(fired, 1);
    EXPECT_EQ(model.editedCellCount(), 3u);

    edits[2].row = 2000000;   // out of range: nothing at all may change
    std::vector<CellEdit> bad = {edits[0], edits[2]};
    bad[0].after = "changed";
    EXPECT_THROW(model.applyEdits(bad, true), std::runtime_error);
    EXPECT_EQ(model.cellText(0, 1), "x");
}

TEST(SpreadsheetEditor, SavedStateTracksUndoAndRedo) {
    ManyRowsModel model(100);
    SpreadsheetEditor editor(model);
    EXPECT_TRUE(editor.isAtSavedState());          // a fresh history is clean
    editor.setCell(0, 1, "a");
    EXPECT_FALSE(editor.isAtSavedState());
    editor.undo();
    EXPECT_TRUE(editor.isAtSavedState());          // undone back to the start
    editor.redo();
    EXPECT_FALSE(editor.isAtSavedState());

    editor.markSaved();                            // saved with one step applied
    EXPECT_TRUE(editor.isAtSavedState());
    editor.setCell(1, 1, "b");
    EXPECT_FALSE(editor.isAtSavedState());
    editor.undo();
    EXPECT_TRUE(editor.isAtSavedState());          // back to the saved step
    editor.undo();
    EXPECT_FALSE(editor.isAtSavedState());         // before it: differs from the file
    editor.redo();
    EXPECT_TRUE(editor.isAtSavedState());
}

TEST(SpreadsheetEditor, SavedStateIsLostWhenANewEditDiscardsIt) {
    ManyRowsModel model(100);
    SpreadsheetEditor editor(model);
    editor.setCell(0, 1, "a");
    editor.setCell(1, 1, "b");
    editor.markSaved();                            // saved at step 2
    editor.undo();
    editor.undo();
    editor.setCell(2, 1, "c");                     // new branch: step 2 of the old one is gone for good
    editor.setCell(3, 1, "d");
    EXPECT_FALSE(editor.isAtSavedState());
    editor.undo();
    editor.undo();
    editor.undo();
    EXPECT_FALSE(editor.isAtSavedState());         // never equal to the saved file again
    editor.markSaved();
    EXPECT_TRUE(editor.isAtSavedState());
    editor.reset();
    EXPECT_TRUE(editor.isAtSavedState());          // replaced content is clean
}

TEST(SpreadsheetEditor, ParsesGoToText) {
    auto ref = SpreadsheetEditor::parseCellRef(" c4 ");
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->row, 3u);
    ASSERT_TRUE(ref->column.has_value());
    EXPECT_EQ(*ref->column, 2u);
    ref = SpreadsheetEditor::parseCellRef("AA10");
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(*ref->column, 26u);   // AA is the 27th column
    EXPECT_EQ(ref->row, 9u);
    ref = SpreadsheetEditor::parseCellRef("2500");
    ASSERT_TRUE(ref.has_value());
    EXPECT_EQ(ref->row, 2499u);
    EXPECT_FALSE(ref->column.has_value());   // a bare row number keeps the column
    EXPECT_EQ(SpreadsheetEditor::cellName(ref->row, 5), "F2500");   // round trip with cellName
    EXPECT_FALSE(SpreadsheetEditor::parseCellRef("").has_value());
    EXPECT_FALSE(SpreadsheetEditor::parseCellRef("C").has_value());
    EXPECT_FALSE(SpreadsheetEditor::parseCellRef("C0").has_value());
    EXPECT_FALSE(SpreadsheetEditor::parseCellRef("4C").has_value());
    EXPECT_FALSE(SpreadsheetEditor::parseCellRef("C4x").has_value());
    EXPECT_FALSE(SpreadsheetEditor::parseCellRef("ABCDE1").has_value());
    EXPECT_FALSE(SpreadsheetEditor::parseCellRef("1234567890").has_value());
}

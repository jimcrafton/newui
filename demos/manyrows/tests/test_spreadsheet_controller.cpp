#include "spreadsheetcontroller.h"

#include <newui/keyboard_constants.h>

#include <gtest/gtest.h>

#include <memory>

using namespace manyrows;
using Op = FilterRule::Op;

namespace {
    // A TimedTableView over a loaded 5 x 3 table, with a SpreadsheetController attached - no window, no layout
    // pass: the view only needs bounds for the header hit-testing the header-click tests do.
    //   0 bob 10 2024-03-01   1 Alice 2 2024-01-15   2 carol - 2024-02-10   3 dave 30 2024-01-15   4 Eve 2 2024-05-05
    struct Fixture {
        TimedTableView* view = new TimedTableView();
        ManyRowsModel* model = nullptr;
        std::unique_ptr<SpreadsheetController> controller;
        int edited = 0;
        int viewChanged = 0;
        int history = 0;

        Fixture(bool generated = false) {
            view->setBounds(newui::Rect(0, 0, 600, 300));
            auto owned = std::make_unique<ManyRowsModel>(generated ? 1000000u : 0u);
            model = owned.get();
            if (!generated) {
                model->beginLoad(3, {"Name", "Qty", "Day"});
                model->appendRow({"bob", "10", "2024-03-01"});
                model->appendRow({"Alice", "2", "2024-01-15"});
                model->appendRow({"carol", "", "2024-02-10"});
                model->appendRow({"dave", "30", "2024-01-15"});
                model->appendRow({"Eve", "2", "2024-05-05"});
                model->endLoad();
            }
            view->setModel(std::move(owned));
            controller = std::make_unique<SpreadsheetController>(*view, *model);
            controller->onEdited += [this](SpreadsheetController&) {
                ++edited;
                return newui::SyncReturn::Handled;
            };
            controller->onViewChanged += [this](SpreadsheetController&) {
                ++viewChanged;
                return newui::SyncReturn::Handled;
            };
            controller->onHistoryChanged += [this](SpreadsheetController&) {
                ++history;
                return newui::SyncReturn::Handled;
            };
        }

        ~Fixture() {
            controller.reset();   // it registered handlers on the view: it goes first
            view->destroy();
            delete view;
        }

        void key(std::uint32_t vkey, std::uint32_t mods = 0) { view->onKeyDown(*view, mods, 0, 1, vkey); }
        // A click on the header of `column` (the columns are 100 wide, the gutter is TableView's own).
        void clickHeader(std::size_t column, std::uint32_t mods = 0) {
            const newui::TableController& c = view->controller();
            newui::Point pt(c.rowHeaderWidth() + c.columnWidth(0) * float(column) + 40.0f, c.headerHeight() * 0.5f);
            view->onMouseDown(*view, pt, 1, mods);
            view->onMouseUp(*view, pt, 1, mods);
        }
        std::string names() const {
            std::string text;
            for (std::size_t d = 0; d < model->rowCount(); ++d) {
                text += (d ? "," : "") + model->cellText(d, 0);
            }
            return text;
        }
    };
}

TEST(SpreadsheetController, CommitEditsACellAsOneUndoStep) {
    Fixture f;
    EXPECT_TRUE(f.controller->commit(0, 0, "Zed"));
    EXPECT_EQ(f.model->cellText(0, 0), "Zed");
    EXPECT_EQ(f.edited, 1);
    EXPECT_TRUE(f.controller->editor().canUndo());
    f.controller->undo();
    EXPECT_EQ(f.model->cellText(0, 0), "bob");
    EXPECT_EQ(f.edited, 2);   // undo is a history change too
    f.controller->redo();
    EXPECT_EQ(f.model->cellText(0, 0), "Zed");
}

TEST(SpreadsheetController, ATypedTableRefusesBadTextAndALoadedOneAcceptsAnything) {
    Fixture generated(true);
    std::string before = generated.model->cellText(0, 5);   // Qty: a whole number
    EXPECT_FALSE(generated.controller->commit(0, 5, "many"));
    EXPECT_EQ(generated.model->cellText(0, 5), before);
    EXPECT_EQ(generated.edited, 0);
    EXPECT_TRUE(generated.controller->commit(0, 5, " 7 "));
    EXPECT_EQ(generated.model->cellText(0, 5), "7");

    Fixture loaded;
    EXPECT_TRUE(loaded.controller->commit(0, 1, "many"));   // a loaded file takes any text
    EXPECT_EQ(loaded.model->cellText(0, 1), "many");
}

TEST(SpreadsheetController, TheEditGuardRefusesEverythingThatChangesCells) {
    Fixture f;
    bool busy = true;
    f.controller->setEditGuard([&busy]() { return busy ? std::string("loading") : std::string(); });
    EXPECT_FALSE(f.controller->commit(0, 0, "Zed"));
    f.view->setActiveCell(0, 1);
    f.controller->clearSelection();
    f.controller->fillDown();
    f.controller->sortActiveColumn(true);
    EXPECT_EQ(f.model->cellText(0, 0), "bob");
    EXPECT_EQ(f.model->cellText(0, 1), "10");
    EXPECT_FALSE(f.model->isSorted());
    EXPECT_EQ(f.edited, 0);
    busy = false;
    EXPECT_TRUE(f.controller->commit(0, 0, "Zed"));
}

TEST(SpreadsheetController, KeyboardShortcutsRunTheCommands) {
    Fixture f;
    f.view->setActiveCell(1, 1);
    f.key(newui::vkDelete);
    EXPECT_EQ(f.model->cellText(1, 1), "");
    f.key(newui::vkLetterZ, newui::kmCtrl);
    EXPECT_EQ(f.model->cellText(1, 1), "2");
    f.key(newui::vkLetterY, newui::kmCtrl);
    EXPECT_EQ(f.model->cellText(1, 1), "");
    f.key(newui::vkLetterZ, newui::kmCtrl);

    // Fill down: select B1:B3, Ctrl+D copies B1 over B2:B3.
    f.view->setActiveCell(0, 1);
    f.view->setActiveCell(2, 1, true);
    f.key(newui::vkLetterD, newui::kmCtrl);
    EXPECT_EQ(f.model->cellText(2, 1), "10");

    // Alt+Down sorts the active column descending; Alt+Backspace resets.
    f.view->setActiveCell(0, 0);
    f.key(newui::vkDownArrow, newui::kmAlt);
    EXPECT_TRUE(f.model->isSorted());
    EXPECT_FALSE(f.model->sortKey()->ascending);
    f.key(newui::vkBackSpace, newui::kmAlt);
    EXPECT_FALSE(f.model->isSorted());
}

TEST(SpreadsheetController, UnrelatedKeysAreLeftAlone) {
    Fixture f;
    f.view->setActiveCell(0, 0);
    int before = f.edited;
    f.key(newui::vkLetterQ);
    f.key(newui::vkLetterZ);   // without Ctrl
    EXPECT_EQ(f.edited, before);
}

TEST(SpreadsheetController, SortingByTheActiveColumnReportsAViewChange) {
    Fixture f;
    f.view->setActiveCell(2, 1);   // Qty
    f.controller->sortActiveColumn(true);
    EXPECT_EQ(f.names(), "Alice,Eve,bob,dave,carol");
    EXPECT_EQ(f.viewChanged, 1);
    EXPECT_EQ(f.controller->viewSummary(), "Sorted by Qty (ascending)");
    f.controller->clearSort();
    EXPECT_FALSE(f.model->isSorted());
    EXPECT_EQ(f.viewChanged, 2);
    f.controller->clearSort();   // nothing to clear: no event
    EXPECT_EQ(f.viewChanged, 2);
}

TEST(SpreadsheetController, SortingWithoutAnActiveCellDoesNothing) {
    Fixture f;
    f.controller->sortActiveColumn(true);
    EXPECT_FALSE(f.model->isSorted());
    EXPECT_EQ(f.viewChanged, 0);
}

TEST(SpreadsheetController, ClickingAColumnHeaderCyclesAscendingDescendingUnsorted) {
    Fixture f;
    f.clickHeader(1);
    ASSERT_TRUE(f.model->isSorted());
    EXPECT_EQ(f.model->sortKey()->column, 1u);
    EXPECT_TRUE(f.model->sortKey()->ascending);
    f.clickHeader(1);
    EXPECT_FALSE(f.model->sortKey()->ascending);
    f.clickHeader(1);
    EXPECT_FALSE(f.model->isSorted());
    f.clickHeader(1);   // and round again
    EXPECT_TRUE(f.model->sortKey()->ascending);
    f.clickHeader(2);   // another column starts at ascending
    EXPECT_EQ(f.model->sortKey()->column, 2u);
    EXPECT_TRUE(f.model->sortKey()->ascending);
}

TEST(SpreadsheetController, ModifiedHeaderClicksOnlySelect) {
    Fixture f;
    f.clickHeader(1, newui::kmShift);
    f.clickHeader(1, newui::kmCtrl);
    EXPECT_FALSE(f.model->isSorted());
    EXPECT_TRUE(f.view->selectedRange().has_value());   // the column was still selected
}

TEST(SpreadsheetController, FilterBySelectedValueTogglesNarrowsAndReplaces) {
    Fixture f;
    f.view->setActiveCell(1, 1);   // Alice's Qty: 2
    f.controller->filterBySelectedValue();
    EXPECT_EQ(f.names(), "Alice,Eve");
    EXPECT_EQ(f.controller->viewSummary(), "Qty = 2 - 2 of 5 rows");
    EXPECT_FALSE(f.view->selectedRange().has_value());   // the old selection no longer means anything

    f.view->setActiveCell(0, 0);   // now row 0 is Alice: narrow by Name
    f.controller->filterBySelectedValue();
    EXPECT_EQ(f.names(), "Alice");

    f.view->setActiveCell(0, 1);   // the same Qty value again switches that filter off
    f.controller->filterBySelectedValue();
    EXPECT_EQ(f.model->filters().size(), 1u);
    EXPECT_EQ(f.names(), "Alice");   // only the Name filter is left
    f.controller->clearFilters();
    EXPECT_EQ(f.model->rowCount(), 5u);
    EXPECT_EQ(f.viewChanged, 4);
}

TEST(SpreadsheetController, ABlankCellFiltersToTheBlankRows) {
    Fixture f;
    f.view->setActiveCell(2, 1);   // carol's Qty is blank
    f.controller->filterBySelectedValue();
    EXPECT_EQ(f.names(), "carol");
}

TEST(SpreadsheetController, SetFiltersAppliesArbitraryRulesAndRejectsBadColumns) {
    Fixture f;
    EXPECT_TRUE(f.controller->setFilters({FilterRule(1, Op::Greater, "5"), FilterRule(2, Op::Less, "2024-03-01")}));
    EXPECT_EQ(f.names(), "dave");
    EXPECT_FALSE(f.controller->setFilters({FilterRule(9, Op::Blank)}));
    EXPECT_EQ(f.names(), "dave");   // unchanged
    f.controller->resetView();
    EXPECT_EQ(f.model->rowCount(), 5u);
    EXPECT_FALSE(f.model->isFiltered());
}

TEST(SpreadsheetController, GoToMovesTheSelection) {
    Fixture f;
    EXPECT_TRUE(f.controller->goTo("b4"));
    EXPECT_EQ(f.view->activeRow(), 3u);
    EXPECT_EQ(f.view->activeColumn(), 1u);
    EXPECT_TRUE(f.controller->goTo("5"));   // a bare row keeps the column
    EXPECT_EQ(f.view->activeRow(), 4u);
    EXPECT_EQ(f.view->activeColumn(), 1u);
    EXPECT_TRUE(f.controller->goTo("Z99"));   // past the end clamps
    EXPECT_EQ(f.view->activeRow(), 4u);
    EXPECT_EQ(f.view->activeColumn(), 2u);
    EXPECT_FALSE(f.controller->goTo("hello"));
    EXPECT_EQ(f.view->activeColumn(), 2u);
    ASSERT_TRUE(f.controller->activeColumn().has_value());
    EXPECT_EQ(*f.controller->activeColumn(), 2u);
}

TEST(SpreadsheetController, DocumentReplacedForgetsHistorySelectionAndView) {
    Fixture f;
    f.controller->commit(0, 0, "Zed");
    f.view->setActiveCell(0, 1);
    f.controller->sortActiveColumn(true);
    int edited = f.edited;
    int history = f.history;
    f.model->makeBlank(3, 3);   // what a New / Open does to the model
    f.controller->documentReplaced();
    EXPECT_FALSE(f.controller->editor().canUndo());
    EXPECT_FALSE(f.view->selectedRange().has_value());
    EXPECT_EQ(f.edited, edited);          // clearing the history is not an edit
    EXPECT_GT(f.history, history);        // but the toolbar must hear that Undo is gone
    EXPECT_GE(f.viewChanged, 2);          // and the sort and filter are gone too
    EXPECT_FALSE(f.model->isSorted());
}

TEST(SpreadsheetController, TheFormulaBarFollowsTheSelectionAndCommits) {
    Fixture f;
    auto* nameBox = new newui::TextField();
    auto* formula = new newui::TextField();
    f.controller->setFormulaBar(nameBox, formula);
    f.view->setActiveCell(1, 1);
    EXPECT_EQ(nameBox->text(), L"B2");
    EXPECT_EQ(formula->text(), L"2");

    formula->setText(L"99");
    formula->onReturnPressed(*formula);   // Enter in the formula field edits the active cell
    EXPECT_EQ(f.model->cellText(1, 1), "99");

    nameBox->setText(L"c3");
    nameBox->onReturnPressed(*nameBox);   // the name box is the Go to box
    EXPECT_EQ(f.view->activeRow(), 2u);
    EXPECT_EQ(f.view->activeColumn(), 2u);
    EXPECT_EQ(nameBox->text(), L"C3");

    f.controller.reset();
    nameBox->destroy();
    formula->destroy();
    delete nameBox;
    delete formula;
}

TEST(SpreadsheetController, SayWithoutAStatusBarIsHarmless) {
    Fixture f;
    f.controller->say("hello");
    f.controller->showTotals();
    f.controller->setPaneText(SpreadsheetController::TotalPane, "x");
}

#include "newui/controllers.h"
#include "newui/controls.h"
#include "newui/items.h"
#include "newui/keyboard_constants.h"
#include "newui/models.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

// Covers TableModel/TableController geometry, StatusBar/StatusPane, and the headless surface of
// TableView (selection, keyboard navigation, header clicks, column resize) - same simulated-event
// pattern as test_controls.cpp. Painting needs a live HWND for themed styles, so it isn't run here.

using namespace newui;

namespace {

// A rows x columns table of "r,c" strings, computed on demand - nothing is stored per cell.
class GridModel : public TableModel {
public:
    GridModel(std::size_t rows, std::size_t columns) : rows_(rows), columns_(columns) {}

    std::size_t rowCount() const override { return rows_; }
    std::size_t columnCount() const override { return columns_; }

    std::any value(const std::any& key = std::any()) override {
        const auto* cell = std::any_cast<std::pair<std::size_t, std::size_t>>(&key);
        if (cell == nullptr) {
            return std::any();
        }
        return std::string(std::to_string(cell->first) + "," + std::to_string(cell->second));
    }

private:
    std::size_t rows_;
    std::size_t columns_;
};

TableView* makeTableView(std::size_t rows, std::size_t columns) {
    auto* view = new TableView();
    view->setBounds(Rect(0, 0, 400, 300));
    view->setModel(std::make_unique<GridModel>(rows, columns));
    return view;
}

void keyDown(TableView& view, std::uint32_t vk, std::uint32_t mods = 0) {
    view.onKeyDown(view, mods, 0, 1, vk);
}

}

TEST(TableModel, SizeIsTheRowCountAndValueAtBoxesTheKey) {
    GridModel model(10, 3);
    EXPECT_EQ(model.size(), 10u);
    EXPECT_EQ(std::any_cast<std::string>(model.valueAt(4, 2)), "4,2");
}

TEST(TableController, ModelMustBeATableModel) {
    TableController controller;
    EXPECT_EQ(controller.model(), nullptr);
    controller.Controller::setModel(std::make_unique<StringListModel>());
    EXPECT_THROW(controller.model(), std::runtime_error);
}

TEST(TableController, RowGeometryIsConstantTime) {
    TableController controller;
    controller.setModel(std::make_unique<GridModel>(1000000, 5));
    controller.setRowHeight(20.0f);

    EXPECT_EQ(controller.rowCount(), 1000000u);
    EXPECT_FLOAT_EQ(controller.totalHeight(), 20000000.0f);
    EXPECT_EQ(controller.rowAt(0.0f), 0u);
    EXPECT_EQ(controller.rowAt(39.9f), 1u);
    EXPECT_EQ(controller.rowAt(-1.0f), controller.rowCount());
    EXPECT_EQ(controller.rowAt(20000000.0f), controller.rowCount());
    EXPECT_FLOAT_EQ(controller.rowOffset(500000), 10000000.0f);
}

TEST(TableController, ColumnWidthsDefaultAndOverride) {
    TableController controller;
    controller.setModel(std::make_unique<GridModel>(5, 4));
    controller.setDefaultColumnWidth(100.0f);
    controller.setColumnWidth(1, 50.0f);

    EXPECT_FLOAT_EQ(controller.columnWidth(0), 100.0f);
    EXPECT_FLOAT_EQ(controller.columnWidth(1), 50.0f);
    EXPECT_FLOAT_EQ(controller.totalWidth(), 350.0f);
    EXPECT_FLOAT_EQ(controller.columnOffset(2), 150.0f);
    EXPECT_EQ(controller.columnAt(149.0f), 1u);
    EXPECT_EQ(controller.columnAt(150.0f), 2u);
    EXPECT_EQ(controller.columnAt(350.0f), controller.columnCount());
    EXPECT_THROW(controller.setColumnWidth(0, 0.0f), std::runtime_error);
}

TEST(TableController, ChangingAColumnWidthFiresOnDataChanged) {
    TableController controller;
    controller.setModel(std::make_unique<GridModel>(5, 4));
    int fired = 0;
    controller.onDataChanged.add([&fired](TableController&) {
        ++fired;
        return SyncReturn::Handled;
    });
    controller.setColumnWidth(0, 80.0f);
    EXPECT_EQ(fired, 1);
}

TEST(StatusBar, PanesAreAddedAndTheirTextRoundTrips) {
    auto* bar = new StatusBar();
    EXPECT_EQ(bar->paneCount(), 0u);

    std::size_t first = bar->addPane("Ready");
    std::size_t second = bar->addPane("", 120.0f);
    EXPECT_EQ(first, 0u);
    EXPECT_EQ(second, 1u);
    EXPECT_EQ(bar->paneCount(), 2u);
    EXPECT_EQ(bar->paneText(0), "Ready");

    bar->setPaneText(1, "12 ms");
    EXPECT_EQ(bar->paneText(1), "12 ms");

    EXPECT_THROW(bar->paneText(2), std::runtime_error);
    EXPECT_THROW(bar->setPaneText(2, "x"), std::runtime_error);

    bar->destroy();
    delete bar;
}

TEST(StatusBar, FixedWidthPaneKeepsItsWidthAndFlexiblePaneTakesTheRest) {
    auto* bar = new StatusBar();
    bar->setBounds(Rect(0, 0, 500, 22));
    bar->addPane("flex");
    bar->addPane("fixed", 100.0f);
    bar->updateLayout();

    EXPECT_FLOAT_EQ(bar->childViews()[1]->bounds().width(), 100.0f);
    EXPECT_FLOAT_EQ(bar->childViews()[0]->bounds().width(), 400.0f);

    bar->setPaneWidth(1, 0.0f);   // now flexible too
    bar->updateLayout();
    EXPECT_FLOAT_EQ(bar->childViews()[0]->bounds().width(), 250.0f);

    bar->destroy();
    delete bar;
}

TEST(TableView, StartsWithNoActiveCell) {
    TableView* view = makeTableView(100, 5);
    EXPECT_FALSE(view->hasActiveCell());
    EXPECT_FALSE(view->selectedRange().has_value());
    EXPECT_THROW(view->activeRow(), std::runtime_error);
    view->destroy();
    delete view;
}

TEST(TableView, SetActiveCellSelectsOneCellAndFiresOnce) {
    TableView* view = makeTableView(100, 5);
    int fired = 0;
    view->onSelectionChanged.add([&fired](TableView&) {
        ++fired;
        return SyncReturn::Handled;
    });

    view->setActiveCell(3, 2);
    EXPECT_EQ(view->activeRow(), 3u);
    EXPECT_EQ(view->activeColumn(), 2u);
    ASSERT_TRUE(view->selectedRange().has_value());
    EXPECT_EQ(view->selectedRange()->rowCount(), 1u);
    EXPECT_EQ(fired, 1);

    view->setActiveCell(3, 2);   // no change
    EXPECT_EQ(fired, 1);

    view->destroy();
    delete view;
}

TEST(TableView, ExtendingKeepsTheAnchorAndNormalizesTheRange) {
    TableView* view = makeTableView(100, 5);
    view->setActiveCell(5, 3);
    view->setActiveCell(2, 1, true);

    std::optional<CellRange> range = view->selectedRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->firstRow, 2u);
    EXPECT_EQ(range->lastRow, 5u);
    EXPECT_EQ(range->firstColumn, 1u);
    EXPECT_EQ(range->lastColumn, 3u);
    EXPECT_TRUE(view->isCellSelected(4, 2));
    EXPECT_FALSE(view->isCellSelected(6, 2));

    view->destroy();
    delete view;
}

TEST(TableView, OutOfRangeCellsClamp) {
    TableView* view = makeTableView(10, 3);
    view->setActiveCell(500, 500);
    EXPECT_EQ(view->activeRow(), 9u);
    EXPECT_EQ(view->activeColumn(), 2u);
    view->destroy();
    delete view;
}

TEST(TableView, ArrowKeysMoveTheActiveCellAndStopAtTheEdges) {
    TableView* view = makeTableView(10, 3);
    view->setActiveCell(0, 0);

    keyDown(*view, vkDownArrow);
    keyDown(*view, vkRightArrow);
    EXPECT_EQ(view->activeRow(), 1u);
    EXPECT_EQ(view->activeColumn(), 1u);

    keyDown(*view, vkUpArrow);
    keyDown(*view, vkUpArrow);
    keyDown(*view, vkLeftArrow);
    keyDown(*view, vkLeftArrow);
    EXPECT_EQ(view->activeRow(), 0u);
    EXPECT_EQ(view->activeColumn(), 0u);

    view->destroy();
    delete view;
}

TEST(TableView, CtrlArrowJumpsToTheEdgeAndCtrlEndToTheLastCell) {
    TableView* view = makeTableView(1000000, 8);
    view->setActiveCell(10, 2);

    keyDown(*view, vkDownArrow, kmCtrl);
    EXPECT_EQ(view->activeRow(), 999999u);

    keyDown(*view, vkHome, kmCtrl);
    EXPECT_EQ(view->activeRow(), 0u);
    EXPECT_EQ(view->activeColumn(), 0u);

    keyDown(*view, vkEnd, kmCtrl);
    EXPECT_EQ(view->activeRow(), 999999u);
    EXPECT_EQ(view->activeColumn(), 7u);

    view->destroy();
    delete view;
}

TEST(TableView, ShiftArrowExtendsTheRange) {
    TableView* view = makeTableView(10, 5);
    view->setActiveCell(2, 2);
    keyDown(*view, vkDownArrow, kmShift);
    keyDown(*view, vkRightArrow, kmShift);

    std::optional<CellRange> range = view->selectedRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->firstRow, 2u);
    EXPECT_EQ(range->lastRow, 3u);
    EXPECT_EQ(range->firstColumn, 2u);
    EXPECT_EQ(range->lastColumn, 3u);

    view->destroy();
    delete view;
}

TEST(TableView, TabWrapsToTheNextRow) {
    TableView* view = makeTableView(10, 3);
    view->setActiveCell(0, 2);
    keyDown(*view, vkTab);
    EXPECT_EQ(view->activeRow(), 1u);
    EXPECT_EQ(view->activeColumn(), 0u);

    keyDown(*view, vkTab, kmShift);
    EXPECT_EQ(view->activeRow(), 0u);
    EXPECT_EQ(view->activeColumn(), 2u);

    view->destroy();
    delete view;
}

TEST(TableView, CtrlASelectsEverything) {
    TableView* view = makeTableView(1000000, 4);
    keyDown(*view, vkLetterA, kmCtrl);
    std::optional<CellRange> range = view->selectedRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->rowCount(), 1000000u);
    EXPECT_EQ(range->columnCount(), 4u);
    view->destroy();
    delete view;
}

TEST(TableView, ClickingACellMakesItActive) {
    TableView* view = makeTableView(100, 5);
    const TableController& c = view->controller();
    // Inside the cell at row 2, column 1 - past the header strip and row-number gutter.
    Point pt(c.rowHeaderWidth() + c.columnWidth(0) + 5.0f, c.headerHeight() + c.rowHeight() * 2.0f + 5.0f);
    view->onMouseDown(*view, pt, 1, 0);
    view->onMouseUp(*view, pt, 1, 0);
    EXPECT_EQ(view->activeRow(), 2u);
    EXPECT_EQ(view->activeColumn(), 1u);
    view->destroy();
    delete view;
}

TEST(TableView, ClickingAColumnHeaderSelectsTheWholeColumn) {
    TableView* view = makeTableView(100, 5);
    const TableController& c = view->controller();
    Point pt(c.rowHeaderWidth() + c.columnWidth(0) + 40.0f, c.headerHeight() * 0.5f);
    view->onMouseDown(*view, pt, 1, 0);
    view->onMouseUp(*view, pt, 1, 0);

    std::optional<CellRange> range = view->selectedRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->firstColumn, 1u);
    EXPECT_EQ(range->lastColumn, 1u);
    EXPECT_EQ(range->rowCount(), 100u);
    view->destroy();
    delete view;
}

TEST(TableView, ClickingARowNumberSelectsTheWholeRow) {
    TableView* view = makeTableView(100, 5);
    const TableController& c = view->controller();
    Point pt(c.rowHeaderWidth() * 0.5f, c.headerHeight() + c.rowHeight() * 3.0f + 5.0f);
    view->onMouseDown(*view, pt, 1, 0);
    view->onMouseUp(*view, pt, 1, 0);

    std::optional<CellRange> range = view->selectedRange();
    ASSERT_TRUE(range.has_value());
    EXPECT_EQ(range->firstRow, 3u);
    EXPECT_EQ(range->columnCount(), 5u);
    view->destroy();
    delete view;
}

TEST(TableView, DraggingAColumnBorderResizesTheColumn) {
    TableView* view = makeTableView(100, 5);
    std::size_t resized = 99;
    view->onColumnResized.add([&resized](TableView&, std::size_t column) {
        resized = column;
        return SyncReturn::Handled;
    });

    const TableController& c = view->controller();
    float border = c.rowHeaderWidth() + c.columnWidth(0);
    Point down(border - 1.0f, c.headerHeight() * 0.5f);
    view->onMouseDown(*view, down, 1, 0);
    view->onMouseMove(*view, Point(down.x + 40.0f, down.y), 1, 0);
    view->onMouseUp(*view, Point(down.x + 40.0f, down.y), 1, 0);

    EXPECT_FLOAT_EQ(c.columnWidth(0), 140.0f);
    EXPECT_EQ(resized, 0u);
    EXPECT_FALSE(view->hasActiveCell());   // a resize drag is not a selection
    view->destroy();
    delete view;
}

TEST(TableView, ContentSizeIncludesTheHeadersAndEveryRow) {
    TableView* view = makeTableView(1000000, 4);
    Size size;
    view->onQueryContentSize.syncCallFirst(*view, size);
    const TableController& c = view->controller();
    EXPECT_FLOAT_EQ(size.height, c.headerHeight() + c.rowHeight() * 1000000.0f);
    EXPECT_FLOAT_EQ(size.width, c.rowHeaderWidth() + c.columnWidth(0) * 4.0f);
    view->destroy();
    delete view;
}

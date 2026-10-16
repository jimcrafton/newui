#include "manyrowsmodel.h"

#include <gtest/gtest.h>

using namespace manyrows;

TEST(ManyRowsModel, ShapeIsAMillionRowsByTenColumns) {
    ManyRowsModel model;
    EXPECT_EQ(model.rowCount(), 1000000u);
    EXPECT_EQ(model.columnCount(), 10u);
    EXPECT_EQ(model.size(), 1000000u);
    EXPECT_EQ(model.columnTitle(0), "Order");
    EXPECT_EQ(model.columnTitle(9), "Rating");
    EXPECT_EQ(model.columnTitle(10), "");
}

TEST(ManyRowsModel, CellsAreDeterministicPerSeed) {
    ManyRowsModel a(1000, 7);
    ManyRowsModel b(1000, 7);
    ManyRowsModel c(1000, 8);
    bool differs = false;
    for (std::size_t row = 0; row < 200; ++row) {
        for (std::size_t col = 0; col < ManyRowsModel::kColumnCount; ++col) {
            EXPECT_EQ(a.cellText(row, col), b.cellText(row, col));
            if (a.cellText(row, col) != c.cellText(row, col)) {
                differs = true;
            }
        }
    }
    EXPECT_TRUE(differs);
}

TEST(ManyRowsModel, OrderColumnIsTheRowNumber) {
    ManyRowsModel model;
    EXPECT_EQ(model.cellText(0, 0), "1");
    EXPECT_EQ(model.cellText(999999, 0), "1000000");
}

TEST(ManyRowsModel, TotalIsQuantityTimesPrice) {
    ManyRowsModel model;
    for (std::size_t row = 0; row < 500; ++row) {
        double qty = std::stod(model.cellText(row, 5));
        double price = std::stod(model.cellText(row, 6));
        double total = std::stod(model.cellText(row, 7));
        EXPECT_NEAR(total, qty * price, 0.006) << "row " << row;
    }
}

TEST(ManyRowsModel, DatesLookLikeDates) {
    ManyRowsModel model;
    for (std::size_t row = 0; row < 200; ++row) {
        std::string date = model.cellText(row, 8);
        ASSERT_EQ(date.size(), 10u);
        EXPECT_EQ(date[4], '-');
        EXPECT_EQ(date[7], '-');
    }
}

TEST(ManyRowsModel, ValueAtBoxesTheTextAndRejectsOutOfRange) {
    ManyRowsModel model;
    EXPECT_EQ(std::any_cast<std::string>(model.valueAt(41, 0)), "42");
    EXPECT_EQ(model.cellText(1000000, 0), "");
    EXPECT_EQ(model.cellText(0, 10), "");
    EXPECT_FALSE(model.value().has_value());
}

TEST(ManyRowsModel, EditsOverlayTheGeneratedValueAndFireOnChanged) {
    ManyRowsModel model;
    int fired = 0;
    model.onChanged.add([&fired](newui::Model&) {
        ++fired;
        return newui::SyncReturn::Handled;
    });
    std::string before = model.cellText(5, 1);
    model.setValueAt(5, 1, std::string("Custom"));
    EXPECT_EQ(model.cellText(5, 1), "Custom");
    EXPECT_NE(before, "Custom");
    EXPECT_EQ(model.editedCellCount(), 1u);
    EXPECT_EQ(fired, 1);
    // Neighbours are untouched.
    EXPECT_EQ(model.cellText(5, 2), ManyRowsModel(1000000).cellText(5, 2));
    EXPECT_THROW(model.setValueAt(1000000, 0, std::string("x")), std::runtime_error);
}

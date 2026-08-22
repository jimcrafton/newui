#include "newui/items.h"
#include "newui/controllers.h"
#include "newui/models.h"

#include <any>
#include <string>
#include <vector>

#include <gtest/gtest.h>

using namespace newui;

namespace {

// Real BLContext shared by every paint test below - Item::paint()/
// ListItem::paint() etc. need one to draw into, even though these tests
// only care that painting doesn't crash, not the resulting pixels - same
// "shared headless BLContext" pattern test_viewstyle.cpp already uses.
BLContext& SharedContext() {
    static BLImage image(64, 64, BL_FORMAT_PRGB32);
    static BLContext ctx(image);
    return ctx;
}

// Model stub returning a fixed string for a given std::size_t index - just
// enough real data for ListItem::paint() to exercise its
// controller.model()->value(index) path, same "test-local stub" pattern
// test_controllers.cpp's RecordingController already uses. ListModel, not
// plain Model - ListController::setModel() requires it.
class StubListModel : public ListModel {
public:
    std::vector<std::string> rows = { "Alpha", "Bravo", "Charlie" };

    std::any value(const std::any& key) override {
        if (const std::size_t* index = std::any_cast<std::size_t>(&key)) {
            if (*index < rows.size()) {
                return rows[*index];
            }
        }
        return std::any();
    }

    std::size_t size() const override { return rows.size(); }
};

// Model stub for a small, fixed 2-level hierarchy: the root has 2
// children (0, 1); child 0 has 2 children of its own (0/0, 0/1); child 1
// and both of 0's children are leaves. Enough real structure to exercise
// TreeController's own visible-row flattening across expand/collapse.
class StubTreeModel : public TreeModel {
public:
    std::size_t childCount(const std::vector<std::size_t>& path) const override {
        if (path.empty()) {
            return 2;
        }
        if (path.size() == 1 && path[0] == 0) {
            return 2;
        }
        return 0;
    }

    std::any value(const std::any& key) override {
        if (const std::vector<std::size_t>* path = std::any_cast<std::vector<std::size_t>>(&key)) {
            std::string label = "node";
            for (std::size_t i : *path) {
                label += "-" + std::to_string(i);
            }
            return label;
        }
        return std::any();
    }
};

}  // namespace

// ---------------------------------------------------------------------
// ItemController / ListController / TreeController / TableController
// ---------------------------------------------------------------------

TEST(ListController, DefaultItemClassNameIsListItem) {
    ListController controller;
    EXPECT_EQ(controller.defaultItemClassName(), "ListItem");
}

TEST(TreeController, DefaultItemClassNameIsTreeItem) {
    TreeController controller;
    EXPECT_EQ(controller.defaultItemClassName(), "TreeItem");
}

TEST(TableController, DefaultItemClassNameIsTableItem) {
    TableController controller;
    EXPECT_EQ(controller.defaultItemClassName(), "TableItem");
}

TEST(ListController, CreateItemReturnsNonNull) {
    ListController controller;
    ListItem* item = controller.createItem(0);
    ASSERT_NE(item, nullptr);
    controller.releaseItem(item);
}

TEST(ListController, ReleaseItemThenCreateItemReusesSamePointer) {
    ListController controller;
    ListItem* first = controller.createItem(0);
    ASSERT_NE(first, nullptr);

    controller.releaseItem(first);
    ListItem* second = controller.createItem(1);

    EXPECT_EQ(first, second) << "expected the pooled Item to be reused, not reallocated";
    controller.releaseItem(second);
}

TEST(ListController, UnknownDefaultItemClassNameMakesCreateItemThrow) {
    ListController controller;
    controller.setDefaultItemClassName("NoSuchItemClass");
    EXPECT_THROW(controller.createItem(0), std::runtime_error);
}

TEST(ListController, BlankDefaultItemClassNameMakesCreateItemThrow) {
    ListController controller;
    controller.setDefaultItemClassName("");
    EXPECT_THROW(controller.createItem(0), std::runtime_error);
}

TEST(TableController, CreateItemPoolsAndReuses) {
    TableController controller;
    TableItem* first = controller.createItem(0, 0);
    ASSERT_NE(first, nullptr);

    controller.releaseItem(first);
    TableItem* second = controller.createItem(1, 2);

    EXPECT_EQ(first, second);
    controller.releaseItem(second);
}

TEST(TreeController, CreateItemPoolsAndReuses) {
    TreeController controller;
    TreeItem* first = controller.createItem({});
    ASSERT_NE(first, nullptr);

    controller.releaseItem(first);
    TreeItem* second = controller.createItem({ 0, 1 });

    EXPECT_EQ(first, second);
    controller.releaseItem(second);
}

// ---------------------------------------------------------------------
// TreeController - flattening a real hierarchy (StubTreeModel) into a
// visible-row space, driven by expand/collapse state.
// ---------------------------------------------------------------------

TEST(TreeController, VisibleCountIsRootChildCountByDefaultAllCollapsed) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);

    ASSERT_EQ(controller.visibleCount(), 2u);
    EXPECT_EQ(controller.pathAt(0), (std::vector<std::size_t>{ 0u }));
    EXPECT_EQ(controller.pathAt(1), (std::vector<std::size_t>{ 1u }));
}

TEST(TreeController, ExpandingANodeRevealsItsChildrenInPlace) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);

    controller.setExpanded({ 0u }, true);

    ASSERT_EQ(controller.visibleCount(), 4u);
    EXPECT_EQ(controller.pathAt(0), (std::vector<std::size_t>{ 0u }));
    EXPECT_EQ(controller.pathAt(1), (std::vector<std::size_t>{ 0u, 0u }));
    EXPECT_EQ(controller.pathAt(2), (std::vector<std::size_t>{ 0u, 1u }));
    EXPECT_EQ(controller.pathAt(3), (std::vector<std::size_t>{ 1u }));
}

TEST(TreeController, CollapsingHidesChildrenAgain) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);

    controller.toggleExpanded({ 0u });
    ASSERT_EQ(controller.visibleCount(), 4u);

    controller.toggleExpanded({ 0u });
    EXPECT_EQ(controller.visibleCount(), 2u);
}

TEST(TreeController, VisibleIndexOfFindsAVisiblePathAndNulloptForAHiddenOne) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);

    EXPECT_EQ(controller.visibleIndexOf({ 0u, 0u }), std::nullopt) << "0/0 is hidden while 0 is collapsed";

    controller.setExpanded({ 0u }, true);
    ASSERT_TRUE(controller.visibleIndexOf({ 0u, 0u }).has_value());
    EXPECT_EQ(*controller.visibleIndexOf({ 0u, 0u }), 1u);
}

TEST(TreeController, OnDataChangedFiresWhenExpandStateActuallyChanges) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);

    int dataChangedCount = 0;
    controller.onDataChanged.add([&](TreeController&) {
        ++dataChangedCount;
        return SyncReturn::Handled;
        });

    controller.setExpanded({ 0u }, false);  // already collapsed - no-op
    EXPECT_EQ(dataChangedCount, 0);

    controller.setExpanded({ 0u }, true);
    EXPECT_EQ(dataChangedCount, 1);

    controller.toggleExpanded({ 0u });
    EXPECT_EQ(dataChangedCount, 2);
}

TEST(TreeController, TotalHeightItemOffsetAndIndexAtOverVisibleRows) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);
    controller.setDefaultItemHeight(20.0f);
    controller.setExpanded({ 0u }, true);  // 4 visible rows now

    EXPECT_FLOAT_EQ(controller.totalHeight(), 80.0f);
    EXPECT_FLOAT_EQ(controller.itemOffset(2), 40.0f);
    EXPECT_EQ(controller.indexAt(41.0f), 2u);
    EXPECT_EQ(controller.indexAt(80.0f), 4u) << "at/past totalHeight() -> one past the last visible row";
}

TEST(ListController, ItemCountIsZeroWithNoModel) {
    ListController controller;
    EXPECT_EQ(controller.itemCount(), 0u);
}

TEST(ListController, ItemCountForwardsToModelSize) {
    StubListModel model;
    ListController controller;
    controller.setModel(&model);

    EXPECT_EQ(controller.itemCount(), model.rows.size());
}

TEST(ListController, OnDataChangedFiresWhenModelChanges) {
    StubListModel model;
    ListController controller;
    controller.setModel(&model);

    int dataChangedCount = 0;
    controller.onDataChanged.add([&](ListController&) {
        ++dataChangedCount;
        return SyncReturn::Handled;
        });

    model.setValue(std::any(), std::any());

    EXPECT_EQ(dataChangedCount, 1);
}

// ---------------------------------------------------------------------
// ListController - per-row height (itemHeight()/totalHeight()/
// itemOffset()/indexAt()) - a customized ListController subclass can vary
// a row's own height by content, not just by a single uniform constant.
// ---------------------------------------------------------------------

TEST(ListController, ItemHeightDefaultsToDefaultItemHeightForEveryIndex) {
    ListController controller;
    controller.setDefaultItemHeight(30.0f);

    EXPECT_FLOAT_EQ(controller.itemHeight(0), 30.0f);
    EXPECT_FLOAT_EQ(controller.itemHeight(41), 30.0f);
}

TEST(ListController, TotalHeightItemOffsetAndIndexAtAgreeForAUniformHeightList) {
    StubListModel model;  // 3 rows
    ListController controller;
    controller.setModel(&model);
    controller.setDefaultItemHeight(20.0f);

    EXPECT_FLOAT_EQ(controller.totalHeight(), 60.0f);
    EXPECT_FLOAT_EQ(controller.itemOffset(0), 0.0f);
    EXPECT_FLOAT_EQ(controller.itemOffset(1), 20.0f);
    EXPECT_FLOAT_EQ(controller.itemOffset(2), 40.0f);
    EXPECT_EQ(controller.indexAt(0.0f), 0u);
    EXPECT_EQ(controller.indexAt(19.9f), 0u);
    EXPECT_EQ(controller.indexAt(20.0f), 1u);
    EXPECT_EQ(controller.indexAt(59.9f), 2u);
    EXPECT_EQ(controller.indexAt(60.0f), 3u) << "at/past totalHeight() -> one past the last index";
}

namespace {

// A customized ListController: row 1 is twice as tall as every other row
// - e.g. because it represents a kind of content (an image, a header,
// ...) that genuinely needs more room, exactly the scenario itemHeight()
// exists for.
class DoubleHeightSecondRowController : public ListController {
public:
    float itemHeight(std::size_t index) const override {
        return index == 1 ? defaultItemHeight() * 2.0f : defaultItemHeight();
    }
};

}  // namespace

TEST(ListController, CustomizedItemHeightChangesTotalHeightItemOffsetAndIndexAt) {
    StubListModel model;  // 3 rows: 0, 1, 2
    DoubleHeightSecondRowController controller;
    controller.setModel(&model);
    controller.setDefaultItemHeight(20.0f);

    // Row 0: [0, 20), row 1 (doubled): [20, 60), row 2: [60, 80).
    EXPECT_FLOAT_EQ(controller.totalHeight(), 80.0f);
    EXPECT_FLOAT_EQ(controller.itemOffset(1), 20.0f);
    EXPECT_FLOAT_EQ(controller.itemOffset(2), 60.0f);
    EXPECT_EQ(controller.indexAt(30.0f), 1u) << "still inside row 1's doubled span";
    EXPECT_EQ(controller.indexAt(60.0f), 2u);
}

// ---------------------------------------------------------------------
// Item / ListItem paint()
// ---------------------------------------------------------------------

TEST(ListItem, PaintWithNoModelDoesNotCrash) {
    ListController controller;
    ListItem* item = controller.createItem(0);
    ASSERT_NE(item, nullptr);

    item->paint(SharedContext(), Rect(0.0f, 0.0f, 64.0f, 20.0f), 0, controller);

    controller.releaseItem(item);
}

TEST(ListItem, PaintWithStubModelDoesNotCrash) {
    StubListModel model;
    ListController controller;
    controller.setModel(&model);

    ListItem* item = controller.createItem(1);
    ASSERT_NE(item, nullptr);

    item->paint(SharedContext(), Rect(0.0f, 0.0f, 64.0f, 20.0f), 1, controller);

    controller.releaseItem(item);
}

TEST(Item, PaintSetsClientBoundsFromStyle) {
    ListController controller;
    ListItem* item = controller.createItem(0);
    ASSERT_NE(item, nullptr);

    Rect rect(5.0f, 5.0f, 50.0f, 20.0f);
    item->paint(SharedContext(), rect, 0, controller);

    EXPECT_FLOAT_EQ(item->clientBounds().size().width, rect.size().width);
    EXPECT_FLOAT_EQ(item->clientBounds().size().height, rect.size().height);

    controller.releaseItem(item);
}

// ---------------------------------------------------------------------
// TreeItem paint() - takes TreeController&, not the generic
// ItemController& ListItem/TableItem use (see TreeItem's own class
// comment, items.h, for why).
// ---------------------------------------------------------------------

TEST(TreeItem, PaintWithNoModelDoesNotCrash) {
    TreeController controller;
    TreeItem* item = controller.createItem({});
    ASSERT_NE(item, nullptr);

    item->paint(SharedContext(), Rect(0.0f, 0.0f, 64.0f, 20.0f), {}, controller);

    controller.releaseItem(item);
}

TEST(TreeItem, PaintForALeafAndAnExpandableNodeBothDoNotCrash) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);

    // Path {0} has children (expandable, draws a glyph); path {1} is a
    // leaf (no glyph, same reserved indent space).
    TreeItem* expandableItem = controller.createItem({ 0u });
    ASSERT_NE(expandableItem, nullptr);
    expandableItem->paint(SharedContext(), Rect(0.0f, 0.0f, 100.0f, 20.0f), { 0u }, controller);
    controller.releaseItem(expandableItem);

    TreeItem* leafItem = controller.createItem({ 1u });
    ASSERT_NE(leafItem, nullptr);
    leafItem->paint(SharedContext(), Rect(0.0f, 0.0f, 100.0f, 20.0f), { 1u }, controller);
    controller.releaseItem(leafItem);
}

TEST(TreeItem, PaintAtADeeperPathDoesNotCrash) {
    StubTreeModel model;
    TreeController controller;
    controller.setModel(&model);
    controller.setExpanded({ 0u }, true);

    TreeItem* item = controller.createItem({ 0u, 1u });
    ASSERT_NE(item, nullptr);
    item->paint(SharedContext(), Rect(0.0f, 0.0f, 100.0f, 20.0f), { 0u, 1u }, controller);
    controller.releaseItem(item);
}

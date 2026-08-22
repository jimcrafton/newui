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
// test_controllers.cpp's RecordingController already uses.
class StubListModel : public Model {
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

TEST(ListController, UnknownDefaultItemClassNameMakesCreateItemReturnNull) {
    ListController controller;
    controller.setDefaultItemClassName("NoSuchItemClass");
    EXPECT_EQ(controller.createItem(0), nullptr);
}

TEST(ListController, BlankDefaultItemClassNameMakesCreateItemReturnNull) {
    ListController controller;
    controller.setDefaultItemClassName("");
    EXPECT_EQ(controller.createItem(0), nullptr);
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

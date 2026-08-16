#include "newui/serialization.h"
#include "newui/color.h"

#include <gtest/gtest.h>

// Views are heap-only, never stack-allocated - see View's class comment -
// so every tree a test needs is built with new and torn down via
// destroy()+delete at the end, same convention as test_layout.cpp.

namespace {

// A test-local SubView subclass with no fields of its own, used to check
// that SerializationRegistry::registerType<T>() round-trips a
// user-registered widget type as itself rather than as a plain SubView -
// the extension point future button/checkbox classes will rely on.
class TestWidget : public newui::SubView {
public:
    TestWidget() = default;
};

}  // namespace

TEST(Serialization, RoundTripsSubViewTree) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 400, 300));
    root->setVisible(true);
    root->setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal));

    auto buttonStyle = std::make_unique<newui::ButtonStyle>();
    buttonStyle->backgroundFill = newui::Color::fromName("cornflowerblue").toBLRgba32();
    buttonStyle->borderWidth = 2.0f;
    // highlightFill deliberately left unset - checked below to stay unset
    // after round-tripping (BLVar fields are solid-color-only/omit-if-null
    // per the serialization scope).

    auto* child = new newui::SubView();
    child->setName("sidebar");
    child->setBounds(newui::Rect(0, 0, 120, 300));
    child->setVisible(true);
    child->setStyle(std::move(buttonStyle));
    child->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    child->setCursor(newui::Cursor(newui::CursorKind::Hand));
    root->addChild(child);

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    EXPECT_EQ(loadedRoot->bounds(), root->bounds());

    auto* loadedLayout = dynamic_cast<newui::FlexLayout*>(loadedRoot->layout());
    ASSERT_NE(loadedLayout, nullptr);
    EXPECT_EQ(loadedLayout->orientation(), newui::Orientation::Horizontal);

    ASSERT_EQ(loadedRoot->childViews().size(), 1u);
    auto* loadedChild = loadedRoot->childViews()[0];
    EXPECT_EQ(loadedChild->name(), "sidebar");
    EXPECT_EQ(loadedChild->bounds(), child->bounds());
    EXPECT_TRUE(loadedChild->isVisible());
    EXPECT_EQ(loadedChild->cursorKind(), newui::CursorKind::Hand);

    auto* loadedStyle = dynamic_cast<newui::ButtonStyle*>(&loadedChild->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_EQ(loadedStyle->borderWidth, 2.0f);
    EXPECT_FALSE(loadedStyle->backgroundFill.is_null());
    EXPECT_TRUE(loadedStyle->highlightFill.is_null());

    auto* loadedParams = dynamic_cast<newui::FlexLayoutParams*>(loadedChild->layoutParams());
    ASSERT_NE(loadedParams, nullptr);
    EXPECT_EQ(loadedParams->weight, 1.0f);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, CustomCursorWithoutAPathIsOmittedAndReloadsAsWhateverWasThereBefore) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);
    // A Custom cursor built from an in-memory image (Cursor::setImage())
    // has no path() - no stable, file-portable value to write - see
    // cursor.h - so View::writeFields() skips it entirely rather than
    // writing a "Custom" string with no path to go with it. Contrast with
    // the path-based case below, which does round-trip.
    BLImage image(16, 16, BL_FORMAT_PRGB32);
    BLContext ctx(image);
    ctx.set_fill_style(BLRgba32(0, 0, 200, 200));
    ctx.fill_all();
    ctx.end();
    ASSERT_TRUE(root->cursor().setImage(image));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    loadedRoot->setCursor(newui::Cursor(newui::CursorKind::Wait));  // pre-existing value readFields() should leave alone
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    EXPECT_EQ(loadedRoot->cursorKind(), newui::CursorKind::Wait);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, CustomCursorLoadedFromAFileRoundTripsViaItsPath) {
    const std::string cursorPath = "serialization_cursor_test.png";
    {
        BLImage image(16, 16, BL_FORMAT_PRGB32);
        BLContext ctx(image);
        ctx.set_fill_style(BLRgba32(0, 128, 0, 200));
        ctx.fill_all();
        ctx.end();
        ASSERT_EQ(image.write_to_file(cursorPath.c_str()), BL_SUCCESS);
    }

    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);
    ASSERT_TRUE(root->cursor().setPath(cursorPath));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    EXPECT_EQ(loadedRoot->cursorKind(), newui::CursorKind::Custom);
    EXPECT_EQ(loadedRoot->cursor().path(), cursorPath);
    EXPECT_NE(loadedRoot->resolvedCursor(), nullptr);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
    ::DeleteFileA(cursorPath.c_str());
}

TEST(Serialization, RoundTripsThemedButtonStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedButtonStyle>();
    themedStyle->pressed = true;
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedButtonStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->pressed);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedRadioButtonStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedRadioButtonStyle>();
    themedStyle->checked = true;
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedRadioButtonStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->checked);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedGroupBoxStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedGroupBoxStyle>();
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedGroupBoxStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedToolbarButtonStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedToolbarButtonStyle>();
    themedStyle->checked = true;
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedToolbarButtonStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->checked);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedToolbarDropDownButtonStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedToolbarDropDownButtonStyle>();
    themedStyle->checked = true;
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedToolbarDropDownButtonStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->checked);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedToolbarDropDownButtonGlyphStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 16, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedToolbarDropDownButtonGlyphStyle>();
    themedStyle->pressed = true;
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedToolbarDropDownButtonGlyphStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->pressed);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedToolbarSplitButtonStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedToolbarSplitButtonStyle>();
    themedStyle->checked = true;
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedToolbarSplitButtonStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->checked);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedToolbarSplitButtonDropDownStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 16, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedToolbarSplitButtonDropDownStyle>();
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedToolbarSplitButtonDropDownStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedToolbarSeparatorStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 6, 24));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedToolbarSeparatorStyle>();
    themedStyle->horizontal = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedToolbarSeparatorStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedStatusPaneStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);
    root->setStyle(std::make_unique<newui::ThemedStatusPaneStyle>());

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    EXPECT_NE(dynamic_cast<newui::ThemedStatusPaneStyle*>(&loadedRoot->style()), nullptr);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedRebarBandStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);
    root->setStyle(std::make_unique<newui::ThemedRebarBandStyle>());

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    EXPECT_NE(dynamic_cast<newui::ThemedRebarBandStyle*>(&loadedRoot->style()), nullptr);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedRebarChevronStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 16, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedRebarChevronStyle>();
    themedStyle->horizontal = false;
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedRebarChevronStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTooltipStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 30));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedTooltipStyle>();
    themedStyle->linked = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedTooltipStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->linked);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedSpinButtonStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 16, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedSpinButtonStyle>();
    themedStyle->isUpButton = false;
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedSpinButtonStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->isUpButton);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedEditStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 120, 24));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedEditStyle>();
    themedStyle->focused = true;
    themedStyle->readOnly = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedEditStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->focused);
    EXPECT_TRUE(loadedStyle->readOnly);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedListItemStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 120, 20));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedListItemStyle>();
    themedStyle->selected = true;
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedListItemStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->selected);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedHeaderItemStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 80, 20));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedHeaderItemStyle>();
    themedStyle->pressed = true;
    themedStyle->sorted = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedHeaderItemStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->pressed);
    EXPECT_TRUE(loadedStyle->sorted);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedHeaderSortArrowStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 12, 12));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedHeaderSortArrowStyle>();
    themedStyle->sortedAscending = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedHeaderSortArrowStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->sortedAscending);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTreeItemStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 120, 18));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedTreeItemStyle>();
    themedStyle->selected = true;
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedTreeItemStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->selected);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTreeGlyphStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 16, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedTreeGlyphStyle>();
    themedStyle->expanded = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedTreeGlyphStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->expanded);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTabItemStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 80, 24));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedTabItemStyle>();
    themedStyle->alignment = newui::ThemedTabItemStyle::TabAlignment::Left;
    themedStyle->position = newui::ThemedTabItemStyle::Position::Right;
    themedStyle->selected = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedTabItemStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_EQ(loadedStyle->alignment, newui::ThemedTabItemStyle::TabAlignment::Left);
    EXPECT_EQ(loadedStyle->position, newui::ThemedTabItemStyle::Position::Right);
    EXPECT_TRUE(loadedStyle->selected);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTabPaneStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 200, 150));
    root->setVisible(true);
    root->setStyle(std::make_unique<newui::ThemedTabPaneStyle>());

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    EXPECT_NE(dynamic_cast<newui::ThemedTabPaneStyle*>(&loadedRoot->style()), nullptr);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTrackbarTrackStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 120, 6));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedTrackbarTrackStyle>();
    themedStyle->horizontal = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedTrackbarTrackStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTrackbarThumbStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 12, 20));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedTrackbarThumbStyle>();
    themedStyle->horizontal = false;
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedTrackbarThumbStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedTrackbarTicksStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 120, 6));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedTrackbarTicksStyle>();
    themedStyle->horizontal = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedTrackbarTicksStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedProgressBarTrackStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 160, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedProgressBarTrackStyle>();
    themedStyle->horizontal = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedProgressBarTrackStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedProgressBarFillStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 80, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedProgressBarFillStyle>();
    themedStyle->horizontal = false;
    themedStyle->state = newui::ThemedProgressBarFillStyle::FillState::Error;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedProgressBarFillStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);
    EXPECT_EQ(loadedStyle->state, newui::ThemedProgressBarFillStyle::FillState::Error);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedScrollbarThumbStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 20, 40));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedScrollbarThumbStyle>();
    themedStyle->horizontal = false;
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedScrollbarThumbStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedScrollbarArrowStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 16, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedScrollbarArrowStyle>();
    themedStyle->direction = newui::ThemedScrollbarArrowStyle::Direction::Right;
    themedStyle->pressed = true;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedScrollbarArrowStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_EQ(loadedStyle->direction, newui::ThemedScrollbarArrowStyle::Direction::Right);
    EXPECT_TRUE(loadedStyle->pressed);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedScrollbarTrackStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 120, 16));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedScrollbarTrackStyle>();
    themedStyle->horizontal = false;
    themedStyle->position = newui::ThemedScrollbarTrackStyle::Position::Upper;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedScrollbarTrackStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_FALSE(loadedStyle->horizontal);
    EXPECT_EQ(loadedStyle->position, newui::ThemedScrollbarTrackStyle::Position::Upper);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsThemedMenuBarItemStyle) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 60, 32));
    root->setVisible(true);

    auto themedStyle = std::make_unique<newui::ThemedMenuBarItemStyle>();
    themedStyle->pressed = true;
    themedStyle->enabled = false;
    root->setStyle(std::move(themedStyle));

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedStyle = dynamic_cast<newui::ThemedMenuBarItemStyle*>(&loadedRoot->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_TRUE(loadedStyle->pressed);
    EXPECT_FALSE(loadedStyle->enabled);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, RoundTripsGridLayout) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 300, 200));
    root->setVisible(true);

    auto grid = std::make_unique<newui::GridLayout>();
    grid->addFixedColumn(80.0f);
    grid->addStarColumn(2.0f);
    grid->addAutoRow();
    grid->setColumnSpacing(4.0f);
    grid->setRowSpacing(2.0f);
    root->setLayout(std::move(grid));

    auto* child = new newui::SubView();
    child->setName("cell");
    child->setBounds(newui::Rect(0, 0, 10, 10));
    child->setVisible(true);
    auto params = std::make_unique<newui::GridLayoutParams>(0, 1);
    params->columnSpan = 1;
    params->horizontalAlignment = newui::CrossAxisAlignment::Center;
    child->setLayoutParams(std::move(params));
    root->addChild(child);

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    auto* loadedGrid = dynamic_cast<newui::GridLayout*>(loadedRoot->layout());
    ASSERT_NE(loadedGrid, nullptr);
    ASSERT_EQ(loadedGrid->columns().size(), 2u);
    EXPECT_EQ(loadedGrid->columns()[0].kind, newui::GridTrackKind::Fixed);
    EXPECT_FLOAT_EQ(loadedGrid->columns()[0].value, 80.0f);
    EXPECT_EQ(loadedGrid->columns()[1].kind, newui::GridTrackKind::Star);
    EXPECT_FLOAT_EQ(loadedGrid->columns()[1].value, 2.0f);
    ASSERT_EQ(loadedGrid->rows().size(), 1u);
    EXPECT_EQ(loadedGrid->rows()[0].kind, newui::GridTrackKind::Auto);
    EXPECT_FLOAT_EQ(loadedGrid->columnSpacing(), 4.0f);
    EXPECT_FLOAT_EQ(loadedGrid->rowSpacing(), 2.0f);

    ASSERT_EQ(loadedRoot->childViews().size(), 1u);
    auto* loadedParams = dynamic_cast<newui::GridLayoutParams*>(loadedRoot->childViews()[0]->layoutParams());
    ASSERT_NE(loadedParams, nullptr);
    EXPECT_EQ(loadedParams->row, 0u);
    EXPECT_EQ(loadedParams->column, 1u);
    EXPECT_EQ(loadedParams->horizontalAlignment, newui::CrossAxisAlignment::Center);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, LoadFailsGracefullyOnUnknownChildType) {
    const std::string json = R"({
        name: "root", visible: true,
        bounds: { x: 0, y: 0, width: 10, height: 10 },
        style: { type: "ViewStyle" },
        children: [
            {
                type: "TotallyUnknownWidget", name: "x", visible: true,
                bounds: { x: 0, y: 0, width: 1, height: 1 },
                style: { type: "ViewStyle" },
                children: [],
            },
        ],
    })";

    auto* target = new newui::SubView();
    EXPECT_FALSE(newui::loadViewTree(*target, json));

    target->destroy();
    delete target;
}

TEST(Serialization, CustomRegisteredSubViewRoundTripsAsItself) {
    newui::SerializationRegistry::registerType<TestWidget>();

    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 100));
    root->setVisible(true);

    auto* child = new TestWidget();
    child->setName("widget");
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));
    ASSERT_EQ(loadedRoot->childViews().size(), 1u);
    EXPECT_NE(dynamic_cast<TestWidget*>(loadedRoot->childViews()[0]), nullptr);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, FrameFieldsRoundTripWithoutLiveWindow) {
    // ~Frame() only throws over a live window that was never torn down
    // through WM_DESTROY (see frame.cpp) - neither Frame below ever calls
    // initialize(), so both are safe to destroy normally here.
    newui::Frame frame;
    frame.setTitle("My Window");
    frame.setBounds(newui::Rect(10, 20, 800, 600));

    const std::string json = newui::saveFrame(frame);

    newui::Frame loaded;
    ASSERT_TRUE(newui::loadFrame(loaded, json));
    EXPECT_EQ(loaded.getTitle(), "My Window");
    EXPECT_EQ(loaded.getBounds(), frame.getBounds());
}

TEST(Serialization, ApplicationCustomDataRoundTrips) {
    newui::Application& app = newui::Application::instance();
    app.setCustomValue("lastOpenedFile", "C:/foo/bar.txt");

    const std::string json = newui::saveApplication(app);

    app.setCustomValue("lastOpenedFile", "overwritten-before-load");
    ASSERT_TRUE(newui::loadApplication(app, json));

    std::string value;
    ASSERT_TRUE(app.getCustomValue("lastOpenedFile", value));
    EXPECT_EQ(value, "C:/foo/bar.txt");
}

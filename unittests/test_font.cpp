#include "newui/font.h"

#include <gtest/gtest.h>

TEST(Font, DefaultConstructedHasDefaultSizeAndNoStyleFlags) {
    newui::Font font;
    EXPECT_TRUE(font.name().empty());
    EXPECT_FLOAT_EQ(font.size(), 12.0f);
    EXPECT_FALSE(font.bold());
    EXPECT_FALSE(font.italic());
    EXPECT_FALSE(font.strikeThrough());
    EXPECT_FALSE(font.underlined());
}

TEST(Font, NameAndSizeConstructorSetsFields) {
    newui::Font font("Segoe UI", 18.0f);
    EXPECT_EQ(font.name(), "Segoe UI");
    EXPECT_FLOAT_EQ(font.size(), 18.0f);
}

TEST(Font, SettersUpdateFields) {
    newui::Font font;
    font.setName("Arial");
    font.setSize(20.0f);
    font.setBold(true);
    font.setItalic(true);
    font.setStrikeThrough(true);
    font.setUnderlined(true);

    EXPECT_EQ(font.name(), "Arial");
    EXPECT_FLOAT_EQ(font.size(), 20.0f);
    EXPECT_TRUE(font.bold());
    EXPECT_TRUE(font.italic());
    EXPECT_TRUE(font.strikeThrough());
    EXPECT_TRUE(font.underlined());
}

TEST(Font, BlFontResolvesToAValidFontForAKnownSystemFont) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u) << "need at least one system font to test with";

    newui::Font font(fonts[0].name, 16.0f);
    BLFont* blFont = font.blFont();
    ASSERT_NE(blFont, nullptr);
    EXPECT_TRUE(blFont->is_valid());
    EXPECT_FLOAT_EQ(blFont->size(), 16.0f);
}

TEST(Font, BlFontReturnsNullptrForUnknownName) {
    newui::Font font("ThisIsNotARealFontName_xyz123", 12.0f);
    EXPECT_EQ(font.blFont(), nullptr);
}

TEST(Font, RepeatedBlFontCallsWithoutChangesReturnSameCachedInstance) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u);

    newui::Font font(fonts[0].name, 14.0f);
    BLFont* first = font.blFont();
    BLFont* second = font.blFont();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, second);
}

TEST(Font, SameNameAndSizeAcrossDifferentFontInstancesShareCachedBLFont) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u);

    newui::Font a(fonts[0].name, 22.0f);
    newui::Font b(fonts[0].name, 22.0f);

    BLFont* fontA = a.blFont();
    BLFont* fontB = b.blFont();
    ASSERT_NE(fontA, nullptr);
    EXPECT_EQ(fontA, fontB) << "FontManager should cache by name+size";
}

TEST(Font, ChangingSizeResolvesToADifferentCachedInstance) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u);

    newui::Font font(fonts[0].name, 10.0f);
    BLFont* atTen = font.blFont();
    ASSERT_NE(atTen, nullptr);

    font.setSize(11.0f);
    BLFont* atEleven = font.blFont();
    ASSERT_NE(atEleven, nullptr);

    EXPECT_NE(atTen, atEleven);
    EXPECT_FLOAT_EQ(atTen->size(), 10.0f);
    EXPECT_FLOAT_EQ(atEleven->size(), 11.0f);
}

// ---------------------------------------------------------------------------
// FontManager::getSystemFont()
// ---------------------------------------------------------------------------

TEST(FontManagerGetSystemFont, MessageFontHasNonEmptyNameAndPositiveSize) {
    newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
    EXPECT_FALSE(font.name().empty());
    EXPECT_GT(font.size(), 0.0f);
}

TEST(FontManagerGetSystemFont, DefaultArgumentIsMessageFont) {
    newui::Font withDefault = newui::FontManager::getSystemFont();
    newui::Font message = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);

    EXPECT_EQ(withDefault.name(), message.name());
    EXPECT_FLOAT_EQ(withDefault.size(), message.size());
}

TEST(FontManagerGetSystemFont, EachStandardFontKindHasNonEmptyNameAndPositiveSize) {
    const newui::SystemUIFont kinds[] = {
        newui::SystemUIFont::Caption, newui::SystemUIFont::SmallCaption,
        newui::SystemUIFont::Menu, newui::SystemUIFont::Status, newui::SystemUIFont::Message,
    };

    for (newui::SystemUIFont kind : kinds) {
        newui::Font font = newui::FontManager::getSystemFont(kind);
        EXPECT_FALSE(font.name().empty());
        EXPECT_GT(font.size(), 0.0f);
    }
}

TEST(FontManagerGetSystemFont, ResolvesToAValidBLFont) {
    newui::Font font = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
    BLFont* blFont = font.blFont();
    ASSERT_NE(blFont, nullptr);
    EXPECT_TRUE(blFont->is_valid());
}

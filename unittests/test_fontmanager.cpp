#include "newui/fontmanager.h"

#include <gtest/gtest.h>

#include <algorithm>

TEST(FontManager, ListFontsIsNonEmptyOnAWindowsMachine) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    EXPECT_GT(fonts.size(), 0u);
}

TEST(FontManager, ListFontsIsStableAcrossCalls) {
    const std::vector<newui::SystemFontInfo>& first = newui::FontManager::listFonts();
    const std::vector<newui::SystemFontInfo>& second = newui::FontManager::listFonts();
    EXPECT_EQ(&first, &second) << "listFonts() should return the same cached instance";
}

TEST(FontManager, EveryListedFontHasNameAndFilePath) {
    for (const newui::SystemFontInfo& font : newui::FontManager::listFonts()) {
        EXPECT_FALSE(font.name.empty());
        EXPECT_FALSE(font.filePath.empty());
    }
}

TEST(FontManager, CreateFontByKnownSystemNameSucceeds) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u) << "need at least one system font to test with";

    BLFont font;
    bool ok = newui::FontManager::createFont(fonts[0].name, 12.0f, font);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(font.is_valid());
}

TEST(FontManager, CreateFontNameLookupIsCaseInsensitive) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u);

    std::string upperName = fonts[0].name;
    std::transform(upperName.begin(), upperName.end(), upperName.begin(), ::toupper);

    BLFont font;
    EXPECT_TRUE(newui::FontManager::createFont(upperName, 12.0f, font));
}

TEST(FontManager, CreateFontByDirectFilePathSucceeds) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u);

    BLFont font;
    bool ok = newui::FontManager::createFont(fonts[0].filePath, 12.0f, font);
    EXPECT_TRUE(ok);
    EXPECT_TRUE(font.is_valid());
}

TEST(FontManager, CreateFontWithUnknownNameOrPathFails) {
    BLFont font;
    bool ok = newui::FontManager::createFont("ThisIsNotARealFontName_xyz123", 12.0f, font);
    EXPECT_FALSE(ok);
}

TEST(FontManager, CreatedFontHasRequestedSize) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u);

    BLFont font;
    ASSERT_TRUE(newui::FontManager::createFont(fonts[0].name, 24.0f, font));
    EXPECT_FLOAT_EQ(font.size(), 24.0f);
}

// blend2d only supports TrueType/OpenType (BL_FONT_FACE_TYPE_OPENTYPE is
// the only non-"none" BLFontFaceType), so every listed font, having
// already loaded successfully once during enumeration, must report that
// type when loaded again here.
TEST(FontManager, ListedFontsAreOpenTypeOrTrueType) {
    const std::vector<newui::SystemFontInfo>& fonts = newui::FontManager::listFonts();
    ASSERT_GT(fonts.size(), 0u);

    for (const newui::SystemFontInfo& info : fonts) {
        BLFontFace face;
        ASSERT_EQ(face.create_from_file(info.filePath.c_str()), BL_SUCCESS) << info.filePath;
        EXPECT_EQ(face.face_type(), BL_FONT_FACE_TYPE_OPENTYPE) << info.name;
    }
}

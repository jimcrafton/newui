#include "newui/themedata.h"
#include "newui/bundle.h"

#include <gtest/gtest.h>

#include <fstream>

// Same "create/remove the on-disk resource ourselves" discipline
// test_bundle.cpp already establishes - Bundle::resourcesDir() doesn't
// exist under a fresh build output directory by default.
//
// ThemeData is a process-lifetime Meyer's singleton (themedata.h's own
// class comment explains why - same shape as Bundle) - every test here
// calls unload() when it's done, so a later test (in this file or any
// other TU linked into the same binary) never sees stale state left
// over from an earlier one.

namespace {

std::string WriteThemeFixture(const std::string& relativePath, const std::string& contents) {
    const newui::Bundle& bundle = newui::Bundle::instance();
    const std::string themesDir = bundle.resourcesDir() + "\\Themes";
    ::CreateDirectoryA(bundle.resourcesDir().c_str(), nullptr);
    ::CreateDirectoryA(themesDir.c_str(), nullptr);

    const std::string filePath = themesDir + "\\" + relativePath;
    {
        std::ofstream file(filePath, std::ios::binary);
        file << contents;
    }
    return filePath;
}

void RemoveThemesDir() {
    const newui::Bundle& bundle = newui::Bundle::instance();
    ::RemoveDirectoryA((bundle.resourcesDir() + "\\Themes").c_str());
    ::RemoveDirectoryA(bundle.resourcesDir().c_str());
}

}  // namespace

TEST(ThemeData, StartsUnloadedAndEveryLookupMisses) {
    newui::ThemeData::instance().unload();

    EXPECT_FALSE(newui::ThemeData::instance().isLoaded());

    newui::Color color;
    EXPECT_FALSE(newui::ThemeData::instance().tryColorFor(newui::UIColorRole::WindowBackground, color));
    EXPECT_EQ(newui::ThemeData::instance().tryPartData(L"BUTTON", 1, 1), nullptr);
}

TEST(ThemeData, LoadReturnsFalseAndStaysUnloadedForAMissingFile) {
    newui::ThemeData::instance().unload();

    EXPECT_FALSE(newui::ThemeData::instance().load("Themes\\NoSuchTheme.theme"));
    EXPECT_FALSE(newui::ThemeData::instance().isLoaded());
}

TEST(ThemeData, LoadParsesRolesAndPartsAndReloadRepeatsTheSamePath) {
    newui::ThemeData::instance().unload();

    const std::string fixture = R"({
        "roles": {
            "WindowBackground": "#112233FF",
            "NotARealRole": "#000000FF"
        },
        "parts": {
            "BUTTON": {
                "BP_PUSHBUTTON": {
                    "PBS_NORMAL": {
                        "size": { "width": 75, "height": 23 },
                        "contentRect": { "left": 3, "top": 3, "right": 3, "bottom": 3 },
                        "colors": { "textColor": "#000000FF", "edgeFillColor": "#C6C6C6FF" }
                    }
                }
            },
            "STATUS": {
                "SP_PANE": {
                    "DEFAULT": { "size": { "width": 10, "height": 10 } }
                }
            }
        }
    })";
    std::string filePath = WriteThemeFixture("fixture.theme", fixture);

    ASSERT_TRUE(newui::ThemeData::instance().load("Themes\\fixture.theme"));
    EXPECT_TRUE(newui::ThemeData::instance().isLoaded());

    newui::Color color;
    ASSERT_TRUE(newui::ThemeData::instance().tryColorFor(newui::UIColorRole::WindowBackground, color));
    EXPECT_EQ(color.toString(), "#112233ff");
    // Unrecognized role name is silently skipped, not an error - just
    // never resolvable.
    EXPECT_FALSE(newui::ThemeData::instance().tryColorFor(newui::UIColorRole::ControlBackground, color));

    const newui::ThemePartData* buttonData = newui::ThemeData::instance().tryPartData(L"BUTTON", 1, 1);
    ASSERT_NE(buttonData, nullptr);
    ASSERT_TRUE(buttonData->size.has_value());
    EXPECT_FLOAT_EQ(buttonData->size->width, 75.0f);
    EXPECT_FLOAT_EQ(buttonData->size->height, 23.0f);
    ASSERT_TRUE(buttonData->textColor.has_value());
    EXPECT_EQ(buttonData->textColor->toString(), "#000000ff");

    // The "0" state - parts with no real state enum (see themesgen.py's
    // own notes on STATUS/SP_PANE) - resolves via resolveSymbol()'s raw-
    // integer fallback, not a symbol-table entry.
    const newui::ThemePartData* statusData = newui::ThemeData::instance().tryPartData(L"STATUS", 1, 0);
    ASSERT_NE(statusData, nullptr);
    ASSERT_TRUE(statusData->size.has_value());
    EXPECT_FLOAT_EQ(statusData->size->width, 10.0f);

    // A triple this fixture never mentions still misses cleanly.
    EXPECT_EQ(newui::ThemeData::instance().tryPartData(L"BUTTON", 3, 1), nullptr);

    // reload() repeats whatever path load() last used, without the
    // caller needing to remember it.
    EXPECT_TRUE(newui::ThemeData::instance().reload());
    EXPECT_TRUE(newui::ThemeData::instance().isLoaded());

    newui::ThemeData::instance().unload();
    ::DeleteFileA(filePath.c_str());
    RemoveThemesDir();
}

TEST(ThemeData, UnloadClearsEverything) {
    newui::ThemeData::instance().unload();
    std::string filePath = WriteThemeFixture("fixture2.theme", R"({"roles":{"WindowBackground":"#FFFFFFFF"},"parts":{}})");

    ASSERT_TRUE(newui::ThemeData::instance().load("Themes\\fixture2.theme"));
    ASSERT_TRUE(newui::ThemeData::instance().isLoaded());

    newui::ThemeData::instance().unload();
    EXPECT_FALSE(newui::ThemeData::instance().isLoaded());
    newui::Color color;
    EXPECT_FALSE(newui::ThemeData::instance().tryColorFor(newui::UIColorRole::WindowBackground, color));
    // reload() after unload() has nothing to repeat.
    EXPECT_FALSE(newui::ThemeData::instance().reload());

    ::DeleteFileA(filePath.c_str());
    RemoveThemesDir();
}

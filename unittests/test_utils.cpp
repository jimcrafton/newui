#include "newui/utils.h"

#include <gtest/gtest.h>

TEST(NormalizePath, TurnsBackslashesIntoSlashes) {
    EXPECT_EQ(newui::normalizePath("C:\\code\\newui\\src"), "C:/code/newui/src");
}

TEST(NormalizePath, DropsTrailingSlashes) {
    EXPECT_EQ(newui::normalizePath("C:/code/newui/"), "C:/code/newui");
    EXPECT_EQ(newui::normalizePath("C:\\code\\\\"), "C:/code");
}

TEST(NormalizePath, LeavesALoneSlashAndEmptyAlone) {
    EXPECT_EQ(newui::normalizePath("/"), "/");
    EXPECT_EQ(newui::normalizePath(""), "");
}

TEST(ToLowerCase, LowersAsciiAndUnicode) {
    EXPECT_EQ(newui::toLowerCase("C:/Code/MIXED.Txt"), "c:/code/mixed.txt");
    EXPECT_EQ(newui::toLowerCase("\xC3\x84rger"), "\xC3\xA4rger");   // Ä -> ä
    EXPECT_EQ(newui::toLowerCase(""), "");
}

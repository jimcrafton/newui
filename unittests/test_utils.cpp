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

namespace {
    // What the environment says, as UTF-8 with '/' (for comparing with the system's answer).
    std::string fromEnvironment(const wchar_t* name) {
        wchar_t buffer[1024];
        const DWORD length = ::GetEnvironmentVariableW(name, buffer, 1024);
        return length == 0 || length >= 1024 ? std::string() : newui::normalizePath(newui::wideToUtf8(std::wstring(buffer, length)));
    }
}

TEST(SpecialFolders, EachGivesAnExistingFolderWithSlashesAndNoTrailingOne) {
    using newui::SpecialFolders;
    for (const std::string& path : { SpecialFolders::home(), SpecialFolders::documents(), SpecialFolders::localAppData(),
                                     SpecialFolders::roamingAppData(), SpecialFolders::programData(),
                                     SpecialFolders::programFiles(), SpecialFolders::temp() }) {
        ASSERT_FALSE(path.empty());
        EXPECT_EQ(path.find('\\'), std::string::npos) << path;
        EXPECT_NE(path.back(), '/') << path;
        const DWORD attributes = ::GetFileAttributesW(newui::utf8ToWide(path).c_str());
        EXPECT_NE(attributes, INVALID_FILE_ATTRIBUTES) << path;
        EXPECT_TRUE((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) << path;
    }
}

TEST(SpecialFolders, AgreeWithTheEnvironmentWhereOneExists) {
    using newui::SpecialFolders;
    EXPECT_EQ(newui::toLowerCase(SpecialFolders::localAppData()), newui::toLowerCase(fromEnvironment(L"LOCALAPPDATA")));
    EXPECT_EQ(newui::toLowerCase(SpecialFolders::roamingAppData()), newui::toLowerCase(fromEnvironment(L"APPDATA")));
    EXPECT_EQ(newui::toLowerCase(SpecialFolders::programData()), newui::toLowerCase(fromEnvironment(L"PROGRAMDATA")));
    EXPECT_EQ(newui::toLowerCase(SpecialFolders::home()), newui::toLowerCase(fromEnvironment(L"USERPROFILE")));
}

TEST(SpecialFolders, TheUserFoldersLiveUnderHome) {
    using newui::SpecialFolders;
    const std::string home = newui::toLowerCase(SpecialFolders::home());
    EXPECT_EQ(newui::toLowerCase(SpecialFolders::localAppData()).rfind(home, 0), 0u);
    EXPECT_EQ(newui::toLowerCase(SpecialFolders::roamingAppData()).rfind(home, 0), 0u);
}

TEST(SpecialFolders, CreateTempFileMakesAUniqueEmptyFileInTemp) {
    using newui::SpecialFolders;
    const std::string first = SpecialFolders::createTempFile("nui");
    const std::string second = SpecialFolders::createTempFile("nui");
    ASSERT_FALSE(first.empty());
    ASSERT_FALSE(second.empty());
    EXPECT_NE(first, second);
    EXPECT_EQ(newui::toLowerCase(first).rfind(newui::toLowerCase(SpecialFolders::temp()), 0), 0u);
    WIN32_FILE_ATTRIBUTE_DATA data{};
    ASSERT_TRUE(::GetFileAttributesExW(newui::utf8ToWide(first).c_str(), GetFileExInfoStandard, &data));
    EXPECT_EQ(data.nFileSizeLow, 0u);
    ::DeleteFileW(newui::utf8ToWide(first).c_str());
    ::DeleteFileW(newui::utf8ToWide(second).c_str());
}

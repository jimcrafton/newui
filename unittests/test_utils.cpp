#include "newui/utils.h"
#include "newui/win32/winheaders.h"

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

// ---------------------------------------------------------------------
// Portable UTF-8 <-> UTF-16 (Step 2.1 of the port plan).
// ---------------------------------------------------------------------
namespace {

// Valid text: ASCII, 2/3-byte sequences, CJK, an emoji (surrogate pair), noncharacters at
// the ends of each length class.
const char* const kValidCorpus[] = {
    "",
    "plain ascii",
    "caf\xC3\xA9 \xC3\x84rger",                 // é Ä
    "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",     // 日本語
    "smile \xF0\x9F\x98\x80 end",              // U+1F600
    "\xC2\x80\xDF\xBF\xE0\xA0\x80\xEF\xBF\xBF\xF0\x90\x80\x80\xF4\x8F\xBF\xBF",   // range edges
    "\xED\x9F\xBF\xEE\x80\x80",              // just either side of the surrogate block
    "line1\nline2\ttabbed",
};

std::string corpusEntry(const char* s) {
    return std::string(s);
}

}  // namespace

TEST(Utf16, ValidTextRoundTrips) {
    for (const char* entry : kValidCorpus) {
        const std::string text = corpusEntry(entry);
        const std::u16string wide = newui::utf8ToUtf16(text);
        EXPECT_EQ(newui::utf16ToUtf8(wide), text) << "entry: " << text;
    }
}

TEST(Utf16, MatchesTheWindowsConversionOnValidText) {
    for (const char* entry : kValidCorpus) {
        const std::string text = corpusEntry(entry);
        const std::wstring viaWindows = newui::utf8ToWide(text);
        const std::u16string portable = newui::utf8ToUtf16(text);
        ASSERT_EQ(portable.size(), viaWindows.size()) << "entry: " << text;
        for (std::size_t i = 0; i < portable.size(); ++i) {
            EXPECT_EQ(static_cast<wchar_t>(portable[i]), viaWindows[i]);
        }
        EXPECT_EQ(newui::utf16ToUtf8(portable), newui::wideToUtf8(viaWindows));
    }
}

TEST(Utf16, EmbeddedNulsSurvive) {
    const std::string text("a\0b", 3);
    EXPECT_EQ(newui::utf8ToUtf16(text), std::u16string(u"a\0b", 3));
    EXPECT_EQ(newui::utf16ToUtf8(std::u16string(u"a\0b", 3)), text);
}

TEST(Utf16, SurrogatePairsEncodeAstralCharacters) {
    const std::u16string wide = newui::utf8ToUtf16("\xF0\x9F\x98\x80");
    ASSERT_EQ(wide.size(), 2u);
    EXPECT_EQ(wide[0], u'\xD83D');
    EXPECT_EQ(wide[1], u'\xDE00');
    EXPECT_EQ(newui::utf16ToUtf8(wide), "\xF0\x9F\x98\x80");
}

TEST(Utf16, InvalidUtf8BecomesReplacementCharacters) {
    EXPECT_EQ(newui::utf8ToUtf16("a\x80z"), u"a\uFFFDz");                // stray continuation
    EXPECT_EQ(newui::utf8ToUtf16("a\xC0\xAFz"), u"a\uFFFD\uFFFDz");     // overlong '/'
    EXPECT_EQ(newui::utf8ToUtf16("a\xE6\x97z"), u"a\uFFFDz");          // truncated 3-byte
    EXPECT_EQ(newui::utf8ToUtf16("\xED\xA0\x80"), u"\uFFFD\uFFFD\uFFFD");   // encoded surrogate
    EXPECT_EQ(newui::utf8ToUtf16("\xF4\x90\x80\x80"), u"\uFFFD\uFFFD\uFFFD\uFFFD");   // > U+10FFFF
    EXPECT_EQ(newui::utf8ToUtf16("ab\xE6"), u"ab\uFFFD");               // truncated at the end
}

TEST(Utf16, UnpairedSurrogatesBecomeReplacementCharacters) {
    EXPECT_EQ(newui::utf16ToUtf8(std::u16string(u"a\xD800z")), "a\xEF\xBF\xBDz");   // lone high
    EXPECT_EQ(newui::utf16ToUtf8(std::u16string(u"a\xDC00z")), "a\xEF\xBF\xBDz");   // lone low
    EXPECT_EQ(newui::utf16ToUtf8(std::u16string(u"\xD800\xD800")), "\xEF\xBF\xBD\xEF\xBF\xBD");
}

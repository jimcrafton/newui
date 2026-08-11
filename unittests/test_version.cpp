#include "newui/newui.h"
#include "newui/version.h"

#include <gtest/gtest.h>

#include <sstream>
#include <string>
#include <vector>

namespace {

std::vector<int> splitVersion(const std::string& version) {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string part;
    while (std::getline(ss, part, '.')) {
        parts.push_back(std::stoi(part));
    }
    return parts;
}

}  // namespace

// newui::version() is Major.Minor.Release.Build. Build advances by 1 every
// build and rolls over into Release/Minor/Major (see GenerateVersion.cmake
// and root CMakeLists.txt's newui_version target) - so unlike a
// conventional fixed project version, Major/Minor/Release aren't pinned to
// CMake's project(VERSION) after the first build; that value only seeds
// VERSION.state the first time it's created. These checks are therefore
// structural (shape and bounds), not tied to a specific expected value.
TEST(Version, HasFourNumericComponents) {
    std::vector<int> parts = splitVersion(newui::version());
    ASSERT_EQ(parts.size(), 4u) << "version() = " << newui::version();

    int major = parts[0], minor = parts[1], release = parts[2], build = parts[3];
    EXPECT_GE(major, 0);
    EXPECT_GE(minor, 0);
    EXPECT_LE(minor, 99);
    EXPECT_GE(release, 0);
    EXPECT_LE(release, 999);
    EXPECT_GE(build, 0);
    EXPECT_LE(build, 9999);
}

TEST(Version, MacrosAgreeWithVersionString) {
    std::string expected = std::to_string(NEWUI_VERSION_MAJOR) + "." +
                            std::to_string(NEWUI_VERSION_MINOR) + "." +
                            std::to_string(NEWUI_VERSION_RELEASE) + "." +
                            std::to_string(NEWUI_VERSION_BUILD);
    EXPECT_STREQ(NEWUI_VERSION_STRING, expected.c_str());
    EXPECT_STREQ(newui::version(), NEWUI_VERSION_STRING);
}

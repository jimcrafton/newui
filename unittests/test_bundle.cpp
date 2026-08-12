#include "newui/bundle.h"
#include "newui/application.h"

#include <gtest/gtest.h>

#include <fstream>

// Bundle::resourcesDir() won't already exist under a fresh build's output
// directory - tests that need an on-disk resource create/remove it
// themselves, mirroring the explicit new/delete cleanup discipline
// test_layout.cpp uses for heap objects, just for the filesystem instead.

TEST(Bundle, ExecutableDirAndResourcesDirAreSane) {
    const newui::Bundle& bundle = newui::Bundle::instance();

    EXPECT_FALSE(bundle.executableDir().empty());
    EXPECT_EQ(bundle.resourcesDir(), bundle.executableDir() + "\\Resources");
}

TEST(Bundle, ResourcePathReturnsEmptyForMissingFile) {
    EXPECT_TRUE(newui::Bundle::instance().resourcePath("NoSuchFile.txt").empty());
}

TEST(Bundle, LoadImageFailsForMissingFile) {
    BLImage image;
    EXPECT_FALSE(newui::Bundle::instance().loadImage("NoSuchImage.png", image));
}

TEST(Bundle, ResourcePathAndLoadTextFileFindAnOnDiskResource) {
    const newui::Bundle& bundle = newui::Bundle::instance();

    const std::string uisDir = bundle.resourcesDir() + "\\UIs";
    ::CreateDirectoryA(bundle.resourcesDir().c_str(), nullptr);
    ::CreateDirectoryA(uisDir.c_str(), nullptr);

    const std::string filePath = uisDir + "\\test.json5";
    {
        std::ofstream file(filePath, std::ios::binary);
        file << "{ name: \"test\" }";
    }

    EXPECT_FALSE(newui::Bundle::instance().resourcePath("UIs\\test.json5").empty());
    EXPECT_EQ(newui::Bundle::instance().loadTextFile("UIs\\test.json5"), "{ name: \"test\" }");

    ::DeleteFileA(filePath.c_str());
    ::RemoveDirectoryA(uisDir.c_str());
    ::RemoveDirectoryA(bundle.resourcesDir().c_str());
}

TEST(Bundle, AppNameFallsBackToApplicationNameWithoutInfoJson) {
    newui::Application::instance().setName("bundle-test-app");
    EXPECT_EQ(newui::Bundle::instance().appName(), "bundle-test-app");

    // The fallback isn't frozen on the first name it happened to see -
    // confirms appName_ (the Info.json-only cache) was never overwritten
    // by it.
    newui::Application::instance().setName("bundle-test-app-renamed");
    EXPECT_EQ(newui::Bundle::instance().appName(), "bundle-test-app-renamed");
}

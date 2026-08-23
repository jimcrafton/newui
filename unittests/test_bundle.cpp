#include "newui/bundle.h"
#include "newui/application.h"
#include "newui/dialogs.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/view.h"

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

namespace {

    // Shared by the loadFrame()/loadDialog()/loadRootView()/loadView()
    // tests below - writes relativeName + ".newui" directly under
    // resourcesDir() (Bundle::loadFrame()/loadRootView()/loadView() all
    // resolve "<name>.newui" there, no subdirectory), creating
    // resourcesDir() itself first since a fresh build output won't
    // already have it (see this file's own header comment).
    class NewuiFileFixture : public ::testing::Test {
    protected:
        void SetUp() override {
            ::CreateDirectoryA(newui::Bundle::instance().resourcesDir().c_str(), nullptr);
        }

        void writeFile(const std::string& relativeName, const std::string& contents) {
            const std::string path = newui::Bundle::instance().resourcesDir() + "\\" + relativeName + ".newui";
            std::ofstream file(path, std::ios::binary);
            file << contents;
            writtenPaths_.push_back(path);
        }

        // Registers "<relativeName>.newui" for TearDown() cleanup without
        // writing it - for the writeFrame()/writeView() tests below, which
        // create the file themselves (via Bundle) rather than through
        // writeFile() above.
        void trackFile(const std::string& relativeName) {
            writtenPaths_.push_back(newui::Bundle::instance().resourcesDir() + "\\" + relativeName + ".newui");
        }

        void TearDown() override {
            for (const std::string& path : writtenPaths_) {
                ::DeleteFileA(path.c_str());
            }
            ::RemoveDirectoryA(newui::Bundle::instance().resourcesDir().c_str());
        }

    private:
        std::vector<std::string> writtenPaths_;
    };

}

TEST_F(NewuiFileFixture, LoadFrameAppliesTitleAndRebuildsRootViewChildren) {
    writeFile("BundleTestFrame1", R"({
        type: "Frame",
        title: "Loaded Title",
        name: "BundleTestFrame1",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "child1" },
            ],
        },
    })");

    newui::Frame frame;
    frame.setName("BundleTestFrame1");

    EXPECT_TRUE(newui::Bundle::instance().loadFrame(frame));

    EXPECT_EQ(frame.getTitle(), "Loaded Title");
    ASSERT_EQ(frame.rootView().childViews().size(), 1u);
    EXPECT_EQ(frame.rootView().childViews()[0]->name(), "child1");
}

TEST_F(NewuiFileFixture, LoadFrameFailsWithNoNameSet) {
    newui::Frame frame;
    EXPECT_FALSE(newui::Bundle::instance().loadFrame(frame));
}

TEST_F(NewuiFileFixture, LoadFrameFailsForMissingFile) {
    newui::Frame frame;
    frame.setName("NoSuchBundleTestFrame");
    EXPECT_FALSE(newui::Bundle::instance().loadFrame(frame));
}

TEST_F(NewuiFileFixture, LoadDialogDelegatesToUnderlyingFrame) {
    writeFile("BundleTestFrame2", R"({
        type: "Frame",
        title: "Dialog Title",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "dialogChild" },
            ],
        },
    })");

    newui::Dialog dialog;
    dialog.setName("BundleTestFrame2");

    EXPECT_TRUE(newui::Bundle::instance().loadDialog(dialog));

    EXPECT_EQ(dialog.getTitle(), "Dialog Title");
    ASSERT_EQ(dialog.rootView().childViews().size(), 1u);
    EXPECT_EQ(dialog.rootView().childViews()[0]->name(), "dialogChild");
}

TEST_F(NewuiFileFixture, LoadRootViewOnlyTouchesRootViewNotFramesOwnProperties) {
    writeFile("BundleTestFrame3", R"({
        type: "Frame",
        title: "Should Not Apply",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "onlyChild" },
            ],
        },
    })");

    newui::Frame frame;
    frame.setName("BundleTestFrame3");
    frame.setTitle("Original Title");

    EXPECT_TRUE(newui::Bundle::instance().loadRootView(frame.rootView()));

    EXPECT_EQ(frame.getTitle(), "Original Title");  // untouched - loadRootView() never reads Frame's own properties
    ASSERT_EQ(frame.rootView().childViews().size(), 1u);
    EXPECT_EQ(frame.rootView().childViews()[0]->name(), "onlyChild");
}

TEST_F(NewuiFileFixture, LoadViewConstructsAFreshInstanceWithChildren) {
    writeFile("BundleTestPanel", R"({
        type: "SubView",
        name: "panel",
        childViews: [
            { type: "SubView", name: "panelChild" },
        ],
    })");

    newui::View* view = newui::Bundle::instance().loadView("BundleTestPanel");
    ASSERT_NE(view, nullptr);

    EXPECT_EQ(view->name(), "panel");
    ASSERT_EQ(view->childViews().size(), 1u);
    EXPECT_EQ(view->childViews()[0]->name(), "panelChild");

    view->destroy();
    delete view;
}

TEST_F(NewuiFileFixture, LoadViewReturnsNullForMissingFile) {
    EXPECT_EQ(newui::Bundle::instance().loadView("NoSuchBundleTestPanel"), nullptr);
}

TEST_F(NewuiFileFixture, WriteFrameRoundTripsTitleAndRootViewChildren) {
    trackFile("BundleWriteFrame1");

    newui::Frame frame;
    frame.setName("BundleWriteFrame1");
    frame.setTitle("Written Title");
    frame.setBounds(newui::Rect(10, 20, 300, 200));

    newui::SubView* child = new newui::SubView();
    child->setName("writtenChild");
    frame.rootView().addChild(child);

    ASSERT_TRUE(newui::Bundle::instance().writeFrame(frame));

    newui::Frame reloaded;
    reloaded.setName("BundleWriteFrame1");
    ASSERT_TRUE(newui::Bundle::instance().loadFrame(reloaded));

    EXPECT_EQ(reloaded.getTitle(), "Written Title");
    EXPECT_EQ(reloaded.getBounds(), newui::Rect(10, 20, 300, 200));
    ASSERT_EQ(reloaded.rootView().childViews().size(), 1u);
    EXPECT_EQ(reloaded.rootView().childViews()[0]->name(), "writtenChild");
}

TEST_F(NewuiFileFixture, WriteFrameFailsWithNoNameSet) {
    newui::Frame frame;
    EXPECT_FALSE(newui::Bundle::instance().writeFrame(frame));
}

TEST_F(NewuiFileFixture, WriteDialogDelegatesToUnderlyingFrame) {
    trackFile("BundleWriteFrame2");

    newui::Dialog dialog;
    dialog.setName("BundleWriteFrame2");
    dialog.setTitle("Written Dialog Title");

    ASSERT_TRUE(newui::Bundle::instance().writeDialog(dialog));

    newui::Frame reloaded;
    reloaded.setName("BundleWriteFrame2");
    ASSERT_TRUE(newui::Bundle::instance().loadFrame(reloaded));
    EXPECT_EQ(reloaded.getTitle(), "Written Dialog Title");
}

TEST_F(NewuiFileFixture, WriteViewRoundTripsUsingItsRealRuntimeType) {
    trackFile("BundleWritePanel");

    newui::SubView panel;
    panel.setName("writtenPanel");

    newui::SubView* grandchild = new newui::SubView();
    grandchild->setName("writtenGrandchild");
    panel.addChild(grandchild);

    ASSERT_TRUE(newui::Bundle::instance().writeView(panel, "BundleWritePanel"));

    newui::View* reloaded = newui::Bundle::instance().loadView("BundleWritePanel");
    ASSERT_NE(reloaded, nullptr);

    EXPECT_EQ(reloaded->name(), "writtenPanel");
    ASSERT_EQ(reloaded->childViews().size(), 1u);
    EXPECT_EQ(reloaded->childViews()[0]->name(), "writtenGrandchild");

    reloaded->destroy();
    delete reloaded;
}

TEST_F(NewuiFileFixture, WriteViewFailsWithEmptyName) {
    newui::SubView panel;
    EXPECT_FALSE(newui::Bundle::instance().writeView(panel, ""));
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

#include "newui/bundle.h"
#include "newui/animation.h"
#include "newui/application.h"
#include "newui/controls.h"
#include "newui/dialogs.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/property.h"
#include "newui/rootview.h"
#include "newui/rootviewproxy.h"
#include "newui/subview.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <gtest/gtest.h>

#include <fstream>
#include <memory>
#include <typeindex>

// Bundle::resourcesDir() won't already exist under a fresh build's output
// directory - tests that need an on-disk resource create/remove it
// themselves, mirroring the explicit new/delete cleanup discipline
// test_layout.cpp uses for heap objects, just for the filesystem instead.

TEST(Bundle, ExecutableDirAndResourcesDirAreSane) {
    const newui::Bundle& bundle = newui::Bundle::instance();

    EXPECT_FALSE(bundle.executableDir().empty());
    EXPECT_EQ(bundle.resourcesDir(), bundle.executableDir() + "\\Resources");
}

// Bundle is a shared singleton - every test here that calls
// setExecutableDirOverride() must restore the real value ("") before
// returning, or every later test in this binary (most of which rely on
// resourcesDir() pointing at the real build output) breaks.
TEST(Bundle, ExecutableDirOverrideChangesResourcesDirToo) {
    newui::Bundle& bundle = newui::Bundle::instance();
    const std::string realExecutableDir = bundle.executableDir();

    bundle.setExecutableDirOverride("C:\\SomeOverriddenRoot");
    EXPECT_EQ(bundle.executableDir(), "C:\\SomeOverriddenRoot");
    EXPECT_EQ(bundle.resourcesDir(), "C:\\SomeOverriddenRoot\\Resources");

    bundle.setExecutableDirOverride("");
    EXPECT_EQ(bundle.executableDir(), realExecutableDir);
}

TEST(Bundle, ExecutableDirOverrideAffectsResourcePathResolution) {
    newui::Bundle& bundle = newui::Bundle::instance();
    const std::string realExecutableDir = bundle.executableDir();

    char tempPathBuf[MAX_PATH]{};
    ::GetTempPathA(MAX_PATH, tempPathBuf);
    const std::string overrideRoot = std::string(tempPathBuf) + "BundleOverrideTest";
    const std::string overrideResources = overrideRoot + "\\Resources";
    ::CreateDirectoryA(overrideRoot.c_str(), nullptr);
    ::CreateDirectoryA(overrideResources.c_str(), nullptr);

    const std::string filePath = overrideResources + "\\overrideProbe.txt";
    {
        std::ofstream file(filePath, std::ios::binary);
        file << "hello from override root";
    }

    bundle.setExecutableDirOverride(overrideRoot);
    EXPECT_EQ(bundle.loadTextFile("overrideProbe.txt"), "hello from override root");

    bundle.setExecutableDirOverride("");
    EXPECT_EQ(bundle.executableDir(), realExecutableDir);
    EXPECT_TRUE(bundle.loadTextFile("overrideProbe.txt").empty());  // no longer resolves once restored

    ::DeleteFileA(filePath.c_str());
    ::RemoveDirectoryA(overrideResources.c_str());
    ::RemoveDirectoryA(overrideRoot.c_str());
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

TEST_F(NewuiFileFixture, LoadFrameAppliesBoundsAndVisibleOntoRebuiltChildren) {
    writeFile("BundleTestFrame1b", R"({
        type: "Frame",
        title: "Loaded Title",
        name: "BundleTestFrame1b",
        rootView: {
            type: "RootView",
            bounds: { type: "Rect", pos: { type: "Point", x: 0, y: 0 }, size: { type: "Size", width: 464, height: 221 } },
            childViews: [
                { type: "SubView",
                  bounds: { type: "Rect", pos: { type: "Point", x: 16, y: 16 }, size: { type: "Size", width: 432, height: 24 } },
                  visible: true, name: "child1" },
            ],
        },
    })");

    newui::Frame frame;
    frame.setName("BundleTestFrame1b");

    ASSERT_TRUE(newui::Bundle::instance().loadFrame(frame));

    ASSERT_EQ(frame.rootView().childViews().size(), 1u);
    newui::SubView* child = frame.rootView().childViews()[0];
    EXPECT_TRUE(child->isVisible());
    EXPECT_FLOAT_EQ(child->bounds().pos().x, 16.0f);
    EXPECT_FLOAT_EQ(child->bounds().pos().y, 16.0f);
    EXPECT_FLOAT_EQ(child->bounds().size().width, 432.0f);
    EXPECT_FLOAT_EQ(child->bounds().size().height, 24.0f);
}

// Real, reproduced bug (HANDOFF.md's entry on making View::layout()/
// style() genuinely reflected properties): a saved document that
// predates "style"/"layout" becoming reflected (or was hand-written
// without them) has no "style" key at all for a freshly-reconstructed
// child - TypedClass<T>::read()'s own fallback (no resolved-subclass
// "type" tag found under a missing key, fall back to ValueT's own Class)
// used to unconditionally createInstance() a *blank* ViewStyle and
// overwrite whatever real style the child's own C++ constructor had
// already set up (e.g. Button's real ButtonStyle) - accidentally
// harmless only while ViewStyle itself had no registered zero-arg
// constructor (createInstance() silently failed, leaving the real style
// alone by luck, not by design). Real crash this caused live:
// Button::paint() reading a field ButtonStyle has but base ViewStyle
// doesn't, off the just-substituted blank object.
TEST_F(NewuiFileFixture, LoadFrameLeavesAFreshChildsOwnConstructorSetStyleAloneWhenTheDocumentHasNone) {
    writeFile("BundleTestFrameNoStyle", R"({
        type: "Frame",
        name: "BundleTestFrameNoStyle",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "Button", name: "plainButton" },
            ],
        },
    })");

    newui::Frame frame;
    frame.setName("BundleTestFrameNoStyle");

    ASSERT_TRUE(newui::Bundle::instance().loadFrame(frame));

    ASSERT_EQ(frame.rootView().childViews().size(), 1u);
    newui::SubView* button = frame.rootView().childViews()[0];
    // Button::Button() (controls.cpp) sets up a real ThemedButtonStyle -
    // typeid(), not dynamic_cast<ButtonStyle*>, since ThemedButtonStyle
    // isn't a ButtonStyle subclass at all (a separate ThemedViewStyle-
    // derived branch) - the point here is just "not the blank base
    // ViewStyle a wrongly-reconstructed style would be".
    EXPECT_NE(std::type_index(typeid(button->style())), std::type_index(typeid(newui::ViewStyle)));
}

// Closes the type-safety gap TypedPropertyCollection::readAndAddItem()'s
// position-based reuse (bug #4 above) left open: reusing a childViews
// slot by INDEX alone is only safe if the saved element is still the SAME
// type as what's already living there. Reloading the same live Frame a
// second time, with the document now naming a different type at the same
// childViews[0] position, must not read Button-shaped data into the
// still-live SubView (or vice versa) - ClassReader::peekElementType()
// (a side-effect-free peek at the array element's "type" tag) lets
// readAndAddItem() detect the mismatch and remove+destroy the stale
// element before constructing a fresh, correctly-typed replacement.
TEST_F(NewuiFileFixture, LoadFrameReplacesAChildWhoseTypeChangedAtTheSamePosition) {
    writeFile("BundleTestFrameTypeSwap", R"({
        type: "Frame",
        name: "BundleTestFrameTypeSwap",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "child1" },
            ],
        },
    })");

    newui::Frame frame;
    frame.setName("BundleTestFrameTypeSwap");

    ASSERT_TRUE(newui::Bundle::instance().loadFrame(frame));
    ASSERT_EQ(frame.rootView().childViews().size(), 1u);
    ASSERT_EQ(std::type_index(typeid(*frame.rootView().childViews()[0])), std::type_index(typeid(newui::SubView)));

    writeFile("BundleTestFrameTypeSwap", R"({
        type: "Frame",
        name: "BundleTestFrameTypeSwap",
        rootView: {
            type: "RootView",
            childViews: [
                { type: "Button", name: "child1Button" },
            ],
        },
    })");

    ASSERT_TRUE(newui::Bundle::instance().loadFrame(frame));

    ASSERT_EQ(frame.rootView().childViews().size(), 1u);
    newui::SubView* replaced = frame.rootView().childViews()[0];
    EXPECT_EQ(std::type_index(typeid(*replaced)), std::type_index(typeid(newui::Button)));
    EXPECT_EQ(replaced->name(), "child1Button");
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

// Frame-less overload - for a standalone RootView (see RootView's
// Frame-less constructor), which has no Frame to pull the bundle name
// from, so the caller supplies it explicitly instead.
TEST_F(NewuiFileFixture, LoadRootViewWithExplicitBundleNameWorksWithNoFrame) {
    writeFile("BundleTestFrameless", R"({
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "onlyChild" },
            ],
        },
    })");

    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "standaloneRoot");
    ASSERT_EQ(root.getFrame(), nullptr);

    EXPECT_TRUE(newui::Bundle::instance().loadRootView(root, "BundleTestFrameless"));

    ASSERT_EQ(root.childViews().size(), 1u);
    EXPECT_EQ(root.childViews()[0]->name(), "onlyChild");
}

TEST_F(NewuiFileFixture, LoadRootViewWithExplicitBundleNameFailsWhenNameIsEmpty) {
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "standaloneRoot");

    EXPECT_FALSE(newui::Bundle::instance().loadRootView(root, ""));
}

// loadRootView()/writeRootView() are templated (Bundle::loadRootView<T>()/
// writeRootView<T>()) specifically so a registered proxy class (Class::
// proxy()/proxyFor()) can be loaded/written the same way a real RootView
// is - RootViewProxy (newui/rootviewproxy.h) can't be loaded as a real
// RootView& at all (unrelated, non-inheriting types - see its own class
// comment), so this is the real, only way DesignerEditor (cpp_codetools)
// can load a document into one.
TEST_F(NewuiFileFixture, LoadRootViewIntoARegisteredProxyTypeAlsoWorks) {
    writeFile("BundleTestProxyLoad", R"({
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "onlyChild" },
            ],
        },
    })");

    newui::RootViewProxy proxy;
    EXPECT_TRUE(newui::Bundle::instance().loadRootView(proxy, "BundleTestProxyLoad"));

    ASSERT_EQ(proxy.childViews().size(), 1u);
    EXPECT_EQ(proxy.childViews()[0]->name(), "onlyChild");
}

TEST_F(NewuiFileFixture, LoadRootViewWithDesignModePropagatesDesignTimeOntoFreshChildren) {
    writeFile("BundleTestProxyDesignMode", R"({
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "onlyChild" },
            ],
        },
    })");

    newui::RootViewProxy proxy;
    ASSERT_TRUE(newui::Bundle::instance().loadRootView(proxy, "BundleTestProxyDesignMode", /*designMode=*/true));

    ASSERT_EQ(proxy.childViews().size(), 1u);
    EXPECT_TRUE(proxy.childViews()[0]->isDesignTime());
}

TEST_F(NewuiFileFixture, LoadRootViewWithoutDesignModeLeavesFreshChildrenNotDesignTime) {
    writeFile("BundleTestProxyNoDesignMode", R"({
        rootView: {
            type: "RootView",
            childViews: [
                { type: "SubView", name: "onlyChild" },
            ],
        },
    })");

    newui::RootViewProxy proxy;
    ASSERT_TRUE(newui::Bundle::instance().loadRootView(proxy, "BundleTestProxyNoDesignMode"));  // designMode defaults false

    ASSERT_EQ(proxy.childViews().size(), 1u);
    EXPECT_FALSE(proxy.childViews()[0]->isDesignTime());
}

TEST_F(NewuiFileFixture, WriteRootViewFromAProxyWithDesignModeWritesTheRealClassName) {
    trackFile("BundleTestProxyWriteDesignMode");

    newui::RootViewProxy proxy;
    ASSERT_TRUE(newui::Bundle::instance().writeRootView(proxy, "BundleTestProxyWriteDesignMode", /*designMode=*/true));

    // A real running app's own loadRootView(RootView&, name) never actually
    // consults this "type" tag (readNested() always trusts its own static
    // T - see its own comment, reflectionio.h), so this is really about
    // the saved file being human-legible/consistent, not correctness of
    // any real load path - still worth locking down explicitly.
    std::string written = newui::Bundle::instance().loadTextFile("BundleTestProxyWriteDesignMode.newui");
    EXPECT_NE(written.find("RootView"), std::string::npos);
    EXPECT_EQ(written.find("RootViewProxy"), std::string::npos);
}

TEST_F(NewuiFileFixture, WriteRootViewFromAProxyWithoutDesignModeWritesItsOwnClassName) {
    trackFile("BundleTestProxyWriteNoDesignMode");

    newui::RootViewProxy proxy;
    ASSERT_TRUE(newui::Bundle::instance().writeRootView(proxy, "BundleTestProxyWriteNoDesignMode"));  // designMode defaults false

    std::string written = newui::Bundle::instance().loadTextFile("BundleTestProxyWriteNoDesignMode.newui");
    EXPECT_NE(written.find("RootViewProxy"), std::string::npos);
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

TEST_F(NewuiFileFixture, WriteRootViewRoundTripsChildrenWithNoExistingFile) {
    trackFile("BundleWriteRootViewFresh");

    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "standaloneRoot");
    newui::SubView* child = new newui::SubView();
    child->setName("writtenChild");
    root.addChild(child);

    ASSERT_TRUE(newui::Bundle::instance().writeRootView(root, "BundleWriteRootViewFresh"));

    newui::RootView reloaded(nullptr, newui::Rect(0, 0, 10, 10), "standaloneRoot");
    ASSERT_TRUE(newui::Bundle::instance().loadRootView(reloaded, "BundleWriteRootViewFresh"));
    ASSERT_EQ(reloaded.childViews().size(), 1u);
    EXPECT_EQ(reloaded.childViews()[0]->name(), "writtenChild");
}

TEST_F(NewuiFileFixture, WriteRootViewFailsWithEmptyBundleName) {
    newui::RootView root(nullptr, newui::Rect(0, 0, 10, 10), "standaloneRoot");
    EXPECT_FALSE(newui::Bundle::instance().writeRootView(root, ""));
}

// The real point of writeRootView() over writeView(): a Frame-shaped file
// already on disk (title/bounds, as a real Frame elsewhere would still
// loadFrame() from) keeps its other top-level keys when only the
// rootView is re-saved through a Frame-less DesignerEditor-style edit.
TEST_F(NewuiFileFixture, WriteRootViewPreservesOtherTopLevelKeysFromAnExistingFile) {
    trackFile("BundleWriteRootViewMerge");

    newui::Frame originalFrame;
    originalFrame.setName("BundleWriteRootViewMerge");
    originalFrame.setTitle("Original Frame Title");
    originalFrame.setBounds(newui::Rect(5, 6, 400, 300));
    newui::SubView* oldChild = new newui::SubView();
    oldChild->setName("oldChild");
    originalFrame.rootView().addChild(oldChild);
    ASSERT_TRUE(newui::Bundle::instance().writeFrame(originalFrame));

    // A totally separate, Frame-less RootView - standing in for
    // DesignerEditor's own standalone tree - with different content.
    newui::RootView editedRoot(nullptr, newui::Rect(0, 0, 10, 10), "standaloneRoot");
    newui::SubView* newChild = new newui::SubView();
    newChild->setName("newChild");
    editedRoot.addChild(newChild);

    ASSERT_TRUE(newui::Bundle::instance().writeRootView(editedRoot, "BundleWriteRootViewMerge"));

    newui::Frame reloadedFrame;
    reloadedFrame.setName("BundleWriteRootViewMerge");
    ASSERT_TRUE(newui::Bundle::instance().loadFrame(reloadedFrame));

    // title/bounds survived - never touched by writeRootView().
    EXPECT_EQ(reloadedFrame.getTitle(), "Original Frame Title");
    EXPECT_EQ(reloadedFrame.getBounds(), newui::Rect(5, 6, 400, 300));

    // rootView reflects the new edit, not the old content.
    ASSERT_EQ(reloadedFrame.rootView().childViews().size(), 1u);
    EXPECT_EQ(reloadedFrame.rootView().childViews()[0]->name(), "newChild");
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

// Real, previously-not-reflected-at-all properties (HANDOFF.md's entry on
// teaching ClassBuilder::property()/reflectgen to pair a T*-or-T&-
// returning getter against an ownership-taking std::unique_ptr<T> setter -
// View::layout()/setLayout(), View::style()/setStyle()) - round-tripped
// through the same real Bundle::writeView()/loadView() path the plain
// tree-structure tests above already use, not just "the build didn't
// break".
TEST_F(NewuiFileFixture, WriteViewRoundTripsItsLayoutAndStyle) {
    trackFile("BundleWriteLayoutStyle");

    newui::SubView panel;
    panel.setName("layoutStylePanel");
    auto flex = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    flex->setSpacing(7.0f);
    flex->setPadding(3.0f);
    panel.setLayout(std::move(flex));

    auto label = std::make_unique<newui::LabelStyle>();
    label->text = "Hello from style()";
    panel.setStyle(std::move(label));

    ASSERT_TRUE(newui::Bundle::instance().writeView(panel, "BundleWriteLayoutStyle"));

    newui::View* reloaded = newui::Bundle::instance().loadView("BundleWriteLayoutStyle");
    ASSERT_NE(reloaded, nullptr);

    auto* reloadedLayout = dynamic_cast<newui::FlexLayout*>(reloaded->layout());
    ASSERT_NE(reloadedLayout, nullptr);
    EXPECT_EQ(reloadedLayout->orientation(), newui::Orientation::Horizontal);
    EXPECT_FLOAT_EQ(reloadedLayout->spacing(), 7.0f);
    EXPECT_FLOAT_EQ(reloadedLayout->padding(), 3.0f);

    auto* reloadedStyle = dynamic_cast<newui::LabelStyle*>(&reloaded->style());
    ASSERT_NE(reloadedStyle, nullptr);
    EXPECT_EQ(reloadedStyle->text, "Hello from style()");

    reloaded->destroy();
    delete reloaded;
}

// ---------------------------------------------------------------------
// Animation persistence - see HANDOFF.md's own entry on this pass. Round-
// trips a real Animation targeting a real View-tree property (Slider::
// value(), reflectgen-registered - see controls.h) through Bundle::
// writeFrame()/loadFrame(), same as this file's other WriteFrame*/
// LoadFrame* tests do for ordinary View properties.
TEST_F(NewuiFileFixture, WriteFrameThenLoadFrameRoundTripsAnAnimationTargetingARealViewProperty) {
    trackFile("BundleAnimationFrame1");

    newui::AnimationTargetRegistry::registerTarget<newui::Slider, float>();

    newui::Frame frame;
    frame.setName("BundleAnimationFrame1");

    newui::Slider* slider = new newui::Slider();
    slider->setName("mySlider");
    slider->setValue(0.0f);
    frame.rootView().addChild(slider);

    auto* valueProp = newui::PropertyManager::registerProperty<float>(slider, "value");
    newui::Animation* anim = newui::AnimationManager::addAnimation("sliderAnim", 0, 10);
    newui::Key* startKey = anim->addKey("start", 0);
    startKey->setValue(valueProp, 0.0f);
    newui::Key* endKey = anim->addKey("end", 10);
    endKey->setValue(valueProp, 100.0f);

    ASSERT_TRUE(newui::Bundle::instance().writeFrame(frame));

    newui::AnimationManager::clear();
    newui::PropertyManager::clear();

    newui::Frame reloaded;
    reloaded.setName("BundleAnimationFrame1");
    ASSERT_TRUE(newui::Bundle::instance().loadFrame(reloaded));

    ASSERT_EQ(reloaded.rootView().childViews().size(), 1u);
    newui::Slider* reloadedSlider = static_cast<newui::Slider*>(reloaded.rootView().childViews()[0]);
    EXPECT_EQ(reloadedSlider->name(), "mySlider");

    // Real, reproduced bug (HANDOFF.md's entry on making View::layout()/
    // style() genuinely reflected properties, which is what first
    // exposed this): Slider's own constructor already builds and
    // addChild()'s a real thumb_ before any reflection reading starts -
    // childViews' add-only reconstruction used to have no way to know
    // that, and added a second (then third, ...) duplicate thumb on
    // every loadFrame() call. Exactly one child expected here, and it
    // should be the SAME object Slider::thumb() itself already points
    // at (identity preserved, not a fresh unrelated reconstruction) -
    // this specific slider round-trips its own style() too (a real
    // ThemedTrackbarThumbStyle on the thumb), which is what originally
    // crashed: reconstructing thumb's style via a fresh new/delete cycle
    // left Slider's own non-owning thumbStyle_ pointer dangling.
    ASSERT_EQ(reloadedSlider->childViews().size(), 1u);
    EXPECT_EQ(reloadedSlider->childViews()[0], reloadedSlider->thumb());

    ASSERT_EQ(newui::AnimationManager::animations().size(), 1u);
    newui::Animation* reloadedAnim = newui::AnimationManager::animations()[0].get();
    EXPECT_EQ(reloadedAnim->name(), "sliderAnim");
    EXPECT_EQ(reloadedAnim->startTime(), 0u);
    EXPECT_EQ(reloadedAnim->duration(), 10u);
    ASSERT_EQ(reloadedAnim->keys().size(), 2u);

    reloadedAnim->processFrame(10);
    EXPECT_FLOAT_EQ(reloadedSlider->value(), 100.0f);

    reloadedAnim->processFrame(0);
    EXPECT_FLOAT_EQ(reloadedSlider->value(), 0.0f);

    newui::AnimationManager::clear();
    newui::PropertyManager::clear();
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

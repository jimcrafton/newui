#include "newui/serialization.h"
#include "newui/color.h"

#include <gtest/gtest.h>

// Views are heap-only, never stack-allocated - see View's class comment -
// so every tree a test needs is built with new and torn down via
// destroy()+delete at the end, same convention as test_layout.cpp.

namespace {

// A test-local SubView subclass with no fields of its own, used to check
// that SerializationRegistry::registerType<T>() round-trips a
// user-registered widget type as itself rather than as a plain SubView -
// the extension point future button/checkbox classes will rely on.
class TestWidget : public newui::SubView {
public:
    TestWidget() = default;
};

}  // namespace

TEST(Serialization, RoundTripsSubViewTree) {
    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 400, 300));
    root->setVisible(true);
    root->setLayout(std::make_unique<newui::StackLayout>(newui::Orientation::Horizontal));

    auto buttonStyle = std::make_unique<newui::ButtonStyle>();
    buttonStyle->backgroundFill = newui::Color::fromName("cornflowerblue").toBLRgba32();
    buttonStyle->borderWidth = 2.0f;
    // highlightFill deliberately left unset - checked below to stay unset
    // after round-tripping (BLVar fields are solid-color-only/omit-if-null
    // per the serialization scope).

    auto* child = new newui::SubView();
    child->setName("sidebar");
    child->setBounds(newui::Rect(0, 0, 120, 300));
    child->setVisible(true);
    child->setStyle(std::move(buttonStyle));
    child->setLayoutParams(std::make_unique<newui::StackLayoutParams>(1.0f));
    root->addChild(child);

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));

    EXPECT_EQ(loadedRoot->getBounds(), root->getBounds());

    auto* loadedLayout = dynamic_cast<newui::StackLayout*>(loadedRoot->layout());
    ASSERT_NE(loadedLayout, nullptr);
    EXPECT_EQ(loadedLayout->orientation(), newui::Orientation::Horizontal);

    ASSERT_EQ(loadedRoot->childViews().size(), 1u);
    auto* loadedChild = loadedRoot->childViews()[0];
    EXPECT_EQ(loadedChild->getName(), "sidebar");
    EXPECT_EQ(loadedChild->getBounds(), child->getBounds());
    EXPECT_TRUE(loadedChild->isVisible());

    auto* loadedStyle = dynamic_cast<newui::ButtonStyle*>(&loadedChild->style());
    ASSERT_NE(loadedStyle, nullptr);
    EXPECT_EQ(loadedStyle->borderWidth, 2.0f);
    EXPECT_FALSE(loadedStyle->backgroundFill.is_null());
    EXPECT_TRUE(loadedStyle->highlightFill.is_null());

    auto* loadedParams = dynamic_cast<newui::StackLayoutParams*>(loadedChild->layoutParams());
    ASSERT_NE(loadedParams, nullptr);
    EXPECT_EQ(loadedParams->weight, 1.0f);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, LoadFailsGracefullyOnUnknownChildType) {
    const std::string json = R"({
        name: "root", visible: true,
        bounds: { x: 0, y: 0, width: 10, height: 10 },
        style: { type: "ViewStyle" },
        children: [
            {
                type: "TotallyUnknownWidget", name: "x", visible: true,
                bounds: { x: 0, y: 0, width: 1, height: 1 },
                style: { type: "ViewStyle" },
                children: [],
            },
        ],
    })";

    auto* target = new newui::SubView();
    EXPECT_FALSE(newui::loadViewTree(*target, json));

    target->destroy();
    delete target;
}

TEST(Serialization, CustomRegisteredSubViewRoundTripsAsItself) {
    newui::SerializationRegistry::registerType<TestWidget>();

    auto* root = new newui::SubView();
    root->setBounds(newui::Rect(0, 0, 100, 100));
    root->setVisible(true);

    auto* child = new TestWidget();
    child->setName("widget");
    child->setBounds(newui::Rect(0, 0, 50, 50));
    child->setVisible(true);
    root->addChild(child);

    const std::string json = newui::saveViewTree(*root);

    auto* loadedRoot = new newui::SubView();
    ASSERT_TRUE(newui::loadViewTree(*loadedRoot, json));
    ASSERT_EQ(loadedRoot->childViews().size(), 1u);
    EXPECT_NE(dynamic_cast<TestWidget*>(loadedRoot->childViews()[0]), nullptr);

    root->destroy();
    delete root;
    loadedRoot->destroy();
    delete loadedRoot;
}

TEST(Serialization, FrameFieldsRoundTripWithoutLiveWindow) {
    // Frame is heap-only here and deliberately never deleted: ~Frame()
    // throws unless destroy() has run, and destroy() is private, only
    // reachable via a live window's WM_DESTROY (see frame.cpp) - there's
    // no way to tear one down cleanly without an actual HWND. Fine for a
    // one-off in a short-lived test process; not a pattern to reuse
    // outside a test.
    auto* frame = new newui::Frame();
    frame->setTitle("My Window");
    frame->setBounds(newui::Rect(10, 20, 800, 600));

    const std::string json = newui::saveFrame(*frame);

    auto* loaded = new newui::Frame();
    ASSERT_TRUE(newui::loadFrame(*loaded, json));
    EXPECT_EQ(loaded->getTitle(), "My Window");
    EXPECT_EQ(loaded->getBounds(), frame->getBounds());
}

TEST(Serialization, ApplicationCustomDataRoundTrips) {
    newui::Application& app = newui::Application::instance();
    app.setCustomValue("lastOpenedFile", "C:/foo/bar.txt");

    const std::string json = newui::saveApplication(app);

    app.setCustomValue("lastOpenedFile", "overwritten-before-load");
    ASSERT_TRUE(newui::loadApplication(app, json));

    std::string value;
    ASSERT_TRUE(app.getCustomValue("lastOpenedFile", value));
    EXPECT_EQ(value, "C:/foo/bar.txt");
}

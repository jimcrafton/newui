#include "newui/frameproxy.h"
#include "newui/frame.h"
#include "newui/reflection.h"

#include <gtest/gtest.h>

using namespace newui::reflection;

// registerReflectionData() is already run once globally for this whole
// binary by test_reflection.cpp's own ::testing::Environment - no separate
// registration needed here.

TEST(FrameProxy, IsARealSubViewUsableTheOrdinaryAddChildWay) {
    auto* parent = new newui::SubView();
    auto* proxy = new newui::FrameProxy();

    parent->addChild(proxy);

    ASSERT_EQ(parent->childViews().size(), 1u);
    EXPECT_EQ(parent->childViews()[0], proxy);

    delete parent;
}

TEST(FrameProxy, IsVisibleByDefault) {
    // Real, caught bug - see RootViewProxy's own identical test for why.
    auto* proxy = new newui::FrameProxy();
    EXPECT_TRUE(proxy->isVisible());
    delete proxy;
}

TEST(FrameProxy, TitleDefaultsEmptyAndRoundTripsThroughSetTitle) {
    auto* proxy = new newui::FrameProxy();
    EXPECT_TRUE(proxy->title().empty());

    proxy->setTitle("My Window");
    EXPECT_EQ(proxy->title(), "My Window");

    delete proxy;
}

TEST(FrameProxy, NameIsInheritedFromComponentNotDuplicated) {
    auto* proxy = new newui::FrameProxy();
    proxy->setName("myFrame");
    EXPECT_EQ(proxy->name(), "myFrame");
    delete proxy;
}

TEST(FrameProxy, ReflectgenRegisteredProxyForPointsAtFrame) {
    const Class* clazz = classinfo(typeid(newui::FrameProxy));
    ASSERT_NE(clazz, nullptr);
    EXPECT_EQ(clazz->proxyFor(), "Frame");
}

TEST(FrameProxy, FramesOwnProxyPointsBackAtFrameProxy) {
    const Class* clazz = classinfo(typeid(newui::Frame));
    ASSERT_NE(clazz, nullptr);
    EXPECT_EQ(clazz->proxy(), "FrameProxy");
}

TEST(FrameProxy, ProxyAndProxyForResolveToEachOtherByName) {
    const Class* frameClazz = classinfo(typeid(newui::Frame));
    const Class* proxyClazz = classinfo(typeid(newui::FrameProxy));
    ASSERT_NE(frameClazz, nullptr);
    ASSERT_NE(proxyClazz, nullptr);

    EXPECT_EQ(ReflectionRegistry::getClass(frameClazz->proxy()), proxyClazz);
    EXPECT_EQ(ReflectionRegistry::getClass(proxyClazz->proxyFor()), frameClazz);
}

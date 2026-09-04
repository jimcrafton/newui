#include "newui/rootviewproxy.h"
#include "newui/rootview.h"
#include "newui/reflection.h"

#include <gtest/gtest.h>

using namespace newui::reflection;

// registerReflectionData() is already run once globally for this whole
// binary by test_reflection.cpp's own ::testing::Environment - no separate
// registration needed here.

TEST(RootViewProxy, IsARealSubViewUsableTheOrdinaryAddChildWay) {
    auto* parent = new newui::SubView();
    auto* proxy = new newui::RootViewProxy();

    parent->addChild(proxy);  // would not compile at all if RootViewProxy weren't a real SubView

    ASSERT_EQ(parent->childViews().size(), 1u);
    EXPECT_EQ(parent->childViews()[0], proxy);

    delete parent;  // deletes proxy too, same as every other SubView child
}

TEST(RootViewProxy, PaintsAContentAreaBackgroundColor) {
    auto* proxy = new newui::RootViewProxy();
    // Not null/transparent - a real color was actually set, not left at
    // ViewStyle's own default-constructed (unset) backgroundFill.
    EXPECT_FALSE(proxy->style().backgroundFill().is_null());
    delete proxy;
}

TEST(RootViewProxy, ReflectgenRegisteredProxyForPointsAtRootView) {
    const Class* clazz = classinfo(typeid(newui::RootViewProxy));
    ASSERT_NE(clazz, nullptr);
    EXPECT_EQ(clazz->proxyFor(), "RootView");
}

TEST(RootViewProxy, RootViewsOwnProxyPointsBackAtRootViewProxy) {
    const Class* clazz = classinfo(typeid(newui::RootView));
    ASSERT_NE(clazz, nullptr);
    EXPECT_EQ(clazz->proxy(), "RootViewProxy");
}

TEST(RootViewProxy, ProxyAndProxyForResolveToEachOtherByName) {
    const Class* rootViewClazz = classinfo(typeid(newui::RootView));
    const Class* proxyClazz = classinfo(typeid(newui::RootViewProxy));
    ASSERT_NE(rootViewClazz, nullptr);
    ASSERT_NE(proxyClazz, nullptr);

    EXPECT_EQ(ReflectionRegistry::getClass(rootViewClazz->proxy()), proxyClazz);
    EXPECT_EQ(ReflectionRegistry::getClass(proxyClazz->proxyFor()), rootViewClazz);
}

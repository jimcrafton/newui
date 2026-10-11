#include "newui/rootviewproxy.h"
#include "newui/rootview.h"
#include "newui/reflection.h"
#include "newui/reflectionio.h"

#include <blend2d/blend2d.h>

#include <gtest/gtest.h>

namespace {
    // A pixel is "painted" if any of its 4 PRGB32 bytes is nonzero - true
    // regardless of channel order, since a premultiplied-alpha-0 pixel also
    // has its RGB components forced to 0.
    bool isPixelPainted(const BLImageData& data, int x, int y) {
        const uint8_t* px = static_cast<const uint8_t*>(data.pixel_data) + y * data.stride + x * 4;
        return px[0] != 0 || px[1] != 0 || px[2] != 0 || px[3] != 0;
    }
}

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

TEST(RootViewProxy, IsVisibleByDefault) {
    // Real, caught bug: View::visible_ defaults to false, and every other
    // interactive/visual newui control (ScrollBar, Button, Progress, ...)
    // explicitly calls setVisible(true) in its own constructor - missing
    // it here silently made FlexLayout::arrange() skip this proxy entirely
    // wherever it was a Splitter/FlexLayout-managed child (it filters out
    // invisible children), leaving it stuck at zero-sized bounds forever.
    auto* proxy = new newui::RootViewProxy();
    EXPECT_TRUE(proxy->isVisible());
    delete proxy;
}

TEST(RootViewProxy, PaintsAContentAreaBackgroundColor) {
    // paint() fills its own background directly (not via
    // style().setBackgroundColor()) so cornerRadius() can round just its
    // bottom two corners - see RootViewProxy::paint()'s own comment.
    // Verified here by actually rendering, not by inspecting style().
    auto* proxy = new newui::RootViewProxy();
    proxy->setBounds(newui::Rect(0.0f, 0.0f, 40.0f, 40.0f));

    BLImage surface;
    ASSERT_EQ(surface.create(40, 40, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    proxy->paint(ctx);
    ctx.end();

    BLImageData data;
    surface.get_data(&data);
    EXPECT_TRUE(isPixelPainted(data, 20, 20));

    delete proxy;
}

TEST(RootViewProxy, CornerRadiusDefaultsToZeroAndPaintsAPlainSquareBackground) {
    auto* proxy = new newui::RootViewProxy();
    EXPECT_FLOAT_EQ(proxy->cornerRadius(), 0.0f);
    proxy->setBounds(newui::Rect(0.0f, 0.0f, 40.0f, 40.0f));

    BLImage surface;
    ASSERT_EQ(surface.create(40, 40, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    proxy->paint(ctx);
    ctx.end();

    BLImageData data;
    surface.get_data(&data);
    // Every corner is square by default - even the very corner pixel is
    // painted.
    EXPECT_TRUE(isPixelPainted(data, 0, 0));
    EXPECT_TRUE(isPixelPainted(data, 39, 39));

    delete proxy;
}

TEST(RootViewProxy, CornerRadiusRoundsOnlyTheBottomTwoCorners) {
    // A hosting FrameProxy's own body has all 4 corners rounded, but its
    // title bar sits flush on top of RootViewProxy - only RootViewProxy's
    // own bottom corners are ever meant to be rounded (its top always
    // meets that square seam), see setCornerRadius()'s own comment.
    auto* proxy = new newui::RootViewProxy();
    proxy->setBounds(newui::Rect(0.0f, 0.0f, 40.0f, 40.0f));
    proxy->setCornerRadius(10.0f);

    BLImage surface;
    ASSERT_EQ(surface.create(40, 40, BL_FORMAT_PRGB32), BL_SUCCESS);
    BLContext ctx(surface);
    ctx.clear_all();
    proxy->paint(ctx);
    ctx.end();

    BLImageData data;
    surface.get_data(&data);
    // Top corners: not rounded, still painted right at the corner pixel.
    EXPECT_TRUE(isPixelPainted(data, 0, 0));
    EXPECT_TRUE(isPixelPainted(data, 39, 0));
    // Bottom corners: rounded away - the very corner pixel is untouched.
    EXPECT_FALSE(isPixelPainted(data, 0, 39));
    EXPECT_FALSE(isPixelPainted(data, 39, 39));
    // Well inside the rect - always painted regardless of rounding.
    EXPECT_TRUE(isPixelPainted(data, 20, 20));

    delete proxy;
}

TEST(RootViewProxy, LoadingADocumentWithNoCornerRadiusKeyDoesNotResetAPreSetValue) {
    // Regression test for a real bug: cornerRadius()/setCornerRadius()'s
    // getter/setter naming matched reflectgen's property-naming convention
    // closely enough to get auto-registered as a real Property before the
    // @reflect ignore=true annotations were added - Bundle::loadRootView()
    // would then silently reset it back to 0 (a loaded file has no
    // "cornerRadius" key at all), clobbering whatever a hosting FrameProxy
    // had set beforehand. See setCornerRadius()'s own comment.
    newui::RootViewProxy blank;  // a document with none of this instance's own state
    ObjectWriter writer;
    writer.write(&blank);
    ObjectReader reader;
    json5::error err = json5::from_string(json5::to_string(writer.doc), reader.doc);
    ASSERT_FALSE(err);

    newui::RootViewProxy proxy;
    proxy.setCornerRadius(8.0f);

    reader.read(&proxy);

    EXPECT_FLOAT_EQ(proxy.cornerRadius(), 8.0f);
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

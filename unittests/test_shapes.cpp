// Round-trip save/load coverage for newui::shapes::ShapeLayer through the
// same ObjectWriter/ObjectReader (JSON5) pipeline test_enumserialization.cpp/
// test_delegateserialization.cpp already exercise against their own toy
// classes - this uses the real, reflectgen-auto-generated registration for
// newui::shapes::*/newui::gfx::* (see ReflectionDataEnvironment in
// test_reflection.cpp, which this binary already runs once for every test
// here via AddGlobalTestEnvironment - no separate hand registration needed).
//
// Every Shape/ShapeLayer instance here is heap-allocated (via roundTrip()'s
// own `new InstanceT()`, wrapped in a local std::unique_ptr purely for this
// test's own cleanup), never a by-value local - same "heap-only, never
// stack/member-embedded" convention View/SubView already document (view.h).
// Shape/ShapeLayer are deliberately not move/copy-constructible (ShapeStyle
// holds a gfx::Fill/Stroke, themselves move-only because of a cached
// Image, graphics.h) - nothing in the real code ever needs them to be, so
// this stays heap-pointer-based throughout rather than adding move support
// the library itself has no use for.

// newui/newui.h must be the first include in this file - it defines
// NOMINMAX before anything pulls in <windows.h> for the first time. If
// something else (e.g. newui/utils.h, transitively via reflection.h) gets
// there first, its own #include <windows.h> (no NOMINMAX) wins the once-
// only #pragma once race and leaves the real min/max macros defined for
// the rest of this translation unit - std::numeric_limits<uint16_t>::max()
// inside json5's own header (reflectionio.h's dependency) then fails to
// compile. See feedback_no_std_minmax (memory) / utils.h's own doc comment.
#include "newui/newui.h"
#include "newui/reflection.h"
#include "newui/reflectionio.h"
#include "newui/shapes.h"

#include <memory>
#include <string>

#include <gtest/gtest.h>

using namespace newui;
using namespace newui::reflection;
// Deliberately not "using namespace newui::shapes" - newui::shapes::Rectangle
// collides with wingdi.h's own ::Rectangle(HDC, ...) function (pulled in via
// newui.h's <windows.h>), so every shapes:: type below is qualified instead.

namespace {

    // Round-trips *instance through ObjectWriter -> JSON5 text ->
    // ObjectReader into a freshly heap-allocated InstanceT - the pointer-
    // based counterpart to the by-value roundTrip() helper test_enumserial
    // ization.cpp/test_delegateserialization.cpp use for their own (movable/
    // copyable) toy classes. Caller owns the returned pointer.
    template<typename InstanceT>
    std::unique_ptr<InstanceT> roundTrip(InstanceT* instance) {
        ObjectWriter writer;
        writer.write(instance);
        std::string text = json5::to_string(writer.doc);

        ObjectReader reader;
        json5::error err = json5::from_string(text, reader.doc);
        EXPECT_FALSE(err) << "failed to parse written JSON5:\n" << text;

        auto fresh = std::make_unique<InstanceT>();
        reader.read(fresh.get());
        return fresh;
    }

}

// Regression coverage for a real bug this test caught: a collection
// property whose ElementT is itself a registered class (GradientStop is
// a plain value struct, not a pointer - unlike ShapeLayer::shapes()'s
// Shape* elements) used to write as an empty array even with real
// elements in it. TypedPropertyCollection::writeItem()'s non-pointer
// branch (reflection.h) was calling Property::writeValue() with a null
// instancePtr, but writeValue()'s own nested-Class recursion (reflection.
// cpp) only fires when instancePtr is non-null - so every element
// silently wrote nothing at all, no error, just a skipped array slot.
TEST(ShapesReflection, GradientStopsRoundTripAtTopLevel) {
    auto gradient = std::make_unique<gfx::Gradient>();
    gradient->stops().push_back(gfx::GradientStop(0.0f, Color(1.0f, 0.0f, 0.0f, 1.0f)));
    gradient->stops().push_back(gfx::GradientStop(1.0f, Color(0.0f, 0.0f, 1.0f, 1.0f)));

    std::unique_ptr<gfx::Gradient> fresh = roundTrip(gradient.get());

    ASSERT_EQ(fresh->stops().size(), 2u);
    EXPECT_FLOAT_EQ(fresh->stops()[0].offset(), 0.0f);
    EXPECT_FLOAT_EQ(fresh->stops()[0].color().r, 1.0f);
    EXPECT_FLOAT_EQ(fresh->stops()[1].offset(), 1.0f);
    EXPECT_FLOAT_EQ(fresh->stops()[1].color().b, 1.0f);
}

// Same bug, one level of nesting deeper (Fill.gradient.stops) - the shape
// examples/shapes1.cpp's own WriteShapeLayerToDisk() actually hit live.
TEST(ShapesReflection, FillGradientStopsRoundTripOneLevelNested) {
    auto fill = std::make_unique<gfx::Fill>();
    fill->gradient().stops().push_back(gfx::GradientStop(0.0f, Color(1.0f, 0.0f, 0.0f, 1.0f)));
    fill->gradient().stops().push_back(gfx::GradientStop(1.0f, Color(0.0f, 0.0f, 1.0f, 1.0f)));
    fill->setKind(gfx::PaintKind::Gradient);

    std::unique_ptr<gfx::Fill> fresh = roundTrip(fill.get());

    EXPECT_EQ(fresh->gradient().stops().size(), 2u);
}

// Same bug again, a different collection property entirely (Path::points()
// is a std::vector<Point>, not a std::vector<GradientStop>) - confirms the
// fix in TypedPropertyCollection::writeItem() is general, not specific to
// Gradient's own stops(). This is exactly what examples/shapes1.cpp's
// Curve shape hit live (its points() wrote as "[]" before this fix, even
// though the curve rendered correctly on screen - render() reads the live
// object directly, never through reflection, so that bug was invisible
// there).
TEST(ShapesReflection, PathPointsRoundTrip) {
    auto path = std::make_unique<shapes::Path>();
    path->points().push_back(Point(10.0f, 20.0f));
    path->points().push_back(Point(30.0f, 40.0f));
    path->points().push_back(Point(50.0f, 60.0f));
    path->setClosed(true);

    std::unique_ptr<shapes::Path> fresh = roundTrip(path.get());

    ASSERT_EQ(fresh->points().size(), 3u);
    EXPECT_FLOAT_EQ(fresh->points()[0].x, 10.0f);
    EXPECT_FLOAT_EQ(fresh->points()[1].y, 40.0f);
    EXPECT_FLOAT_EQ(fresh->points()[2].x, 50.0f);
    EXPECT_TRUE(fresh->closed());
}

TEST(ShapesReflection, ShapeLayerScalarPropertiesRoundTrip) {
    auto layer = std::make_unique<shapes::ShapeLayer>();
    layer->setOpacity(0.75f);
    layer->setCompositingOp(CompMultiply);

    std::unique_ptr<shapes::ShapeLayer> fresh = roundTrip(layer.get());

    EXPECT_FLOAT_EQ(fresh->opacity(), 0.75f);
    EXPECT_EQ(fresh->compositingOp(), CompMultiply);
}

TEST(ShapesReflection, CircleRoundTripsItsOwnGeometryTransformAndStyle) {
    auto circle = std::make_unique<shapes::Circle>();
    circle->setCenterX(10.0f);
    circle->setCenterY(20.0f);
    circle->setRadius(5.0f);
    circle->transform().setPosition(Point(3.0f, 4.0f));
    circle->style().setOpacity(0.5f);
    circle->style().fill().setColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
    circle->style().fill().setKind(gfx::PaintKind::Color);
    circle->style().stroke().setWidth(2.5f);

    std::unique_ptr<shapes::Circle> fresh = roundTrip(circle.get());

    EXPECT_FLOAT_EQ(fresh->centerX(), 10.0f);
    EXPECT_FLOAT_EQ(fresh->centerY(), 20.0f);
    EXPECT_FLOAT_EQ(fresh->radius(), 5.0f);
    EXPECT_FLOAT_EQ(fresh->transform().position().x, 3.0f);
    EXPECT_FLOAT_EQ(fresh->transform().position().y, 4.0f);
    EXPECT_FLOAT_EQ(fresh->style().opacity(), 0.5f);
    EXPECT_EQ(fresh->style().fill().kind(), gfx::PaintKind::Color);
    EXPECT_FLOAT_EQ(fresh->style().fill().color().r, 1.0f);
    EXPECT_FLOAT_EQ(fresh->style().fill().color().g, 0.0f);
    EXPECT_FLOAT_EQ(fresh->style().stroke().width(), 2.5f);
}

// The real target: a ShapeLayer holding a mix of concrete Shape subtypes,
// round-tripped as a whole - shapes() is a std::vector<Shape*> (a
// polymorphic-by-pointer collection, see ShapeLayer's own class comment),
// so this also exercises whatever ObjectReader does (or doesn't yet do -
// see TypedClass<T>::read()'s own comment in reflection.h) with a "type"
// tag that names a *more-derived* class than the collection's statically-
// declared Shape element type.
TEST(ShapesReflection, ShapeLayerRoundTripsItsMixedShapeCollection) {
    auto layer = std::make_unique<shapes::ShapeLayer>();

    shapes::Circle* circle = new shapes::Circle();
    circle->setCenterX(10.0f);
    circle->setCenterY(20.0f);
    circle->setRadius(5.0f);
    layer->addShape(circle);  // layer now owns circle - deleted by ~ShapeLayer()

    shapes::Rectangle* rect = new shapes::Rectangle();
    rect->setX(1.0f);
    rect->setY(2.0f);
    rect->setWidth(30.0f);
    rect->setHeight(40.0f);
    layer->addShape(rect);  // layer now owns rect - deleted by ~ShapeLayer()

    std::unique_ptr<shapes::ShapeLayer> fresh = roundTrip(layer.get());

    ASSERT_EQ(fresh->shapes().size(), 2u)
        << "ShapeLayer::shapes() didn't reconstruct the same number of shapes it was saved with";

    shapes::Circle* freshCircle = dynamic_cast<shapes::Circle*>(fresh->shapes()[0]);
    ASSERT_NE(freshCircle, nullptr) << "shapes()[0] didn't come back as a Circle";
    EXPECT_FLOAT_EQ(freshCircle->centerX(), 10.0f);
    EXPECT_FLOAT_EQ(freshCircle->centerY(), 20.0f);
    EXPECT_FLOAT_EQ(freshCircle->radius(), 5.0f);

    shapes::Rectangle* freshRect = dynamic_cast<shapes::Rectangle*>(fresh->shapes()[1]);
    ASSERT_NE(freshRect, nullptr) << "shapes()[1] didn't come back as a Rectangle";
    EXPECT_FLOAT_EQ(freshRect->x(), 1.0f);
    EXPECT_FLOAT_EQ(freshRect->y(), 2.0f);
    EXPECT_FLOAT_EQ(freshRect->width(), 30.0f);
    EXPECT_FLOAT_EQ(freshRect->height(), 40.0f);
}

#include "newui/newui.h"
#include "newui/property.h"

#include "newui/geometry.h"
#include "newui/shapes.h"

#include <gtest/gtest.h>

namespace {

int g_changeCount = 0;
std::string g_lastChangedName;

// A template, not a plain function, since onValueChanged's Sender is now
// the fully-typed Property<SourceT, ValueT> (not PropertyBase), and it
// also passes the source and the (already-updated) field directly as
// extra args - see property.h's ValueChangedDelegate. Taking
// &RecordChange in a context expecting a specific
// SyncReturn(*)(Property<SourceT, ValueT>&, SourceT*, ValueT*) deduces
// ValueT/SourceT from that target type, so this one template still
// serves every Property type used below without needing a differently-
// named callback per type.
template<typename SourceT, typename ValueT>
newui::SyncReturn RecordChange(newui::ObservableProperty<SourceT, ValueT>& property, SourceT*, ValueT*) {
    ++g_changeCount;
    g_lastChangedName = property.name();
    return newui::SyncReturn::Handled;
}

void ResetRecorder() {
    g_changeCount = 0;
    g_lastChangedName.clear();
}

}  // namespace

TEST(Property, GetReturnsFieldsCurrentValue) {
    newui::PropertyManager::instance().clear();
    int field = 42;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "value");
    EXPECT_EQ(property->get(), 42);
}

TEST(Property, SetWritesThroughToField) {
    newui::PropertyManager::instance().clear();
    int field = 0;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "value");
    property->set(7);
    EXPECT_EQ(field, 7);
    EXPECT_EQ(property->get(), 7);
}

TEST(Property, SetFiresOnValueChangedWhenValueDiffers) {
    newui::PropertyManager::instance().clear();
    ResetRecorder();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "opacity");
    property->onValueChanged.add(&RecordChange);

    property->set(1.0f);

    EXPECT_EQ(g_changeCount, 1);
    EXPECT_EQ(g_lastChangedName, "opacity");
}

TEST(Property, SetDoesNotFireOnValueChangedWhenValueIsUnchanged) {
    newui::PropertyManager::instance().clear();
    ResetRecorder();
    int field = 5;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "value");
    property->onValueChanged.add(&RecordChange);

    property->set(5);

    EXPECT_EQ(g_changeCount, 0);
}

TEST(Property, InterpolateLinearAtEndpointsMatchesStartAndEnd) {
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    property->setupInterpolation(10.0f, 20.0f);

    property->interpolate(0.0f);
    EXPECT_FLOAT_EQ(field, 10.0f);

    property->interpolate(1.0f);
    EXPECT_FLOAT_EQ(field, 20.0f);
}

TEST(Property, InterpolateLinearAtMidpointIsAverage) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    property->setupInterpolation(0.0f, 100.0f);

    property->interpolate(0.5f);

    EXPECT_FLOAT_EQ(field, 50.0f);
}

TEST(Property, InterpolateDoesNotChangeFieldUntilSetupInterpolationIsCalled) {
    // setupInterpolation() only establishes start/end; it should not itself
    // write the field the way interpolate() does.
    newui::PropertyManager::instance().clear();
    int field = 3;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "value");
    property->setupInterpolation(0, 10);
    EXPECT_EQ(field, 3);
}

TEST(Property, InterpolateEaseInStartsSlow) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    property->setupInterpolation(0.0f, 100.0f, newui::InterpolationKind::EaseIn);

    property->interpolate(0.5f);

    // EaseIn is t^2, so t=0.5 lands well below the linear midpoint.
    EXPECT_LT(field, 50.0f);
}

TEST(Property, InterpolateEaseOutStartsFast) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    property->setupInterpolation(0.0f, 100.0f, newui::InterpolationKind::EaseOut);

    property->interpolate(0.5f);

    // EaseOut is t*(2-t), so t=0.5 lands well above the linear midpoint.
    EXPECT_GT(field, 50.0f);
}

TEST(Property, InterpolateEaseInOutMatchesEndpoints) {
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    property->setupInterpolation(0.0f, 100.0f, newui::InterpolationKind::EaseInOut);

    property->interpolate(0.0f);
    EXPECT_FLOAT_EQ(field, 0.0f);

    property->interpolate(1.0f);
    EXPECT_FLOAT_EQ(field, 100.0f);
}

TEST(Property, InterpolateWithCustomFunctionUsesSetupInterpolationRange) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    property->setupInterpolation(0.0f, 100.0f);

    // A custom interpolator that always returns the midpoint of start/end,
    // ignoring t entirely - proves fn (not the built-in easing) drives the
    // result.
    property->interpolate(0.9f, [](float start, float end, float) {
        return (start + end) * 0.5f;
    });

    EXPECT_FLOAT_EQ(field, 50.0f);
}

TEST(Property, InterpolateWithCustomFunctionCanCapture) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    property->setupInterpolation(0.0f, 10.0f);

    float bias = 3.0f;
    property->interpolate(1.0f, [bias](float start, float end, float t) {
        return start + (end - start) * t + bias;
    });

    EXPECT_FLOAT_EQ(field, 13.0f);
}

TEST(Property, InterpolateWithKeyframesAtEndpointsMatchesFirstAndLast) {
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    std::vector<float> keyframes{0.0f, 5.0f, 100.0f};

    property->interpolate(0.0f, keyframes);
    EXPECT_FLOAT_EQ(field, 0.0f);

    property->interpolate(1.0f, keyframes);
    EXPECT_FLOAT_EQ(field, 100.0f);
}

TEST(Property, InterpolateWithKeyframesSamplesWithinSegment) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    std::vector<float> keyframes{0.0f, 10.0f, 20.0f};

    // t=0.25 is a quarter of the way through the full [0,1] span, which
    // with 3 keyframes (2 segments) is halfway through the first segment
    // (0.0 -> 10.0).
    property->interpolate(0.25f, keyframes);

    EXPECT_FLOAT_EQ(field, 5.0f);
}

TEST(Property, InterpolateWithSingleKeyframeSetsThatValue) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    std::vector<float> keyframes{42.0f};

    property->interpolate(0.7f, keyframes);

    EXPECT_FLOAT_EQ(field, 42.0f);
}

TEST(Property, InterpolateWithEmptyKeyframesIsANoop) {
    newui::PropertyManager::instance().clear();
    float field = 9.0f;
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "t");
    std::vector<float> keyframes;

    property->interpolate(0.5f, keyframes);

    EXPECT_FLOAT_EQ(field, 9.0f);
}

TEST(Property, StructFieldGetReturnsFieldsCurrentValue) {
    newui::PropertyManager::instance().clear();
    newui::Point field(1.0f, 2.0f);
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "pos");
    EXPECT_EQ(property->get(), newui::Point(1.0f, 2.0f));
}

TEST(Property, StructFieldSetWritesThroughAndFiresOnValueChanged) {
    newui::PropertyManager::instance().clear();
    ResetRecorder();
    newui::Point field(0.0f, 0.0f);
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "pos");
    property->onValueChanged.add(&RecordChange);

    property->set(newui::Point(3.0f, 4.0f));

    EXPECT_EQ(field, newui::Point(3.0f, 4.0f));
    EXPECT_EQ(g_changeCount, 1);
}

TEST(Property, StructFieldSetDoesNotFireWhenUnchanged) {
    newui::PropertyManager::instance().clear();
    ResetRecorder();
    newui::Point field(5.0f, 6.0f);
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "pos");
    property->onValueChanged.add(&RecordChange);

    property->set(newui::Point(5.0f, 6.0f));

    EXPECT_EQ(g_changeCount, 0);
}

TEST(Property, StructFieldInterpolatesComponentwiseViaCustomFunction) {
    newui::PropertyManager::instance().clear();
    newui::Point field(0.0f, 0.0f);
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "pos");
    property->setupInterpolation(newui::Point(0.0f, 0.0f), newui::Point(10.0f, 20.0f));

    property->interpolate(0.5f, [](newui::Point start, newui::Point end, float t) {
        return newui::Point(start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t);
        });

    EXPECT_FLOAT_EQ(field.x, 5.0f);
    EXPECT_FLOAT_EQ(field.y, 10.0f);
}

TEST(Property, StructFieldBuiltInInterpolateKindThrows) {
    newui::PropertyManager::instance().clear();
    newui::Point field(0.0f, 0.0f);
    auto* property = newui::PropertyManager::instance().registerProperty(&field, &field, "pos");
    property->setupInterpolation(newui::Point(0.0f, 0.0f), newui::Point(1.0f, 1.0f));

    EXPECT_THROW(property->interpolate(0.5f), std::logic_error);
}

// Property<Point>::interpolate(t, const std::vector<float>&) is removed via
// SFINAE for non-arithmetic T (see property.h) rather than failing at
// runtime, so there's no way to exercise it from a normal test - a call
// like `property->interpolate(0.5f, std::vector<float>{0, 1})` simply
// wouldn't compile. Left as documentation, same as the analogous
// Delegate<FakeSender&, ...> case in test_delegate.cpp.

// ---------------------------------------------------------------------
// ObservableProperty backed by a reflection::Property (PropertyManager::
// registerProperty<ValueT>(source, "propertyName")) instead of a raw
// field address - newui::shapes::Circle is a real, everyday case of
// exactly what this is for: private fields, only a getter/setter pair
// exposed (see shapes.h's own class comment). Circle's own reflection
// data is registered once, globally, by whatever GTest environment in
// this binary calls registerReflectionData() (see test_reflection.cpp) -
// every test below relies on that already having happened by the time it
// runs, the same way every other reflection-touching test in this binary
// does.
// ---------------------------------------------------------------------

TEST(Property, ReflectedPropertyGetReturnsTheLiveGetterValue) {
    newui::PropertyManager::instance().clear();
    newui::shapes::Circle circle;
    circle.setRadius(12.5f);

    auto* property = newui::PropertyManager::instance().registerProperty<float>(&circle, "radius");

    ASSERT_NE(property, nullptr);
    EXPECT_FLOAT_EQ(property->get(), 12.5f);
}

TEST(Property, ReflectedPropertySetWritesThroughTheRealSetter) {
    newui::PropertyManager::instance().clear();
    newui::shapes::Circle circle;

    auto* property = newui::PropertyManager::instance().registerProperty<float>(&circle, "radius");
    property->set(40.0f);

    EXPECT_FLOAT_EQ(circle.radius(), 40.0f);
    EXPECT_FLOAT_EQ(property->get(), 40.0f);
}

TEST(Property, ReflectedPropertySetDoesNotFireOnValueChangedWhenUnchanged) {
    newui::PropertyManager::instance().clear();
    ResetRecorder();
    newui::shapes::Circle circle;
    circle.setCenterX(5.0f);

    auto* property = newui::PropertyManager::instance().registerProperty<float>(&circle, "centerX");
    property->onValueChanged.add(&RecordChange);

    property->set(5.0f);

    EXPECT_EQ(g_changeCount, 0);
}

TEST(Property, ReflectedPropertyFiresOnValueChangedWhenValueDiffers) {
    newui::PropertyManager::instance().clear();
    ResetRecorder();
    newui::shapes::Circle circle;

    auto* property = newui::PropertyManager::instance().registerProperty<float>(&circle, "centerX");
    property->onValueChanged.add(&RecordChange);

    property->set(99.0f);

    EXPECT_EQ(g_changeCount, 1);
    EXPECT_EQ(g_lastChangedName, "centerX");
}

TEST(Property, ReflectedPropertyInterpolatesJustLikeAFieldBackedOne) {
    newui::PropertyManager::instance().clear();
    newui::shapes::Circle circle;

    auto* property = newui::PropertyManager::instance().registerProperty<float>(&circle, "radius");
    property->setupInterpolation(0.0f, 100.0f);

    property->interpolate(0.5f);

    EXPECT_FLOAT_EQ(circle.radius(), 50.0f);
}

TEST(Property, ReflectedPropertyThrowsForAPropertyNameCircleDoesNotHave) {
    newui::PropertyManager::instance().clear();
    newui::shapes::Circle circle;

    EXPECT_THROW(
        newui::PropertyManager::instance().registerProperty<float>(&circle, "notARealCircleProperty"),
        std::invalid_argument);
}

TEST(PropertyManager, RegisterPropertyCreatesAndStoresProperty) {
    newui::PropertyManager& manager = newui::PropertyManager::instance();
    manager.clear();
    int field = 1;
    int source = 0;

    auto* property = manager.registerProperty(&source, &field, "value");

    ASSERT_NE(property, nullptr);
    EXPECT_EQ(property->name(), "value");
    EXPECT_EQ(property->source(), &source);
    EXPECT_EQ(manager.getProperty(&source, "value"), property);
}

TEST(PropertyManager, GetPropertyReturnsNullptrWhenNotRegistered) {
    newui::PropertyManager& manager = newui::PropertyManager::instance();
    manager.clear();
    int source = 0;
    EXPECT_EQ(manager.getProperty(&source, "missing"), nullptr);
}

TEST(PropertyManager, SameNameOnDifferentSourcesAreDistinctProperties) {
    newui::PropertyManager& manager = newui::PropertyManager::instance();
    manager.clear();
    int sourceA = 0;
    int sourceB = 0;
    int fieldA = 1;
    int fieldB = 2;

    auto* propertyA = manager.registerProperty(&sourceA, &fieldA, "value");
    auto* propertyB = manager.registerProperty(&sourceB, &fieldB, "value");

    EXPECT_NE(propertyA, propertyB);
    EXPECT_EQ(manager.getProperty(&sourceA, "value"), propertyA);
    EXPECT_EQ(manager.getProperty(&sourceB, "value"), propertyB);
}

TEST(PropertyManager, RemovePropertyDeletesIt) {
    newui::PropertyManager& manager = newui::PropertyManager::instance();
    manager.clear();
    int field = 1;
    int source = 0;

    manager.registerProperty(&source, &field, "value");
    manager.removeProperty(&source, "value");

    EXPECT_EQ(manager.getProperty(&source, "value"), nullptr);
}

TEST(PropertyManager, RemovePropertyOnUnknownKeyIsANoop) {
    newui::PropertyManager& manager = newui::PropertyManager::instance();
    manager.clear();
    int source = 0;
    manager.removeProperty(&source, "missing");
    SUCCEED();
}

TEST(PropertyManager, RegisteringSameKeyTwiceReplacesThePreviousProperty) {
    newui::PropertyManager& manager = newui::PropertyManager::instance();
    manager.clear();
    int field = 1;
    int source = 0;

    auto* first = manager.registerProperty(&source, &field, "value");
    auto* second = manager.registerProperty(&source, &field, "value");

    EXPECT_NE(first, second);
    EXPECT_EQ(manager.getProperty(&source, "value"), second);
}

TEST(PropertyManager, ClearRemovesAllProperties) {
    newui::PropertyManager& manager = newui::PropertyManager::instance();
    manager.clear();
    int fieldA = 1;
    int fieldB = 2;

    manager.registerProperty(&fieldA, &fieldA, "a");
    manager.registerProperty(&fieldB, &fieldB, "b");

    manager.clear();

    EXPECT_EQ(manager.getProperty(&fieldA, "a"), nullptr);
    EXPECT_EQ(manager.getProperty(&fieldB, "b"), nullptr);
}

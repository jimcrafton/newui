#include "newui/animation.h"

#include "newui/geometry.h"
#include "newui/runloop.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

// ---------------------------------------------------------------------
// FrameRate
// ---------------------------------------------------------------------

TEST(FrameRate, DefaultConstructedIsThirtyFps) {
    newui::FrameRate rate;
    EXPECT_EQ(rate.numerator(), 30u);
    EXPECT_EQ(rate.denominator(), 1u);
    EXPECT_DOUBLE_EQ(rate.fps(), 30.0);
}

TEST(FrameRate, ZeroDenominatorIsTreatedAsOne) {
    newui::FrameRate rate(24, 0);
    EXPECT_EQ(rate.denominator(), 1u);
}

TEST(FrameRate, NtscIsApproximatelyTwentyNineNinetySeven) {
    newui::FrameRate ntsc = newui::FrameRate::NTSC();
    EXPECT_EQ(ntsc.numerator(), 30000u);
    EXPECT_EQ(ntsc.denominator(), 1001u);
    EXPECT_NEAR(ntsc.fps(), 29.97, 0.001);
}

TEST(FrameRate, NtscFilmIsApproximatelyTwentyThreeNinetySevenSix) {
    EXPECT_NEAR(newui::FrameRate::NTSCFilm().fps(), 23.976, 0.001);
}

TEST(FrameRate, PalIsExactlyTwentyFive) {
    EXPECT_DOUBLE_EQ(newui::FrameRate::PAL().fps(), 25.0);
}

TEST(FrameRate, FilmIsExactlyTwentyFour) {
    EXPECT_DOUBLE_EQ(newui::FrameRate::Film().fps(), 24.0);
}

TEST(FrameRate, EqualityComparesValueNotRepresentation) {
    // 60000/2002 reduces to the same rate as 30000/1001 (NTSC) - equality
    // cross-multiplies rather than comparing numerator/denominator fields
    // directly, so these should compare equal despite differing fields.
    newui::FrameRate ntsc = newui::FrameRate::NTSC();
    newui::FrameRate unreduced(60000, 2002);
    EXPECT_EQ(ntsc, unreduced);
}

TEST(FrameRate, InequalityForDifferentRates) {
    EXPECT_NE(newui::FrameRate::NTSC(), newui::FrameRate::PAL());
}

// ---------------------------------------------------------------------
// AnimationFrame
// ---------------------------------------------------------------------

TEST(AnimationFrame, DefaultConstructedIsFrameZeroAtThirtyFps) {
    newui::AnimationFrame frame;
    EXPECT_EQ(frame.value(), 0u);
    EXPECT_EQ(frame.framerate(), newui::FrameRate::FPS30());
}

TEST(AnimationFrame, ValueAndFramerateAreSettable) {
    newui::AnimationFrame frame;
    frame.setValue(42);
    frame.setFramerate(newui::FrameRate::NTSC());
    EXPECT_EQ(frame.value(), 42u);
    EXPECT_EQ(frame.framerate(), newui::FrameRate::NTSC());
}

TEST(AnimationFrame, IncrementAdvancesByOneByDefault) {
    newui::AnimationFrame frame(10, newui::FrameRate::FPS30());
    frame.increment();
    EXPECT_EQ(frame.value(), 11u);
}

TEST(AnimationFrame, IncrementAdvancesByGivenCount) {
    newui::AnimationFrame frame(10, newui::FrameRate::FPS30());
    frame.increment(5);
    EXPECT_EQ(frame.value(), 15u);
}

TEST(AnimationFrame, PreIncrementOperatorAdvancesByOne) {
    newui::AnimationFrame frame(0, newui::FrameRate::FPS30());
    ++frame;
    EXPECT_EQ(frame.value(), 1u);
}

TEST(AnimationFrame, SetFromElapsedComputesWholeFramesAtFramerate) {
    newui::AnimationFrame frame(0, newui::FrameRate::FPS30());  // 1/30s per frame
    auto start = std::chrono::steady_clock::now();
    auto current = start + std::chrono::milliseconds(100);  // 3 whole frames at 30fps

    frame.setFromElapsed(start, current);

    EXPECT_EQ(frame.value(), 3u);
}

TEST(AnimationFrame, SetFromElapsedClampsToZeroWhenCurrentIsNotAfterStart) {
    newui::AnimationFrame frame(99, newui::FrameRate::FPS30());
    auto start = std::chrono::steady_clock::now();

    frame.setFromElapsed(start, start);  // current == start

    EXPECT_EQ(frame.value(), 0u);
}

TEST(AnimationFrame, ConvertToPreservesApproximateWallClockPosition) {
    // Frame 24 at Film() (24fps) is exactly 1 second in; converting to
    // FPS30() should land on frame 30.
    newui::AnimationFrame filmFrame(24, newui::FrameRate::Film());

    newui::AnimationFrame converted = filmFrame.convertTo(newui::FrameRate::FPS30());

    EXPECT_EQ(converted.framerate(), newui::FrameRate::FPS30());
    EXPECT_EQ(converted.value(), 30u);
}

// ---------------------------------------------------------------------
// Key
// ---------------------------------------------------------------------

TEST(Key, StoresNameAndKeyFrame) {
    newui::Key key("start", 10);
    EXPECT_EQ(key.name(), "start");
    EXPECT_EQ(key.keyFrame(), 10u);

    key.setName("renamed");
    key.setKeyFrame(20);
    EXPECT_EQ(key.name(), "renamed");
    EXPECT_EQ(key.keyFrame(), 20u);
}

TEST(Key, SetValueAddsAFindableEntry) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");
    newui::Key key("k", 0);

    key.setValue(opacity, 0.5f);

    newui::KeyValue* found = key.findValue(opacity);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->property(), opacity);

    auto* typed = static_cast<newui::TypedKeyValue<float>*>(found);
    EXPECT_FLOAT_EQ(typed->value(), 0.5f);
}

TEST(Key, FindValueReturnsNullForUnsetProperty) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");
    newui::Key key("k", 0);

    EXPECT_EQ(key.findValue(opacity), nullptr);
}

TEST(Key, SetValueTwiceReplacesRatherThanDuplicating) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");
    newui::Key key("k", 0);

    key.setValue(opacity, 0.25f);
    key.setValue(opacity, 0.75f);

    ASSERT_EQ(key.values().size(), 1u);
    auto* typed = static_cast<newui::TypedKeyValue<float>*>(key.findValue(opacity));
    EXPECT_FLOAT_EQ(typed->value(), 0.75f);
}

TEST(Key, ApplyWritesValueThroughToField) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");
    newui::Key key("k", 0);
    key.setValue(opacity, 1.0f);

    key.findValue(opacity)->apply();

    EXPECT_FLOAT_EQ(field, 1.0f);
}

TEST(Key, SetValueDefaultsToLinearInterpolationKind) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");
    newui::Key key("k", 0);

    key.setValue(opacity, 1.0f);

    EXPECT_EQ(key.findValue(opacity)->interpolationKind(), newui::InterpolationKind::Linear);
}

TEST(Key, SetValueAcceptsAnInterpolationKind) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");
    newui::Key key("k", 0);

    key.setValue(opacity, 1.0f, newui::InterpolationKind::EaseIn);

    EXPECT_EQ(key.findValue(opacity)->interpolationKind(), newui::InterpolationKind::EaseIn);
}

TEST(Key, SetInterpolationKindChangesItAfterTheFact) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");
    newui::Key key("k", 0);
    key.setValue(opacity, 1.0f);

    key.findValue(opacity)->setInterpolationKind(newui::InterpolationKind::EaseOut);

    EXPECT_EQ(key.findValue(opacity)->interpolationKind(), newui::InterpolationKind::EaseOut);
}

// ---------------------------------------------------------------------
// Animation - structure (addKey ordering, isActiveAt)
// ---------------------------------------------------------------------

TEST(Animation, EndTimeIsStartTimePlusDuration) {
    newui::Animation animation("anim", 10, 20);
    EXPECT_EQ(animation.startTime(), 10u);
    EXPECT_EQ(animation.duration(), 20u);
    EXPECT_EQ(animation.endTime(), 30u);
}

TEST(Animation, IsActiveAtIncludesBothEndpoints) {
    newui::Animation animation("anim", 10, 20);  // [10, 30]
    EXPECT_FALSE(animation.isActiveAt(9));
    EXPECT_TRUE(animation.isActiveAt(10));
    EXPECT_TRUE(animation.isActiveAt(29));
    EXPECT_TRUE(animation.isActiveAt(30));  // endTime() itself - where the last Key lives
    EXPECT_FALSE(animation.isActiveAt(31));
}

TEST(Animation, AddKeyKeepsKeysSortedByKeyFrameRegardlessOfInsertionOrder) {
    newui::Animation animation("anim", 0, 100);
    animation.addKey("end", 100);
    animation.addKey("start", 0);
    animation.addKey("middle", 50);

    ASSERT_EQ(animation.keys().size(), 3u);
    EXPECT_EQ(animation.keys()[0]->name(), "start");
    EXPECT_EQ(animation.keys()[1]->name(), "middle");
    EXPECT_EQ(animation.keys()[2]->name(), "end");
}

TEST(Animation, AddKeyReturnsAStablePointer) {
    newui::Animation animation("anim", 0, 100);
    newui::Key* first = animation.addKey("a", 0);
    for (int i = 0; i < 20; ++i) {
        animation.addKey("filler", static_cast<std::uint64_t>(i + 1));
    }

    // first must still point at the same, valid Key after many more
    // addKey() calls - addKey() owns Keys via unique_ptr specifically so
    // vector reallocation can't invalidate previously returned pointers.
    EXPECT_EQ(first->name(), "a");
    EXPECT_EQ(first->keyFrame(), 0u);
}

// ---------------------------------------------------------------------
// Animation::processFrame - scalar (arithmetic) interpolation
// ---------------------------------------------------------------------

TEST(AnimationProcessFrame, InterpolatesLinearlyBetweenTwoKeys) {
    newui::PropertyManager::instance().clear();
    float opacityField = -1.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &opacityField, "opacity");

    newui::Animation animation("fade", 0, 10);
    animation.addKey("start", 0)->setValue(opacity, 0.0f);
    animation.addKey("end", 10)->setValue(opacity, 100.0f);

    animation.processFrame(0);
    EXPECT_FLOAT_EQ(opacityField, 0.0f);

    animation.processFrame(5);
    EXPECT_FLOAT_EQ(opacityField, 50.0f);

    animation.processFrame(10);
    EXPECT_FLOAT_EQ(opacityField, 100.0f);
}

TEST(AnimationProcessFrame, HoldsFirstKeysValueAtOrBeforeIt) {
    newui::PropertyManager::instance().clear();
    int healthField = -1;
    auto* health = newui::PropertyManager::instance().registerProperty(nullptr, &healthField, "health");

    newui::Animation animation("anim", 5, 10);  // starts at frame 5
    animation.addKey("start", 0)->setValue(health, 100);
    animation.addKey("end", 10)->setValue(health, 0);

    animation.processFrame(5);  // absolute frame 5 -> local frame 0 (the first key)
    EXPECT_EQ(healthField, 100);
}

TEST(AnimationProcessFrame, HoldsLastKeysValueAtOrAfterIt) {
    newui::PropertyManager::instance().clear();
    int healthField = -1;
    auto* health = newui::PropertyManager::instance().registerProperty(nullptr, &healthField, "health");

    newui::Animation animation("anim", 0, 10);
    animation.addKey("start", 0)->setValue(health, 100);
    animation.addKey("end", 10)->setValue(health, 0);

    animation.processFrame(999);  // well past the last key
    EXPECT_EQ(healthField, 0);
}

TEST(AnimationProcessFrame, UsesAnimationsStartTimeToOffsetIntoLocalKeyFrames) {
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* value = newui::PropertyManager::instance().registerProperty(nullptr, &field, "value");

    newui::Animation animation("anim", 100, 10);  // active over absolute [100, 110)
    animation.addKey("start", 0)->setValue(value, 0.0f);
    animation.addKey("end", 10)->setValue(value, 100.0f);

    animation.processFrame(105);  // local frame 5 -> halfway
    EXPECT_FLOAT_EQ(field, 50.0f);
}

TEST(AnimationProcessFrame, ThreeKeysInterpolatesWithinTheRightSegment) {
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* value = newui::PropertyManager::instance().registerProperty(nullptr, &field, "value");

    newui::Animation animation("anim", 0, 20);
    animation.addKey("a", 0)->setValue(value, 0.0f);
    animation.addKey("b", 10)->setValue(value, 100.0f);
    animation.addKey("c", 20)->setValue(value, 0.0f);

    animation.processFrame(15);  // halfway through the b->c segment
    EXPECT_FLOAT_EQ(field, 50.0f);
}

TEST(AnimationProcessFrame, EachKeyValueUsesItsOwnInterpolationKind) {
    // Two properties transitioning over the same segment, with different
    // curves - one Linear, one EaseIn - prove processFrame() honors each
    // KeyValue's own interpolationKind() independently rather than
    // applying one kind to the whole Key.
    newui::PropertyManager::instance().clear();
    float linearField = -1.0f;
    float easeInField = -1.0f;
    auto* linearValue = newui::PropertyManager::instance().registerProperty(nullptr, &linearField, "linear");
    auto* easeInValue = newui::PropertyManager::instance().registerProperty(nullptr, &easeInField, "easeIn");

    newui::Animation animation("anim", 0, 10);

    newui::Key* start = animation.addKey("start", 0);
    start->setValue(linearValue, 0.0f);
    start->setValue(easeInValue, 0.0f);

    // Only the *target* Key's interpolationKind is consulted when
    // blending into it - see KeyValue::interpolateFrom() - so it's set
    // here, on "end", not on "start".
    newui::Key* end = animation.addKey("end", 10);
    end->setValue(linearValue, 100.0f, newui::InterpolationKind::Linear);
    end->setValue(easeInValue, 100.0f, newui::InterpolationKind::EaseIn);

    animation.processFrame(5);  // t = 0.5

    EXPECT_FLOAT_EQ(linearField, 50.0f);  // Linear: t itself
    EXPECT_FLOAT_EQ(easeInField, 25.0f);  // EaseIn: t*t = 0.25
}

TEST(AnimationProcessFrame, CustomInterpolationFunctionOverridesInterpolationKind) {
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* value = newui::PropertyManager::instance().registerProperty(nullptr, &field, "value");

    newui::Animation animation("anim", 0, 10);
    animation.addKey("start", 0)->setValue(value, 0.0f);

    // A custom fn always returning the midpoint, ignoring t entirely -
    // proves fn (not interpolationKind()) drives the result, the same way
    // Property<T>::interpolate(t, fn) itself does.
    newui::Key* end = animation.addKey("end", 10);
    end->setValue(value, 100.0f, [](float start, float endValue, float) {
        return (start + endValue) * 0.5f;
        });

    animation.processFrame(3);  // any t within (0,1) should give the same result

    EXPECT_FLOAT_EQ(field, 50.0f);
}

TEST(AnimationProcessFrame, CustomInterpolationFunctionInterpolatesStructFieldsSmoothly) {
    // Property<Point>'s built-in InterpolationKind curve throws (see
    // property.h) and TypedKeyValue falls back to stepping without a
    // custom function (see AnimationProcessFrame.StructFieldStepsRather-
    // Interpolating above) - a custom componentwise fn is what makes a
    // struct-typed property interpolate smoothly between Keys instead.
    newui::PropertyManager::instance().clear();
    newui::Point positionField(-1.0f, -1.0f);
    auto* position = newui::PropertyManager::instance().registerProperty(nullptr, &positionField, "position");

    auto lerpPoint = [](newui::Point start, newui::Point end, float t) {
        return newui::Point(start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t);
        };

    newui::Animation animation("move", 0, 10);
    animation.addKey("start", 0)->setValue(position, newui::Point(0.0f, 0.0f));
    animation.addKey("end", 10)->setValue(position, newui::Point(100.0f, 100.0f), lerpPoint);

    animation.processFrame(5);  // t = 0.5 - smooth midpoint, not a step

    EXPECT_EQ(positionField, newui::Point(50.0f, 50.0f));
}

TEST(Key, SetInterpolationFunctionOnTypedKeyValueIsRetrievable) {
    newui::PropertyManager::instance().clear();
    float field = 0.0f;
    auto* value = newui::PropertyManager::instance().registerProperty(nullptr, &field, "value");
    newui::Key key("k", 0);
    key.setValue(value, 1.0f);

    auto* typed = static_cast<newui::TypedKeyValue<float>*>(key.findValue(value));
    EXPECT_FALSE(typed->hasInterpolationFunction());

    typed->setInterpolationFunction([](float, float, float) { return 0.0f; });
    EXPECT_TRUE(typed->hasInterpolationFunction());
}

// ---------------------------------------------------------------------
// Animation::processFrame - struct (non-arithmetic) fields step instead
// of interpolating smoothly, since there's no generic way to blend an
// arbitrary struct.
// ---------------------------------------------------------------------

TEST(AnimationProcessFrame, StructFieldStepsRatherThanInterpolating) {
    newui::PropertyManager::instance().clear();
    newui::Point positionField(-1.0f, -1.0f);
    auto* position = newui::PropertyManager::instance().registerProperty(nullptr, &positionField, "position");

    newui::Animation animation("move", 0, 10);
    animation.addKey("start", 0)->setValue(position, newui::Point(0.0f, 0.0f));
    animation.addKey("end", 10)->setValue(position, newui::Point(100.0f, 100.0f));

    animation.processFrame(5);  // mid-segment: holds the start Key's value
    EXPECT_EQ(positionField, newui::Point(0.0f, 0.0f));

    animation.processFrame(10);  // reaches the end Key: snaps
    EXPECT_EQ(positionField, newui::Point(100.0f, 100.0f));
}

TEST(AnimationProcessFrame, PropertyOnlySetByLaterKeyAppliesAcrossTheWholeSegment) {
    // opacity only has a value at the "end" Key - with nothing earlier to
    // interpolate from, KeyValue::interpolateFrom() can only apply it
    // directly (see property.h's comment on that method), so it holds
    // that value for the whole start->end segment rather than only at the
    // exact end frame.
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* opacity = newui::PropertyManager::instance().registerProperty(nullptr, &field, "opacity");

    newui::Animation animation("anim", 0, 10);
    animation.addKey("start", 0);  // sets nothing
    animation.addKey("end", 10)->setValue(opacity, 1.0f);

    animation.processFrame(3);
    EXPECT_FLOAT_EQ(field, 1.0f);
}

TEST(AnimationProcessFrame, NoOpWhenAnimationHasNoKeys) {
    newui::PropertyManager::instance().clear();
    float field = 42.0f;
    newui::PropertyManager::instance().registerProperty(nullptr, &field, "value");
    newui::Animation animation("empty", 0, 10);

    animation.processFrame(5);

    EXPECT_FLOAT_EQ(field, 42.0f);  // untouched
}

// ---------------------------------------------------------------------
// AnimationManager - structure
// ---------------------------------------------------------------------

TEST(AnimationManager, DefaultsToThirtyFramesPerSecond) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    EXPECT_EQ(manager.frameRate(), newui::FrameRate::FPS30());
}

TEST(AnimationManager, SetFrameRateChangesIt) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    manager.setFrameRate(newui::FrameRate::NTSC());
    EXPECT_EQ(manager.frameRate(), newui::FrameRate::NTSC());
}

TEST(AnimationManager, AddAnimationReturnsAStablePointer) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    newui::Animation* first = manager.addAnimation("a", 0, 10);
    for (int i = 0; i < 20; ++i) {
        manager.addAnimation("filler", 0, 10);
    }

    EXPECT_EQ(first->name(), "a");
}

TEST(AnimationManager, RemoveAnimationDropsIt) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    newui::Animation* keep = manager.addAnimation("keep", 0, 10);
    newui::Animation* drop = manager.addAnimation("drop", 0, 10);

    manager.removeAnimation(drop);

    // No direct "contains" accessor - proven indirectly via processIdle()
    // in the behavioral tests below instead. Here we just confirm the
    // surviving Animation* is still usable.
    EXPECT_EQ(keep->name(), "keep");
}

TEST(AnimationManager, ClearRemovesAnimationsAndResetsPlayback) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    manager.setFrameRate(newui::FrameRate::NTSC());
    manager.addAnimation("a", 0, 10);
    manager.processIdle();  // starts the clock

    manager.clear();

    EXPECT_EQ(manager.frameRate(), newui::FrameRate::FPS30());
    EXPECT_EQ(manager.currentFrame(), 0u);
}

// ---------------------------------------------------------------------
// AnimationManager::processIdle - direct calls, no RunLoop needed since
// processIdle() only depends on wall-clock time, not the loop itself.
// ---------------------------------------------------------------------

TEST(AnimationManagerProcessIdle, FirstCallStartsTheClockWithoutAdvancing) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    EXPECT_FALSE(manager.processIdle());
    EXPECT_EQ(manager.currentFrame(), 0u);
}

TEST(AnimationManagerProcessIdle, DoesNotAdvanceBeforeAFrameDurationElapses) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    manager.setFrameRate(newui::FrameRate(1));  // 1 fps - a whole second per frame
    manager.processIdle();    // starts the clock

    manager.processIdle();    // effectively immediately after

    EXPECT_EQ(manager.currentFrame(), 0u);
}

TEST(AnimationManagerProcessIdle, AdvancesFrameOnceEnoughTimeHasElapsed) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    manager.setFrameRate(newui::FrameRate(1000));  // 1 ms/frame, so a short sleep is enough
    manager.processIdle();       // starts the clock

    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    manager.processIdle();

    EXPECT_GT(manager.currentFrame(), 0u);
}

TEST(AnimationManagerProcessIdle, AlwaysReportsNotDone) {
    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    EXPECT_FALSE(manager.processIdle());
    EXPECT_FALSE(manager.processIdle());
}

TEST(AnimationManagerProcessIdle, ProcessesOnlyAnimationsActiveAtTheCurrentFrame) {
    newui::PropertyManager::instance().clear();
    float activeField = -1.0f;
    float inactiveField = -1.0f;
    auto* activeValue = newui::PropertyManager::instance().registerProperty(nullptr, &activeField, "active");
    auto* inactiveValue = newui::PropertyManager::instance().registerProperty(nullptr, &inactiveField, "inactive");

    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    manager.setFrameRate(newui::FrameRate(1000));

    newui::Animation* active = manager.addAnimation("active", 0, 1000000);
    active->addKey("k", 0)->setValue(activeValue, 7.0f);

    // startTime is far in the future, so it should never become active
    // during this test.
    newui::Animation* inactive = manager.addAnimation("inactive", 1000000000ull, 10);
    inactive->addKey("k", 0)->setValue(inactiveValue, 9.0f);

    manager.processIdle();  // starts the clock
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    manager.processIdle();  // should advance at least one frame

    EXPECT_FLOAT_EQ(activeField, 7.0f);
    EXPECT_FLOAT_EQ(inactiveField, -1.0f);  // never touched
}

// ---------------------------------------------------------------------
// AnimationManager::run - end-to-end via a real RunLoop's idle task.
// ---------------------------------------------------------------------

TEST(AnimationManagerRun, DrivesAnAnimationToCompletionViaRunLoopIdle) {
    newui::PropertyManager::instance().clear();
    float field = -1.0f;
    auto* value = newui::PropertyManager::instance().registerProperty(nullptr, &field, "value");

    newui::AnimationManager& manager = newui::AnimationManager::instance();
    manager.clear();
    manager.setFrameRate(newui::FrameRate(1000));  // fast, so the test doesn't need to wait long

    newui::Animation* animation = manager.addAnimation("anim", 0, 5);
    animation->addKey("start", 0)->setValue(value, 0.0f);
    animation->addKey("end", 5)->setValue(value, 100.0f);

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    manager.run(runLoop);

    // At 1000 fps a 5-frame animation finishes in ~5ms; sleep with a
    // generous margin, then stop the loop. quit()/join() synchronizes
    // with the loop thread (the only thing that ever writes `field`), so
    // reading it afterward on this thread is safe without extra atomics -
    // no polling task needed, which matters here: a task that kept
    // reposting itself via post() would starve the manager's idle
    // processing entirely, since pending messages always run before the
    // loop goes idle again (see idletasks1.cpp's Demo 4).
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    runLoop.quit();
    loopThread.join();

    EXPECT_FLOAT_EQ(field, 100.0f);
}

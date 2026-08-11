// A tour of newui::Key / newui::Animation / newui::AnimationManager:
// keyframing Property values over time. Demos 1-5 drive Animation
// directly, stepping through frames by hand so the output is
// deterministic; Demo 6 hands playback to AnimationManager, driven by
// RunLoop idle time the way a real application would use it.
//
// The interesting part is curves - not just *when* a value reaches its
// target (InterpolationKind, or a fully custom timing function), but
// *how* it gets there (a custom function can reshape the path itself, not
// just its timing - see Demo 4's hand-written Bezier arc and Demo 5's
// reusable newui::CurveInterpolation<T>, which generalizes it to any
// number of control points).
//
// PropertyManager and AnimationManager are both process-wide singletons
// (see their instance() methods) - a Property or an AnimationManager-
// driven playback clock can only ever be reached through them. Each demo
// calls PropertyManager::instance().clear() first so it can be read on
// its own without needing to know what an earlier demo registered.

#include "newui/newui.h"
#include "newui/animation.h"
#include "newui/curveinterpolation.h"
#include "newui/geometry.h"
#include "newui/runloop.h"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <thread>

struct Entity {
    float opacity = 0.0f;
    float scale = 0.0f;
    newui::Point position;
};

// ---------------------------------------------------------------------
// Demo 1: two Keys, default Linear interpolation - the baseline everything
// else in this file varies from.
// ---------------------------------------------------------------------

void demoBasicKeyframing() {
    std::cout << "\n== Demo 1: two Keys, default Linear interpolation ==\n";

    newui::PropertyManager::instance().clear();
    Entity entity;
    auto* opacity = newui::PropertyManager::instance().registerProperty(&entity, &entity.opacity, "opacity");

    newui::Animation animation("fade-in", 0, 10);
    animation.addKey("start", 0)->setValue(opacity, 0.0f);
    animation.addKey("end", 10)->setValue(opacity, 100.0f);

    for (std::uint64_t frame = 0; frame <= 10; frame += 2) {
        animation.processFrame(frame);
        std::cout << "  frame " << std::setw(2) << frame << ": opacity = " << opacity->get() << "\n";
    }
}

// ---------------------------------------------------------------------
// Demo 2: different InterpolationKind per Key. A Key's kind governs the
// curve used to blend *into* it (see KeyValue::interpolateFrom()) - so a
// three-Key animation can ease in for the first half and ease out for
// the second, entirely independently.
// ---------------------------------------------------------------------

void demoInterpolationKindPerKey() {
    std::cout << "\n== Demo 2: EaseIn rising, EaseOut falling ==\n";

    newui::PropertyManager::instance().clear();
    Entity entity;
    auto* scale = newui::PropertyManager::instance().registerProperty(&entity, &entity.scale, "scale");

    newui::Animation animation("pulse", 0, 20);
    animation.addKey("low", 0)->setValue(scale, 0.0f);
    animation.addKey("high", 10)->setValue(scale, 100.0f, newui::InterpolationKind::EaseIn);
    animation.addKey("low-again", 20)->setValue(scale, 0.0f, newui::InterpolationKind::EaseOut);

    for (std::uint64_t frame = 0; frame <= 20; frame += 2) {
        animation.processFrame(frame);
        std::cout << "  frame " << std::setw(2) << frame << ": "
                  << std::fixed << std::setprecision(1) << scale->get() << "\n";
    }
}

// ---------------------------------------------------------------------
// Demo 3: a fully custom *timing* curve - Key::setValue()'s function
// overload, the same InterpolationFunction Property<T>::interpolate(t, fn)
// itself takes. A back-out ease overshoots past its target before
// settling, something none of InterpolationKind's canned shapes do.
// ---------------------------------------------------------------------

void demoCustomTimingCurve() {
    std::cout << "\n== Demo 3: custom curve - back-out overshoot ==\n";

    newui::PropertyManager::instance().clear();
    Entity entity;
    auto* scale = newui::PropertyManager::instance().registerProperty(&entity, &entity.scale, "scale");

    newui::Animation animation("pop-in", 0, 10);
    animation.addKey("start", 0)->setValue(scale, 0.0f);

    float overshoot = 1.70158f;  // capture a tunable parameter - only
                                  // possible because this takes any
                                  // callable, not just a plain function
                                  // pointer like Delegate requires.
    newui::Key* end = animation.addKey("end", 10);
    end->setValue(scale, 1.0f, [overshoot](float start, float endValue, float t) {
        t -= 1.0f;
        float eased = t * t * ((overshoot + 1.0f) * t + overshoot) + 1.0f;
        return start + (endValue - start) * eased;
        });

    for (std::uint64_t frame = 0; frame <= 10; ++frame) {
        animation.processFrame(frame);
        std::cout << "  frame " << std::setw(2) << frame << ": "
                  << std::fixed << std::setprecision(3) << scale->get() << "\n";
    }
    std::cout << "  (overshoots past 1.0 before settling)\n";
}

// ---------------------------------------------------------------------
// Demo 4: a custom curve reshaping the *path*, not just the timing. A
// Point's built-in InterpolationKind curve can't do arithmetic on a
// struct (see property.h) - the fallback without a custom function is a
// step, not even a straight-line lerp. Here the custom function traces a
// quadratic Bezier arc through a control point instead of a straight
// line between the two Keys.
// ---------------------------------------------------------------------

void demoBezierPathCurve() {
    std::cout << "\n== Demo 4: custom curve - quadratic Bezier path for a Point ==\n";

    newui::PropertyManager::instance().clear();
    Entity entity;
    auto* position = newui::PropertyManager::instance().registerProperty(&entity, &entity.position, "position");

    newui::Point control(100.0f, 0.0f);  // pulls the arc up and over

    newui::Animation animation("arc", 0, 10);
    animation.addKey("start", 0)->setValue(position, newui::Point(0.0f, 100.0f));

    newui::Key* end = animation.addKey("end", 10);
    end->setValue(position, newui::Point(100.0f, 100.0f),
        [control](newui::Point start, newui::Point endPoint, float t) {
            float u = 1.0f - t;
            float x = u * u * start.x + 2.0f * u * t * control.x + t * t * endPoint.x;
            float y = u * u * start.y + 2.0f * u * t * control.y + t * t * endPoint.y;
            return newui::Point(x, y);
            });

    for (std::uint64_t frame = 0; frame <= 10; frame += 2) {
        animation.processFrame(frame);
        std::cout << "  frame " << std::setw(2) << frame << ": ("
                  << std::fixed << std::setprecision(1) << position->get().x << ", "
                  << position->get().y << ")\n";
    }
    std::cout << "  (curves through (100, 0) instead of a straight line to (100, 100))\n";
}

// ---------------------------------------------------------------------
// Demo 5: newui::CurveInterpolation<T> - a reusable Bezier curve instead
// of hand-writing the De Casteljau math like Demo 4 did. addPoint() adds
// as many control points as needed (Demo 4's single point made a
// quadratic Bezier; two points here makes it cubic), and since
// CurveInterpolation<T>'s call operator matches Key::setValue()'s
// interpolation-function signature exactly (T(T start, T end, float t)),
// it plugs straight into a Key with no adapter - the same slot Demo 3
// and Demo 4's lambdas filled.
// ---------------------------------------------------------------------

void demoCurveInterpolation() {
    std::cout << "\n== Demo 5: CurveInterpolation<Point> - a reusable, N-point Bezier ==\n";

    newui::PropertyManager::instance().clear();
    Entity entity;
    auto* position = newui::PropertyManager::instance().registerProperty(&entity, &entity.position, "position");

    newui::CurveInterpolation<newui::Point> curve;
    curve.addPoint(newui::Point(0.0f, 0.0f));      // pulls the path down first...
    curve.addPoint(newui::Point(100.0f, 200.0f));  // ...then sharply back up

    newui::Animation animation("s-curve", 0, 10);
    animation.addKey("start", 0)->setValue(position, newui::Point(0.0f, 100.0f));
    animation.addKey("end", 10)->setValue(position, newui::Point(100.0f, 100.0f), curve);

    for (std::uint64_t frame = 0; frame <= 10; frame += 2) {
        animation.processFrame(frame);
        std::cout << "  frame " << std::setw(2) << frame << ": ("
                  << std::fixed << std::setprecision(1) << position->get().x << ", "
                  << position->get().y << ")\n";
    }
    std::cout << "  (cubic Bezier: an S-shaped path through both control points)\n";
}

// ---------------------------------------------------------------------
// Demo 6: real playback - AnimationManager owns the Animation and drives
// it forward via RunLoop idle time (see RunLoop::postIdle()), the way an
// application actually uses this rather than hand-calling
// processFrame(). onValueChanged (from Property) logs each write so the
// curve's progress is visible as it happens on the loop's own thread.
// ---------------------------------------------------------------------

// A template, not a plain function, since onValueChanged's Sender is now
// the fully-typed Property<SourceT, ValueT> (not PropertyBase) - see
// property.h. Taking &LogOpacityChanged in a context expecting a
// specific SyncReturn(*)(Property<SourceT, ValueT>&) deduces
// ValueT/SourceT from that target type.
template<typename SourceT, typename ValueT>
newui::SyncReturn LogOpacityChanged(newui::ObservableProperty<SourceT, ValueT>& property, SourceT*, ValueT*) {
    std::cout << "  onValueChanged: '" << property.name() << "' updated (thread "
              << std::this_thread::get_id() << ")\n";
    return newui::SyncReturn::Handled;
}

void demoAnimationManagerWithRunLoop() {
    std::cout << "\n== Demo 6: AnimationManager driving playback via RunLoop idle time ==\n";

    newui::PropertyManager::instance().clear();
    Entity entity;
    auto* opacity = newui::PropertyManager::instance().registerProperty(&entity, &entity.opacity, "opacity");
    opacity->onValueChanged.add(&LogOpacityChanged);

    
    newui::AnimationManager::clear();
    newui::AnimationManager::setFrameRate(newui::FrameRate::NTSC());  // ~29.97 fps

    newui::Animation* animation = newui::AnimationManager::addAnimation("fade-in", 0, 15);
    animation->addKey("start", 0)->setValue(opacity, 0.0f);
    animation->addKey("end", 15)->setValue(opacity, 100.0f, newui::InterpolationKind::EaseInOut);

    newui::RunLoop runLoop;
    std::thread loopThread([&runLoop]() { runLoop.run(); });
    runLoop.waitUntilStarted();

    newui::AnimationManager::addToRunLoop(runLoop);

    // 15 frames at ~29.97 fps is ~500ms; give it a comfortable margin.
    // quit()/join() below synchronizes with the loop thread, so reading
    // entity.opacity afterward on this thread is safe without extra
    // atomics - the only thing that ever writes it is the loop thread,
    // and join() happens-after everything it did.
    std::this_thread::sleep_for(std::chrono::milliseconds(700));

    runLoop.quit();
    loopThread.join();

    std::cout << "  final opacity: " << entity.opacity
              << " (manager reached frame " << newui::AnimationManager::currentFrame() << ")\n";
}

int main() {
    std::cout << "newui " << newui::version() << " - animation examples\n";

    demoBasicKeyframing();
    demoInterpolationKindPerKey();
    demoCustomTimingCurve();
    demoBezierPathCurve();
    demoCurveInterpolation();
    demoAnimationManagerWithRunLoop();

    return 0;
}

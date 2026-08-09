// A tour of newui::Property / newui::PropertyManager: binding fields on
// plain structs (no View/Frame involved - Property works on any POD field),
// then walking through the different ways to interpolate them - the
// built-in InterpolationKind curves, a custom lambda, a keyframe vector,
// and componentwise interpolation of struct fields like Point and Color.
//
// PropertyManager is a process-wide singleton (see PropertyManager::
// instance()) - a Property can only ever be created through it, never
// constructed directly, since it's the sole owner of every Property's
// lifetime. Each demo below calls instance().clear() first so it can be
// read on its own without needing to know what an earlier demo registered.

#include "newui/newui.h"
#include "newui/property.h"
#include "newui/geometry.h"
#include "newui/color.h"

#include <cmath>
#include <iomanip>
#include <iostream>

// File-scope so demoTrigInterpolation()'s lambdas can use it without
// capturing it - a constexpr declared *inside* a function still has to be
// captured by a lambda that uses it (at least under MSVC), while one at
// namespace scope never does.
constexpr float kPi = 3.14159265358979323846f;

// A stand-in "model" type with a mix of scalar and struct fields - nothing
// Property-specific about it, which is the point: Property binds to
// whatever field you hand it a pointer to.
struct Entity {
    std::string name;
    int health = 100;
    float opacity = 1.0f;
    newui::Point position;
    newui::Color tint = newui::Color::fromName("white");
};

// ---------------------------------------------------------------------
// Demo 1: PropertyManager as the only way to create/own a Property -
// register a scalar field, read/write it through the Property instead of
// the field directly, and observe onValueChanged.
// ---------------------------------------------------------------------

// A template, not a plain function, since onValueChanged's Sender is now
// the fully-typed Property<SourceT, ValueT> (not PropertyBase), and it
// also passes the source and the (already-updated) field directly as
// extra args - see property.h's ValueChangedDelegate. Taking
// &LogValueChanged in a context expecting a specific
// SyncReturn(*)(Property<SourceT, ValueT>&, SourceT*, ValueT*) deduces
// ValueT/SourceT from that target type.
template<typename SourceT, typename ValueT>
newui::SyncReturn LogValueChanged(newui::Property<SourceT, ValueT>& property, SourceT*, ValueT*) {
    std::cout << "  onValueChanged: '" << property.name() << "' changed\n";
    return newui::SyncReturn::Handled;
}

void demoPropertyManagerBasics() {
    std::cout << "\n== Demo 1: PropertyManager - register, get/set, onValueChanged ==\n";

    newui::PropertyManager& properties = newui::PropertyManager::instance();
    properties.clear();
    Entity hero;
    hero.name = "Hero";

    auto* health = properties.registerProperty(&hero, &hero.health, "health");
    health->onValueChanged.add(&LogValueChanged);

    std::cout << "  initial health: " << health->get() << "\n";
    health->set(80);   // fires onValueChanged - value actually changed
    health->set(80);   // no-op - same value, no notification
    std::cout << "  hero.health field directly: " << hero.health << "\n";

    // Properties are looked up later by (source pointer, name) - no need to
    // keep the Property* returned by registerProperty() around.
    auto* sameProperty = properties.getProperty(&hero, "health");
    std::cout << "  getProperty() returns the same instance: "
              << std::boolalpha << (sameProperty == health) << "\n";

    properties.removeProperty(&hero, "health");
    std::cout << "  after removeProperty(), getProperty() returns null: "
              << std::boolalpha << (properties.getProperty(&hero, "health") == nullptr) << "\n";
}

// ---------------------------------------------------------------------
// Demo 2: the built-in InterpolationKind curves on a scalar Property.
// setupInterpolation() establishes the [start,end] range once; interpolate()
// can then be called repeatedly (e.g. once per animation frame) with a t in
// [0,1] - here we just sample a few t values to compare curves.
// ---------------------------------------------------------------------

void printCurve(const char* label, newui::Property<void, float>& property, newui::InterpolationKind kind) {
    property.setupInterpolation(0.0f, 100.0f, kind);

    std::cout << "  " << std::left << std::setw(10) << label << ": ";
    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        property.interpolate(t);
        std::cout << std::right << std::setw(7) << std::fixed << std::setprecision(2) << property.get();
    }
    std::cout << "\n";
}

void demoBuiltInInterpolationKinds() {
    std::cout << "\n== Demo 2: built-in InterpolationKind curves (t = 0, 0.25, 0.5, 0.75, 1) ==\n";

    newui::PropertyManager::instance().clear();
    float opacity = 0.0f;
    auto* opacityProperty = newui::PropertyManager::instance().registerProperty(nullptr, &opacity, "opacity");

    printCurve("Linear", *opacityProperty, newui::InterpolationKind::Linear);
    printCurve("EaseIn", *opacityProperty, newui::InterpolationKind::EaseIn);
    printCurve("EaseOut", *opacityProperty, newui::InterpolationKind::EaseOut);
    printCurve("EaseInOut", *opacityProperty, newui::InterpolationKind::EaseInOut);
}

// ---------------------------------------------------------------------
// Demo 3: interpolate(t, fn) - a custom shaping function for when none of
// InterpolationKind's built-in curves fit, e.g. a back-out "overshoot" ease
// that goes past end before settling. Delegate can't hold this (it only
// takes plain function pointers, for its lock-free multicast list); this
// overload takes any callable, including a capturing lambda, since it's a
// single direct call rather than a broadcast.
// ---------------------------------------------------------------------

void demoCustomFunctionInterpolation() {
    std::cout << "\n== Demo 3: interpolate(t, fn) - custom overshoot easing ==\n";

    newui::PropertyManager::instance().clear();
    float scale = 0.0f;
    auto* scaleProperty = newui::PropertyManager::instance().registerProperty(nullptr, &scale, "scale");
    scaleProperty->setupInterpolation(0.0f, 1.0f);

    float overshoot = 1.70158f;  // capture a tunable parameter - only possible
                                  // because this overload isn't limited to
                                  // plain function pointers like Delegate is.
    auto backOut = [overshoot](float start, float end, float t) {
        t -= 1.0f;
        float eased = t * t * ((overshoot + 1.0f) * t + overshoot) + 1.0f;
        return start + (end - start) * eased;
        };

    std::cout << "  scale: ";
    for (float t = 0.0f; t <= 1.0f; t += 0.2f) {
        scaleProperty->interpolate(t, backOut);
        std::cout << std::setw(7) << std::fixed << std::setprecision(3) << scaleProperty->get();
    }
    std::cout << "  (overshoots past 1.0 before settling)\n";
}

// ---------------------------------------------------------------------
// Demo 4: interpolate(t, keyframes) - a piecewise-linear curve sampled from
// a std::vector<float> instead of a single [start,end] span. Useful for
// hand-authored curves (a pulse, a wobble) that don't reduce to one of the
// InterpolationKind shapes. Only available for scalar Property<T> - there's
// no generic way to build a struct out of a bare float.
// ---------------------------------------------------------------------

void demoKeyframeInterpolation() {
    std::cout << "\n== Demo 4: interpolate(t, keyframes) - a hand-authored pulse curve ==\n";

    newui::PropertyManager::instance().clear();
    float pulse = 0.0f;
    auto* pulseProperty = newui::PropertyManager::instance().registerProperty(nullptr, &pulse, "pulse");
    std::vector<float> keyframes{0.0f, 1.0f, 0.2f, 1.0f, 0.0f};

    std::cout << "  pulse: ";
    for (float t = 0.0f; t <= 1.0f; t += 0.125f) {
        pulseProperty->interpolate(t, keyframes);
        std::cout << std::setw(6) << std::fixed << std::setprecision(2) << pulseProperty->get();
    }
    std::cout << "\n";
}

// ---------------------------------------------------------------------
// Demo 5: a struct field (newui::Point). Property<Point> works fine for
// get()/set() and interpolate(t, fn) - but the built-in InterpolationKind
// curve and the keyframe overload only do arithmetic on scalar T, so they
// throw / don't compile for a struct like Point. interpolate(t, fn) is the
// escape hatch: the lambda does the componentwise blending itself.
// ---------------------------------------------------------------------

void demoPointProperty() {
    std::cout << "\n== Demo 5: Property<Point> - componentwise interpolation via fn ==\n";

    newui::PropertyManager& properties = newui::PropertyManager::instance();
    properties.clear();
    Entity hero;
    auto* position = properties.registerProperty(&hero, &hero.position, "position");
    position->setupInterpolation(newui::Point(0.0f, 0.0f), newui::Point(100.0f, 50.0f));

    auto lerpPoint = [](newui::Point start, newui::Point end, float t) {
        return newui::Point(start.x + (end.x - start.x) * t, start.y + (end.y - start.y) * t);
        };

    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        position->interpolate(t, lerpPoint);
        std::cout << "  t=" << std::fixed << std::setprecision(2) << t
                  << " -> (" << position->get().x << ", " << position->get().y << ")\n";
    }

    // The built-in InterpolationKind curve can't do arithmetic on a Point -
    // Property<Point>::interpolate(t) compiles (it must, to satisfy
    // PropertyBase's pure virtual) but throws at runtime instead.
    try {
        position->interpolate(0.5f);
    } catch (const std::logic_error& ex) {
        std::cout << "  interpolate(t) without fn throws for struct fields: " << ex.what() << "\n";
    }
}

// ---------------------------------------------------------------------
// Demo 6: another struct field (newui::Color) - same pattern as Point,
// blending r/g/b/a componentwise through a custom fn.
// ---------------------------------------------------------------------

void demoColorProperty() {
    std::cout << "\n== Demo 6: Property<Color> - componentwise interpolation via fn ==\n";

    newui::PropertyManager& properties = newui::PropertyManager::instance();
    properties.clear();
    Entity hero;
    auto* tint = properties.registerProperty(&hero, &hero.tint, "tint");
    tint->setupInterpolation(newui::Color::fromName("red"), newui::Color::fromName("blue"));

    auto lerpColor = [](newui::Color start, newui::Color end, float t) {
        return newui::Color(
            start.r + (end.r - start.r) * t,
            start.g + (end.g - start.g) * t,
            start.b + (end.b - start.b) * t,
            start.a + (end.a - start.a) * t);
        };

    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        tint->interpolate(t, lerpColor);
        const newui::Color& c = tint->get();
        std::cout << "  t=" << std::fixed << std::setprecision(2) << t
                  << " -> r=" << std::setprecision(2) << c.r
                  << " g=" << c.g << " b=" << c.b << " a=" << c.a << "\n";
    }
}

// ---------------------------------------------------------------------
// Demo 7: PropertyManager keys by (name, source pointer), so the same
// field name can be registered independently on multiple source instances.
// ---------------------------------------------------------------------

void demoMultipleSourcesSameName() {
    std::cout << "\n== Demo 7: same property name on different sources ==\n";

    newui::PropertyManager& properties = newui::PropertyManager::instance();
    properties.clear();
    Entity hero;
    Entity villain;
    hero.name = "Hero";
    villain.name = "Villain";

    auto* heroHealth = properties.registerProperty(&hero, &hero.health, "health");
    auto* villainHealth = properties.registerProperty(&villain, &villain.health, "health");

    heroHealth->set(90);
    villainHealth->set(40);

    std::cout << "  hero.health: " << heroHealth->get() << ", villain.health: " << villainHealth->get() << "\n";
    std::cout << "  same name, distinct Property instances per source: "
              << std::boolalpha
              << (properties.getProperty(&hero, "health") != properties.getProperty(&villain, "health")) << "\n";

    properties.removeProperty(&hero, "health");
    std::cout << "  after removing hero's 'health', villain's is unaffected: " << villainHealth->get() << "\n";
    std::cout << "  hero's 'health' now returns null: "
              << std::boolalpha << (properties.getProperty(&hero, "health") == nullptr) << "\n";
}

// ---------------------------------------------------------------------
// Demo 8: interpolate(t, fn) using std::sin()/std::cos() - Demo 3's escape
// hatch isn't limited to easing curves; fn can compute anything derived
// from t, trig included. Two examples: a cosine-eased scalar (a smoother,
// wave-derived alternative to EaseInOut), and a Point orbiting a center on
// a circle - a path a single [start,end] lerp could never produce, since
// start and end are the same point.
// ---------------------------------------------------------------------

void demoTrigInterpolation() {
    std::cout << "\n== Demo 8: interpolate(t, fn) using sin()/cos() ==\n";

    newui::PropertyManager::instance().clear();

    // Cosine interpolation: (1 - cos(t*pi)) / 2 eases smoothly in and out,
    // like EaseInOut, but derived from a continuous wave instead of a
    // piecewise quadratic - handy when a curve needs to match a
    // sine/cosine-based process driving something else (audio, physics).
    float brightness = 0.0f;
    auto* brightnessProperty = newui::PropertyManager::instance().registerProperty(nullptr, &brightness, "brightness");
    brightnessProperty->setupInterpolation(0.0f, 100.0f);

    auto cosineEase = [](float start, float end, float t) {
        float eased = (1.0f - std::cos(t * kPi)) * 0.5f;
        return start + (end - start) * eased;
        };

    std::cout << "  brightness (cosine ease): ";
    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        brightnessProperty->interpolate(t, cosineEase);
        std::cout << std::setw(7) << std::fixed << std::setprecision(2) << brightnessProperty->get();
    }
    std::cout << "\n";

    // Circular motion: cos() drives x, sin() drives y, both fed the same
    // angle - together they trace a full orbit as t goes 0 -> 1. fn here
    // ignores the start/end it's passed (an orbit has neither); nothing
    // requires fn to use them.
    newui::Point center(50.0f, 50.0f);
    float radius = 20.0f;
    newui::Point orbitPosition = center;
    auto* orbitProperty = newui::PropertyManager::instance().registerProperty(nullptr, &orbitPosition, "orbitPosition");

    auto orbit = [center, radius](newui::Point /*start*/, newui::Point /*end*/, float t) {
        float angle = t * 2.0f * kPi;
        return newui::Point(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
        };

    std::cout << "  orbit position (t = 0, 0.25, 0.5, 0.75, 1):\n";
    for (float t = 0.0f; t <= 1.0f; t += 0.25f) {
        orbitProperty->interpolate(t, orbit);
        std::cout << "    t=" << std::fixed << std::setprecision(2) << t
                  << " -> (" << orbitProperty->get().x << ", " << orbitProperty->get().y << ")\n";
    }
}

int main() {
    std::cout << "newui " << newui::version() << " - property examples\n";

    demoPropertyManagerBasics();
    demoBuiltInInterpolationKinds();
    demoCustomFunctionInterpolation();
    demoKeyframeInterpolation();
    demoPointProperty();
    demoColorProperty();
    demoMultipleSourcesSameName();
    demoTrigInterpolation();

    return 0;
}

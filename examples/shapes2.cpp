// Animating newui::shapes via the real animation engine (newui::Property/
// newui::Animation/newui::Key/newui::AnimationManager - property.h/
// animation.h), not ad hoc per-frame math. A ShapeLayerView (same pattern
// as shapes1.cpp) hosts five shapes, each with one property driven by a
// shared, looping Animation:
//   - an orbiting Circle: centerX/centerY driven by an angle that sweeps
//     0..2*PI once per loop (Linear - a real circle needs constant
//     angular speed, not an eased one)
//   - a "breathing" Circle: radius driven by a scale factor that rises
//     and falls twice per loop (EaseOut rising, EaseIn falling - the
//     same rise/fall Ease split animation1.cpp's Demo 2 uses, here
//     driving a shape's real geometry instead of a console printout)
//   - a bouncing RoundRect: y driven by a lift amount with the same
//     EaseOut-up/EaseIn-down curve, which reads as gravity: slow away
//     from the ground, fast back down into it
//   - a hue-cycling RoundRect: fill color driven by a hue in degrees
//     sweeping the full 0..360 wheel once per loop (via Color::fromHSL())
//   - a spinning RoundRect: rotationRadians driven by an angle sweeping
//     0..2*PI once per loop, transform().pivot() set to its own center
//     so it turns in place (see shapes1.cpp's AddRotatedSquaresShapes()
//     for the same pivot convention) - and, unlike the other four, bound
//     *directly* to g_spinSquare->transform() via PropertyManager::
//     registerProperty<float>(source, "rotationRadians") instead of
//     through g_state (see below) - Transform2D::rotationRadians()/
//     setRotationRadians() is already a real reflected property (every
//     newui::shapes field is - see shapes.h's own class comment), so
//     Animation can write straight into it with no shadow field and no
//     manual sync step for this one property at all.
//
// AnimationManager (application.h's own runLoop() comment: "code that
// needs to hook into idle time - e.g. AnimationManager::run()") owns and
// drives the Animation automatically off real elapsed time via RunLoop
// idle processing - see AnimationManager::addToRunLoop() in main(). Looping
// is Animation::setLooping(true) (animation.h): once AnimationManager's
// playback clock passes the Animation's endTime(), it wraps back to
// startTime() and keeps going, instead of holding the last Key's value
// forever the way a non-looping Animation does (see animation1.cpp's
// Demo 6 for that default behavior, and its new Demo 7 for the same
// setLooping(true) this file uses). Every Key at the loop's last frame is
// set to the same value as frame 0 (0 radians == 2*PI radians, hue 0 ==
// hue 360, ...), which is what makes each wrap seamless rather than a
// visible jump back to the start.
//
// Four of the Animation's five properties write into g_state below - a
// small POD struct of plain floats, the same shape newui::Property
// requires (see IsPodLike, property.h) and the same pattern
// animation1.cpp's Entity uses - rather than directly into the Shapes,
// since orbitAngle/pulseScale/bounceLift/hueDegrees each need a bit of
// math (trig, a base offset, an HSL-to-color conversion) a plain
// PropertyManager-driven setter call can't do on its own. The fifth
// (spinRadians) skips that shadow field entirely - see this file's own
// comment above on the spinning RoundRect - since it maps onto a real
// reflected property (Transform2D::rotationRadians()) with nothing extra
// to compute. SyncShapesToState() is the one place that reads g_state
// back out and pushes it into the shapes through their real setters (plus
// re-renders the window); it's subscribed
// as a plain function to AnimationManager::instance().onFrameChanged
// (animation.h), which fires with the new frame number every time
// AnimationManager's playback clock actually advances - the UI component
// (ShapeLayerView, via g_shapeView) listens for that and marks itself
// dirty (RootView::markDirty(), not invalidate() - markDirty() is what
// actually re-renders the shapes into RootView's off-screen buffer off
// their now-updated state; invalidate() alone only re-blits whatever was
// already in that buffer, which is what a first pass at this file got
// wrong - the window looked frozen except when some *other* markDirty()
// call, e.g. mouse-hover tracking, happened to force a real re-render),
// rather than something separately polling currentFrame() on every
// RunLoop idle pass to notice when it's changed. Delegate only
// accepts a plain function pointer, not a capturing lambda (see
// onFrameChanged's own comment, animation.h) - hence g_state/g_shapeView/
// the shape pointers below living at file scope instead of as main()
// locals a lambda could otherwise have captured.
//
// Deliberately not "using namespace newui::shapes" anywhere in this file -
// newui::shapes::Rectangle collides with wingdi.h's own
// ::Rectangle(HDC, ...) function (pulled in via newui.h's <windows.h>), so
// every shapes:: type below is spelled out instead (same reason shapes1.cpp/
// unittests/test_shapes.cpp already do this).

#include "newui/newui.h"

// Defined in the reflectgen-generated .cpp (compiled into the `newui`
// target this example links against - see shapes1.cpp's own comment on
// this same forward declaration) - has to run before main() registers
// the "rotationRadians" property below (PropertyManager::registerProperty
// <float>(source, "rotationRadians")), since reflection::classinfo()
// finds nothing for newui::shapes::Transform2D (or any other reflected
// class) until this has run at least once.
extern void registerReflectionData();

#include "newui/animation.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/shapes.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cmath>
#include <cstdint>

// Heap-only, like every other View/SubView in this codebase - see
// SubView's own class comment (subview.h). Identical to shapes1.cpp's
// own ShapeLayerView.
class ShapeLayerView : public newui::SubView {
public:
    newui::shapes::ShapeLayer& shapeLayer() { return shapeLayer_; }

    void paint(BLContext& ctx) override {
        newui::Rect visibleRect(0.0f, 0.0f, bounds().width(), bounds().height());
        shapeLayer_.render(ctx, visibleRect);
    }

private:
    newui::shapes::ShapeLayer shapeLayer_;
};

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    return newui::SyncReturn::Handled;
}

// The one thing the shared Animation ever writes into - five independent
// POD floats, each bound to its own newui::Property (see main()) and
// keyframed by name there. Defaults match the Animation's own frame-0 Key
// values, so the shapes built from these (before the first animated tick)
// already render in their correct starting pose.
struct AnimState {
    float orbitAngle = 0.0f;     // radians, 0..2*PI once per loop
    float pulseScale = 1.0f;     // 1.0..~1.4, twice per loop
    float bounceLift = 0.0f;     // 0..~55 pixels, twice per loop
    float hueDegrees = 0.0f;     // 0..360, once per loop
};

// Layout constants shared between main() (initial shape placement) and
// SyncShapesToState() (per-frame updates) below.
constexpr float kOrbitCenterX = 110.0f;
constexpr float kOrbitCenterY = 150.0f;
constexpr float kOrbitRadius = 55.0f;
constexpr float kPulseBaseRadius = 35.0f;
constexpr float kBounceBaseY = 270.0f;

// File-scope so SyncShapesToState() - a plain function, not a capturing
// lambda (see this file's own top comment on why) - can reach them. Every
// pointer here is set once in main() before AnimationManager ever fires
// onFrameChanged, and stays valid for the rest of the process (heap-
// allocated, owned by g_layer/the RootView tree - never deleted while
// app.run() is still pumping).
AnimState g_state;
ShapeLayerView* g_shapeView = nullptr;
newui::shapes::Circle* g_orbitDot = nullptr;
newui::shapes::Circle* g_pulseCircle = nullptr;
newui::shapes::RoundRect* g_bounceRect = nullptr;
newui::shapes::RoundRect* g_hueRect = nullptr;
newui::shapes::RoundRect* g_spinSquare = nullptr;

// Reads g_state back out and pushes it into the actual shapes through
// their real setters, then asks the window to repaint - see this file's
// own top comment for why this indirection (rather than Property/
// Animation writing straight into the Shapes) exists. Called once
// directly in main() (so the very first paint, before AnimationManager
// ever advances, already matches frame 0) and again every time
// AnimationManager::instance().onFrameChanged fires.
newui::SyncReturn SyncShapesToState(newui::AnimationManager&, std::uint64_t) {
    g_orbitDot->setCenterX(kOrbitCenterX + std::cos(g_state.orbitAngle) * kOrbitRadius);
    g_orbitDot->setCenterY(kOrbitCenterY + std::sin(g_state.orbitAngle) * kOrbitRadius);
    g_pulseCircle->setRadius(kPulseBaseRadius * g_state.pulseScale);
    g_bounceRect->setY(kBounceBaseY - g_state.bounceLift);
    float hue = g_state.hueDegrees >= 360.0f ? g_state.hueDegrees - 360.0f : g_state.hueDegrees;
    g_hueRect->style().fill().setColor(newui::Color::fromHSL(hue, 0.6f, 0.55f));
    // spinRadians isn't read here - Animation already wrote it straight
    // into g_spinSquare->transform() itself (see main()'s registerProperty
    // <float>(&g_spinSquare->transform(), "rotationRadians") call).

    g_shapeView->rootView()->markDirty();

    return newui::SyncReturn::Handled;
}

int main() {
    printf("newui %s - animated shapes example\n", newui::version());
    printf("An orbiting Circle, a breathing Circle, a bouncing RoundRect, a hue-cycling\n");
    printf("RoundRect, and a spinning RoundRect - all driven by a single looping\n");
    printf("newui::Animation via AnimationManager (property.h/animation.h).\n");

    registerReflectionData();

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("shapes2");
    app.setFrame(&frame);

    frame.setTitle("Animated Shapes Example");
    frame.setBounds(newui::Rect(10, 10, 620, 460));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(0.0f);
    rootLayout->setPadding(0.0f);
    root.setLayout(std::move(rootLayout));

    g_shapeView = new ShapeLayerView();
    g_shapeView->setVisible(true);
    g_shapeView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    g_shapeView->style().setBackgroundColor(newui::Color(0xffffffu, false));
    root.addChild(g_shapeView);

    newui::shapes::ShapeLayer& layer = g_shapeView->shapeLayer();

    // Static heading - the one shape here nothing animates.
    auto* title = new newui::shapes::Text();
    title->setText("animated shapes");
    title->setX(20.0f);
    title->setY(35.0f);
    title->font() = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);
    title->font().setSize(22.0f);
    title->font().setBold(true);
    title->style().fill().setColor(newui::Color(0x333333u, false));
    title->style().fill().setKind(newui::gfx::PaintKind::Color);
    layer.addShape(title);

    // Orbit: a faint static ring plus a static "sun" mark the orbit path
    // and center, so the orbiting dot's motion actually reads as an
    // orbit rather than just a dot wandering around empty space.
    auto* orbitRing = new newui::shapes::Circle();
    orbitRing->setCenterX(kOrbitCenterX);
    orbitRing->setCenterY(kOrbitCenterY);
    orbitRing->setRadius(kOrbitRadius);
    orbitRing->style().stroke().setColor(newui::Color(0xddddddu, false));
    orbitRing->style().stroke().setKind(newui::gfx::PaintKind::Color);
    orbitRing->style().stroke().setWidth(1.5f);
    layer.addShape(orbitRing);

    auto* orbitSun = new newui::shapes::Circle();
    orbitSun->setCenterX(kOrbitCenterX);
    orbitSun->setCenterY(kOrbitCenterY);
    orbitSun->setRadius(6.0f);
    orbitSun->style().fill().setColor(newui::Color(0xf2c94cu, false));
    orbitSun->style().fill().setKind(newui::gfx::PaintKind::Color);
    layer.addShape(orbitSun);

    g_orbitDot = new newui::shapes::Circle();
    g_orbitDot->setCenterX(kOrbitCenterX + kOrbitRadius);
    g_orbitDot->setCenterY(kOrbitCenterY);
    g_orbitDot->setRadius(10.0f);
    g_orbitDot->style().fill().setColor(newui::Color(0x4a90d9u, false));
    g_orbitDot->style().fill().setKind(newui::gfx::PaintKind::Color);
    g_orbitDot->style().glow().setEnabled(true);
    g_orbitDot->style().glow().setColor(newui::Color(0x4a90d9u, false));
    g_orbitDot->style().glow().setAmount(0.7f);
    g_orbitDot->style().glow().setSoftness(6.0f);
    layer.addShape(g_orbitDot);

    // Breathing Circle.
    const float kPulseCenterX = 300.0f;
    const float kPulseCenterY = 150.0f;

    g_pulseCircle = new newui::shapes::Circle();
    g_pulseCircle->setCenterX(kPulseCenterX);
    g_pulseCircle->setCenterY(kPulseCenterY);
    g_pulseCircle->setRadius(kPulseBaseRadius);
    g_pulseCircle->style().fill().setColor(newui::Color(0x9b59b6u, false));
    g_pulseCircle->style().fill().setKind(newui::gfx::PaintKind::Color);
    layer.addShape(g_pulseCircle);

    // Spinning RoundRect - pivot set to its own center (see shapes1.cpp's
    // AddRotatedSquaresShapes() for the same convention) so it turns in
    // place instead of swinging around the canvas origin.
    const float kSpinCenterX = 500.0f;
    const float kSpinCenterY = 150.0f;
    const float kSpinSize = 50.0f;

    g_spinSquare = new newui::shapes::RoundRect();
    g_spinSquare->setX(kSpinCenterX - kSpinSize * 0.5f);
    g_spinSquare->setY(kSpinCenterY - kSpinSize * 0.5f);
    g_spinSquare->setWidth(kSpinSize);
    g_spinSquare->setHeight(kSpinSize);
    g_spinSquare->setRadiusX(10.0f);
    g_spinSquare->setRadiusY(10.0f);
    g_spinSquare->style().fill().setColor(newui::Color(0x2a9d8fu, false));
    g_spinSquare->style().fill().setKind(newui::gfx::PaintKind::Color);
    g_spinSquare->transform().setPivot(newui::Point(kSpinCenterX, kSpinCenterY));
    layer.addShape(g_spinSquare);

    // Bouncing RoundRect.
    const float kBounceX = 85.0f;
    const float kBounceSize = 60.0f;

    g_bounceRect = new newui::shapes::RoundRect();
    g_bounceRect->setX(kBounceX);
    g_bounceRect->setY(kBounceBaseY);
    g_bounceRect->setWidth(kBounceSize);
    g_bounceRect->setHeight(kBounceSize);
    g_bounceRect->setRadiusX(10.0f);
    g_bounceRect->setRadiusY(10.0f);
    g_bounceRect->style().fill().setColor(newui::Color(0xe07a5fu, false));
    g_bounceRect->style().fill().setKind(newui::gfx::PaintKind::Color);
    layer.addShape(g_bounceRect);

    // Hue-cycling RoundRect.
    g_hueRect = new newui::shapes::RoundRect();
    g_hueRect->setX(270.0f);
    g_hueRect->setY(kBounceBaseY);
    g_hueRect->setWidth(90.0f);
    g_hueRect->setHeight(90.0f);
    g_hueRect->setRadiusX(14.0f);
    g_hueRect->setRadiusY(14.0f);
    g_hueRect->style().fill().setKind(newui::gfx::PaintKind::Color);
    layer.addShape(g_hueRect);

    // The animation: one Animation, five Properties (one per AnimState
    // field), keyframed together and owned by AnimationManager rather
    // than hand-rolled here - see this file's own top comment for the
    // looping convention (frame-0 and frame-kLoopDuration Keys always
    // agree, which is what makes Animation::setLooping(true)'s wraparound
    // seamless) and the EaseOut-up/EaseIn-down bounce curve. Each
    // keyframe is a single Animation::addKey() call - addKey() always
    // creates a new Key rather than finding-or-creating by name (see its
    // own implementation, animation.cpp), so a given keyframe's Key* is
    // captured once and reused for every property it sets, not re-fetched
    // via a second addKey() call at the same frame.
    constexpr std::uint64_t kLoopDuration = 180;  // 6 seconds at 30fps

    newui::PropertyManager::instance().clear();
    auto* orbitAngleProp = newui::PropertyManager::registerProperty(&g_state, &g_state.orbitAngle, "orbitAngle");
    auto* pulseScaleProp = newui::PropertyManager::registerProperty(&g_state, &g_state.pulseScale, "pulseScale");
    auto* bounceLiftProp = newui::PropertyManager::registerProperty(&g_state, &g_state.bounceLift, "bounceLift");
    auto* hueDegreesProp = newui::PropertyManager::registerProperty(&g_state, &g_state.hueDegrees, "hueDegrees");

    // Bound directly to the shape's own Transform2D - not to g_state -
    // via the reflection system (reflection.h), rather than a raw field
    // address: Transform2D's fields are private, only rotationRadians()/
    // setRotationRadians() exposed (see shapes.h's own class comment),
    // and reflectgen already registered that pair as a real property
    // named "rotationRadians" when the newui target built. Animation
    // writes straight through it from here on - see this file's own top
    // comment.
    auto* spinRadiansProp =
        newui::PropertyManager::registerProperty<float>(&g_spinSquare->transform(), "rotationRadians");

    constexpr float kPi = 3.14159265359f;
    constexpr float kTwoPi = 6.28318530718f;

    newui::AnimationManager::clear();
    newui::AnimationManager::setFrameRate(newui::FrameRate::FPS30());

    newui::Animation* animation = newui::AnimationManager::addAnimation("loop", 0, kLoopDuration);
    animation->setLooping(true);

    newui::Key* k0 = animation->addKey("start", 0);
    k0->setValue(orbitAngleProp, 0.0f);
    k0->setValue(pulseScaleProp, 1.0f);
    k0->setValue(bounceLiftProp, 0.0f);
    k0->setValue(hueDegreesProp, 0.0f);
    k0->setValue(spinRadiansProp, 0.0f);

    newui::Key* k1 = animation->addKey("quarter", kLoopDuration / 4);
    k1->setValue(pulseScaleProp, 1.4f, newui::InterpolationKind::EaseOut);
    k1->setValue(bounceLiftProp, 55.0f, newui::InterpolationKind::EaseOut);
    k1->setValue(hueDegreesProp, 90.0f);

    newui::Key* k2 = animation->addKey("half", kLoopDuration / 2);
    k2->setValue(orbitAngleProp, kPi);
    k2->setValue(pulseScaleProp, 1.0f, newui::InterpolationKind::EaseIn);
    k2->setValue(bounceLiftProp, 0.0f, newui::InterpolationKind::EaseIn);
    k2->setValue(hueDegreesProp, 180.0f);
    k2->setValue(spinRadiansProp, kPi);

    newui::Key* k3 = animation->addKey("three-quarter", 3 * kLoopDuration / 4);
    k3->setValue(pulseScaleProp, 1.4f, newui::InterpolationKind::EaseOut);
    k3->setValue(bounceLiftProp, 55.0f, newui::InterpolationKind::EaseOut);
    k3->setValue(hueDegreesProp, 270.0f);

    newui::Key* k4 = animation->addKey("end", kLoopDuration);
    k4->setValue(orbitAngleProp, kTwoPi);
    k4->setValue(pulseScaleProp, 1.0f, newui::InterpolationKind::EaseIn);
    k4->setValue(bounceLiftProp, 0.0f, newui::InterpolationKind::EaseIn);
    k4->setValue(hueDegreesProp, 360.0f);
    k4->setValue(spinRadiansProp, kTwoPi);

    SyncShapesToState(newui::AnimationManager::instance(), 0);  // frame 0, before the window ever paints

    // AnimationManager::addToRunLoop() registers its own idle task that
    // drives every registered Animation (this one included) forward off
    // real elapsed time - see Application::runLoop()'s own comment. The
    // UI side of this - reading the resulting AnimState back out and
    // repainting - just listens for AnimationManager::instance().
    // onFrameChanged instead of separately polling currentFrame() on
    // every RunLoop idle pass to notice when it's changed (see this
    // file's own top comment, and onFrameChanged's own comment,
    // animation.h).
    newui::AnimationManager::instance().onFrameChanged.add(&SyncShapesToState);
    newui::AnimationManager::addToRunLoop(app.runLoop());

    app.run();

    return 0;
}

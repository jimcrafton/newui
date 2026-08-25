// A first look at newui::Overlay (overlay.h) - a full-window layer
// RootView paints last, on top of every child SubView, completely
// independent of the normal view tree. This demo builds a small toolbar
// (a button, a toggle button, and a slider) and a HelpOverlay that's
// hidden by default; pressing H toggles it on/off.
//
// HelpOverlay demonstrates every facet of Overlay at once:
//   - fillColor()/opacity()/compositingOp(): a soft white wash across the
//     whole window (CompScreen lightens whatever's underneath rather than
//     alpha-blending over it - Overlay's own default compositingOp/
//     opacity, see overlay.h).
//   - shapeLayer(): a dark, translucent RoundRect "card" plus a handful of
//     Text lines on top of the wash - the actual help content.
//   - viewSized(): re-centers the card (and reflows its text lines) any
//     time the window is resized, so the help panel stays centered
//     instead of drifting toward one corner or clipping off-window.
//
// Showing the card also plays a slide-in: it starts off-screen to the
// left, slides right past its resting center (a small overshoot), then
// eases back to settle there - the same real newui::Animation/Property/
// AnimationManager engine examples/shapes2.cpp uses (an EaseOut-in/
// EaseOut-back pair of Keys, not hand-rolled per-frame math), restarted
// (Animation::setStartTime()) every time H shows the card - see this
// file's own HelpSlideState/SyncHelpSlide()/main() for the setup.

#include "newui/newui.h"
#include "newui/animation.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/fontmanager.h"
#include "newui/frame.h"
#include "newui/keyboard_constants.h"
#include "newui/overlay.h"
#include "newui/rootview.h"
#include "newui/shapes.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"
#include "newui/bundle.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

// Defined in the reflectgen-generated .cpp - see shapes1.cpp's own
// comment on this same forward declaration for why it's needed (Overlay,
// like every other reflectable class in newui/*.h, gets its Class/
// Property data registered there).
extern void registerReflectionData();

// A centered "help card": a dark, translucent RoundRect backdrop (for
// contrast against the wash Overlay::paint()'s base implementation draws
// first) with a bold title line and a few plain caption lines under it -
// panel_/lines_ are built once in the constructor and just repositioned
// in viewSized(), never rebuilt, so a resize is cheap (a handful of
// setX()/setY() calls, no new Shape allocations).
class HelpOverlay : public newui::Overlay {
public:
    // Fixed card width - shared with main()'s Animation setup below, which
    // needs it to pick an off-screen starting offset guaranteed to clear
    // the card regardless of the window's own width.
    static constexpr float kPanelWidth = 380.0f;

    HelpOverlay() {
        // Hidden until the user presses H - see main()'s onKeyDown hook.
        setVisible(false);

        // Soft white wash over the whole window - opacity()/
        // compositingOp() are left at Overlay's own defaults (0.4,
        // CompScreen - see overlay.h), so this is the flat-fill facet of
        // Overlay demonstrated with nothing but a color.
        setFillColor(newui::Color(1.0f, 1.0f, 1.0f, 1.0f));

        panel_ = new newui::shapes::RoundRect();
        panel_->setRadiusX(14.0f);
        panel_->setRadiusY(14.0f);
        panel_->style().fill().setColor(newui::Color(0.0f, 0.0f, 0.0f, 0.72f));
        panel_->style().fill().setKind(newui::gfx::PaintKind::Color);
        panel_->style().stroke().setColor(newui::Color(1.0f, 1.0f, 1.0f, 0.25f));
        panel_->style().stroke().setKind(newui::gfx::PaintKind::Color);
        panel_->style().stroke().setWidth(1.0f);
        shapeLayer().addShape(panel_);

        const std::vector<std::string> kLines = {
            "Help",
            "H \xE2\x80\x94 toggle this help overlay",
            "Click Me \xE2\x80\x94 logs a click to the console",
            "Toggle \xE2\x80\x94 flips its own checked state",
            "Slider \xE2\x80\x94 drag to change its value",
        };

        for (std::size_t i = 0; i < kLines.size(); ++i) {
            auto* line = new newui::shapes::Text();
            line->setText(kLines[i]);
            line->font() = newui::FontManager::getSystemFont(newui::SystemUIFont::Message);

            bool isTitle = (i == 0);
            line->font().setSize(isTitle ? 22.0f : 16.0f);
            line->font().setBold(isTitle);

            line->style().fill().setColor(newui::Color(1.0f, 1.0f, 1.0f, 1.0f));
            line->style().fill().setKind(newui::gfx::PaintKind::Color);

            shapeLayer().addShape(line);
            lines_.push_back(line);
        }
    }

    // Called by RootView::setBounds()/setOverlay() with the RootView's
    // current bounds - recomputes the card's resting, centered layout
    // (restPanelX_/restPanelY_/restTextX_/restLineY_/panelHeight_), then
    // calls applyOffset() to actually position panel_/lines_ from it.
    // Runs even while hidden (cheap, and keeps everything in sync for the
    // moment visible() flips true).
    void viewSized(const newui::Rect& newBounds) override {
        const float kPadding = 28.0f;
        const float kTitleGap = 34.0f;
        const float kLineGap = 24.0f;

        panelHeight_ = kPadding * 2.0f + kTitleGap + kLineGap * float(lines_.size() - 1);
        restPanelX_ = (newBounds.size().width - kPanelWidth) * 0.5f;
        restPanelY_ = (newBounds.size().height - panelHeight_) * 0.5f;
        restTextX_ = restPanelX_ + kPadding;

        restLineY_.resize(lines_.size());
        float textY = restPanelY_ + kPadding + 8.0f;
        for (std::size_t i = 0; i < lines_.size(); ++i) {
            restLineY_[i] = textY;
            textY += (i == 0) ? kTitleGap : kLineGap;
        }

        applyOffset();
    }

    // Driven by main()'s "helpSlide" Animation, through SyncHelpSlide()
    // (AnimationManager::onFrameChanged) - shifts the whole card (panel_
    // plus every line) horizontally by offsetX relative to the resting,
    // centered position viewSized() computed. See HelpSlideState below for
    // why offsetX lives in a small shadow struct rather than a direct
    // newui::Property on the card itself.
    void setSlideOffsetX(float offsetX) {
        slideOffsetX_ = offsetX;
        applyOffset();
    }

private:
    // Positions panel_/lines_ from the cached rest layout plus
    // slideOffsetX_ - the one place that actually writes to the shapes,
    // called by both viewSized() (a resize) and setSlideOffsetX() (an
    // animation frame).
    void applyOffset() {
        panel_->setX(restPanelX_ + slideOffsetX_);
        panel_->setY(restPanelY_);
        panel_->setWidth(kPanelWidth);
        panel_->setHeight(panelHeight_);

        for (std::size_t i = 0; i < lines_.size(); ++i) {
            lines_[i]->setX(restTextX_ + slideOffsetX_);
            lines_[i]->setY(restLineY_[i]);
        }
    }

    newui::shapes::RoundRect* panel_ = nullptr;
    std::vector<newui::shapes::Text*> lines_;

    float restPanelX_ = 0.0f;
    float restPanelY_ = 0.0f;
    float restTextX_ = 0.0f;
    float panelHeight_ = 0.0f;
    std::vector<float> restLineY_;

    float slideOffsetX_ = 0.0f;
};

// The one thing main()'s "helpSlide" Animation ever writes into - one
// offset has to move the panel *and* every line together, which a single
// reflected setter on the card can't do on its own, so this is a small
// shadow field instead - the same split shapes2.cpp's AnimState uses for
// bounceLift/pulseScale/hueDegrees (see that file's own top comment).
struct HelpSlideState {
    float offsetX = 0.0f;
};

// File-scope so SyncHelpSlide() below - a plain function, not a capturing
// lambda (AnimationManager::onFrameChanged only accepts a plain function
// pointer, see animation.h) - can reach them. Both set once in main()
// before AnimationManager ever fires onFrameChanged, and valid for the
// rest of the process - same as shapes2.cpp's g_shapeView/g_state.
HelpSlideState g_slideState;
HelpOverlay* g_help = nullptr;
newui::RootView* g_root = nullptr;
newui::Slider* g_slider = nullptr;

// Reads g_slideState back out, pushes it into g_help, and asks the window
// to repaint - see shapes2.cpp's SyncShapesToState() for the same
// pattern. Subscribed to AnimationManager::instance().onFrameChanged in
// main(), and also called directly right after restarting the slide-in so
// the very first painted frame already shows the card at its off-screen
// starting position (same "apply frame 0 immediately" idiom shapes2.cpp's
// main() uses at startup), rather than waiting on the next RunLoop idle
// pass.
newui::SyncReturn SyncHelpSlide(newui::AnimationManager&, std::uint64_t) {
    g_help->setSlideOffsetX(g_slideState.offsetX);
    g_root->markDirty();
    return newui::SyncReturn::Handled;
}

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());

    // Real end-to-end proof of this session's Animation-persistence work
    // (HANDOFF.md) - a throwaway Animation targeting the real, reflected
    // Slider::value() property (registered with AnimationTargetRegistry in
    // main(), below), so writeFrame() actually has something in its new
    // "animations" block to write out.
    //
    // A "sliderPulse" Animation may already be sitting in AnimationManager
    // if main()'s own loadAnimations() call found and replayed one from
    // the previous run's saved file - real, reproduced bug found live
    // (AV inside Bundle::writeFrame()'s animation-target-descriptor walk,
    // reading freed memory - kv->property() pointing at an already-deleted
    // ObservableProperty): AnimationManager::addAnimation() never dedups
    // by name, so calling it again here left two "sliderPulse" entries in
    // AnimationManager::animations(), and PropertyManager::
    // registerProperty() unconditionally *deletes* whatever was already
    // registered for the same (name,source) pair (property.h's own
    // documented contract) - so blindly re-registering g_slider's "value"
    // property here freed the ObservableProperty the just-loaded
    // Animation's own Keys still pointed at. Fix: drop any stale
    // "sliderPulse" left over from a load before adding a fresh one, and
    // reuse g_slider's already-registered "value" property (via
    // PropertyManager::getProperty()) instead of unconditionally
    // re-registering it - the same check-first pattern
    // AnimationTargetRegistry::buildKeyValue() (animation.h) already uses
    // for exactly this reason.
    std::vector<newui::Animation*> stalePulses;
    for (const auto& anim : newui::AnimationManager::animations()) {
        if (anim->name() == "sliderPulse") {
            stalePulses.push_back(anim.get());
        }
    }
    for (newui::Animation* anim : stalePulses) {
        newui::AnimationManager::removeAnimation(anim);
    }

    newui::Animation* slidePulse = newui::AnimationManager::addAnimation("sliderPulse", 0, 30);
    newui::PropertyBase* existingValueProp = newui::PropertyManager::getProperty(g_slider, "value");
    auto* sliderValueProp = existingValueProp != nullptr
        ? static_cast<newui::ObservableProperty<newui::Slider, float>*>(existingValueProp)
        : newui::PropertyManager::registerProperty<float>(g_slider, "value");
    slidePulse->addKey("start", 0)->setValue(sliderValueProp, g_slider->value());
    slidePulse->addKey("end", 30)->setValue(sliderValueProp, 100.0f, newui::InterpolationKind::EaseOut);

    newui::Bundle::instance().writeFrame(frame);

    return newui::SyncReturn::Handled;
}

int main() {
    printf("newui %s - overlay example\n", newui::version());
    printf("Press H to toggle a full-window help overlay (RootView::setOverlay(), overlay.h).\n");

    registerReflectionData();
    newui::AnimationTargetRegistry::registerTarget<newui::Slider, float>();

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("overlay1");
    app.setFrame(&frame);

    frame.setName("overl1ay1");
    frame.onClosed += FrameClosed;

    // "helpSlide" Animation: slides the help card in from fully off-screen
    // left, overshoots a bit past its resting center, then eases back to
    // settle exactly there - real newui::Property/Animation/
    // AnimationManager (property.h/animation.h), the same engine
    // examples/shapes2.cpp uses, not hand-rolled per-frame math. One-shot
    // (setLooping() left at its default false) rather than shapes2.cpp's
    // looping demo - it's restarted (Animation::setStartTime()) every
    // time H shows the card instead (see the onKeyDown hook below). Set
    // up before loadFrame() below, not after: loadFrame() restores
    // whatever "animations" block the file has (including a previously
    // saved "sliderPulse") as part of the same call now, and
    // AnimationManager::clear()/PropertyManager::instance().clear() here
    // would otherwise wipe that back out again.
    constexpr std::uint64_t kSlideDuration = 20;  // ~0.67s at 30fps

    newui::PropertyManager::instance().clear();
    auto* slideOffsetProp =
        newui::PropertyManager::registerProperty(&g_slideState, &g_slideState.offsetX, "helpSlideOffsetX");

    newui::AnimationManager::clear();
    newui::AnimationManager::setFrameRate(newui::FrameRate::FPS30());

    newui::Animation* slideAnim = newui::AnimationManager::addAnimation("helpSlide", 0, kSlideDuration);

    newui::Key* slideStart = slideAnim->addKey("start", 0);
    slideStart->setValue(slideOffsetProp, -(HelpOverlay::kPanelWidth + 40.0f));

    // EaseOut into both the overshoot and the final settle - decelerating
    // into each, rather than shapes2.cpp's bounce (EaseOut-up/EaseIn-down,
    // which mimics gravity accelerating back toward the ground) - a soft
    // landing reads better for a card settling into place than a hard one.
    newui::Key* slideOvershoot = slideAnim->addKey("overshoot", (kSlideDuration * 7) / 10);
    slideOvershoot->setValue(slideOffsetProp, 24.0f, newui::InterpolationKind::EaseOut);

    newui::Key* slideSettle = slideAnim->addKey("settle", kSlideDuration);
    slideSettle->setValue(slideOffsetProp, 0.0f, newui::InterpolationKind::EaseOut);

    newui::AnimationManager::instance().onFrameChanged.add(&SyncHelpSlide);
    newui::AnimationManager::addToRunLoop(app.runLoop());

    // The UI tree itself - title/bounds/every View (bounds/style-derived
    // content/text/childViews, recursively) plus a previously saved
    // "sliderPulse" Animation, if any - all loaded from
    // Resources/overl1ay1.newui rather than hand-built here. That file is
    // the actual source of truth for this UI from here on: any further
    // change to what's on screen (new controls, retitled buttons,
    // rearranged rows, ...) should happen by editing/regenerating it, not
    // by adding more view-construction code here - see HANDOFF.md.
    if (!newui::Bundle::instance().loadFrame(frame)) {
        printf("Could not load Resources/overl1ay1.newui - delete-and-recreate it from a hand-built "
               "tree first (see HANDOFF.md's note on this example's loadFrame()-only migration).\n");
        return 1;
    }
    if (!newui::AnimationManager::animations().empty()) {
        printf("Loaded a saved \"sliderPulse\" animation from the previous run - watch the slider.\n");
    }

    newui::RootView& root = frame.rootView();

    // Background color is theme-derived at runtime (UIColorManager tracks
    // OS Light/Dark mode), not persisted UI content - reapplied here the
    // same way a ThemedViewStyle reopens its own HTHEME rather than being
    // baked into the saved file.
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    // Layout is a genuinely persisted property now too (View::layout()/
    // setLayout(), reflection.h/reflectgen.py's new "T*-or-T&-returning
    // getter + ownership-taking std::unique_ptr<T> setter" property shape -
    // see HANDOFF.md) - root's/buttonRow's/sliderRow's real FlexLayout
    // instances (orientation/spacing/padding included) all came back from
    // loadFrame() above already, no manual reattachment needed.

    auto* buttonRow = dynamic_cast<newui::SubView*>(root.findView("buttonRow"));
    auto* sliderRow = dynamic_cast<newui::SubView*>(root.findView("sliderRow"));
    auto* clickButton = dynamic_cast<newui::Button*>(root.findView("clickButton"));
    auto* toggleButton = dynamic_cast<newui::Button*>(root.findView("toggleButton"));
    auto* sliderValueLabel = dynamic_cast<newui::Label*>(root.findView("valueLabel"));
    auto* slider = dynamic_cast<newui::Slider*>(root.findView("valueSlider"));
    if (buttonRow == nullptr || sliderRow == nullptr || clickButton == nullptr || toggleButton == nullptr ||
            sliderValueLabel == nullptr || slider == nullptr) {
        printf("Resources/overl1ay1.newui is missing one of the named views this example wires up by "
               "hand (buttonRow/sliderRow/clickButton/toggleButton/valueLabel/valueSlider) - can't "
               "continue.\n");
        return 1;
    }

    clickButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [](newui::Control&) -> newui::SyncReturn {
            printf("Click Me pressed\n");
            return newui::SyncReturn::Handled;
        }));

    toggleButton->onCheckedChanged.add(std::function<newui::SyncReturn(newui::Button&)>(
        [](newui::Button& sender) -> newui::SyncReturn {
            printf("Toggle button is now %s\n", sender.isChecked() ? "ON" : "off");
            return newui::SyncReturn::Handled;
        }));

    slider->onValueChanged.add(std::function<newui::SyncReturn(newui::Slider&)>(
        [sliderValueLabel](newui::Slider& sender) -> newui::SyncReturn {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "Value: %d", static_cast<int>(sender.value()));
            sliderValueLabel->setText(buf);
            return newui::SyncReturn::Handled;
        }));
    g_slider = slider;

    // The overlay itself - null until this call (RootView::overlay_'s own
    // default, rootview.h). setOverlay() immediately calls viewSized()
    // with root's current bounds, so the card is already centered before
    // the first time it's shown.
    auto* help = new HelpOverlay();
    root.setOverlay(std::unique_ptr<HelpOverlay>(help));
    g_help = help;
    g_root = &root;

    // H toggles help->visible() and asks root to repaint - Overlay itself
    // has no path back to RootView to schedule that on its own (see
    // Overlay's class comment, overlay.h), so whoever flips visible() is
    // responsible for markDirty() too, same as any other state change
    // that should show up on screen. Hooked on root.onKeyDown, not some
    // focused SubView's - RootView::keyEvent() (rootview.cpp) always
    // fires root's own onKeyDown first regardless of what's focused, so
    // this works even after clicking a button/toggle/slider above.
    root.onKeyDown += [help, slideAnim](newui::View&, std::uint32_t /*keyMask*/, int /*keyCharVal*/,
                                         int /*repeatCount*/, std::uint32_t VKeyCode) -> newui::SyncReturn {
        if (VKeyCode != newui::vkLetterH) {
            return newui::SyncReturn::Ignored;
        }

        bool showing = !help->visible();
        help->setVisible(showing);

        if (showing) {
            // Restart the one-shot slide-in from scratch every time the
            // card is shown, anchored to "now" in AnimationManager's own
            // playback clock.
            std::uint64_t now = newui::AnimationManager::currentFrame();
            slideAnim->setStartTime(now);
            slideAnim->processFrame(now);
        }

        SyncHelpSlide(newui::AnimationManager::instance(), 0);  // repositions (if showing) and always repaints
        return newui::SyncReturn::Handled;
    };

    app.run();    


    return 0;
}

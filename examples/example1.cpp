

#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>



#include "newui/newui.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/color.h"
#include "newui/animation.h"
#include <blend2d/blend2d.h>

#include <iostream>


newui::SyncReturn FrameClosed(newui::Frame& frame) {
	printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
	return newui::SyncReturn::Handled;
}

// registerProperty()'s source must be a real object instance and field
// one of *that same instance's* member fields. We want to animate the
// RootView's style, so the property belongs on the LabelStyle instance
// itself - but LabelStyle has no field a Property can bind to for this:
// backgroundFill is a BLVar, not the POD scalar/struct Property requires.
// This subclass adds one: bgColor is the field the Property binds to;
// BackgroundColorChanged pushes it into backgroundFill on change.
struct AnimatedLabelStyle : newui::LabelStyle {
    newui::Color bgColor = newui::Color::fromName("red");
};

// onValueChanged's ValueChangedDelegate passes the source instance and a
// pointer to the (already-updated) field straight through as extra
// arguments (see property.h), so this trampoline needs no casts or
// method calls at all: style is the same AnimatedLabelStyle instance
// bgColor lives on (the way a bound callback normally would reach its
// instance - see delegates1.cpp's Demo 7 - but strongly typed instead of
// a void*), and color is the new value directly. ViewStyle::markDirty()
// finds its own way to the owning RootView (via view()->rootView(),
// populated by View::setStyle()/addChild() - see rootview.cpp/
// subview.cpp), so no RootView-specific downcast is needed here either,
// even though this style happens to be attached directly to one.
newui::SyncReturn BackgroundColorChanged(newui::ObservableProperty<AnimatedLabelStyle, newui::Color>& property,
        AnimatedLabelStyle* style, newui::Color* color) {
	style->setBackgroundColor(*color);
	// invalidate() alone would just re-blit the existing pixel buffer -
	// markDirty() is what re-runs paintStyle()/paint() into it first.
	style->markDirty();

	return newui::SyncReturn::Handled;
}


int main() {
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);


    std::cout << "newui " << newui::version() << "\n";

    newui::Frame frame;
    

    newui::Application& app = newui::Application::instance();

    app.setName("example1");
    app.setFrame(&frame);

    frame.setTitle("Example 1");
    frame.setBounds(newui::Rect(10, 10, 1200, 600));
    frame.onClosed += FrameClosed;

    newui::RootView& v = frame.getView();
    //v.style().backgroundFill = newui::Color::fromName("yellow").toBLRgba32();
    //v.style().borderFill = newui::Color::fromName("lightblue").toBLRgba32();
    //v.style().borderWidth = 2.0;
    auto labelStyle = std::make_unique<AnimatedLabelStyle>();

    labelStyle->setBackgroundColor(newui::Color::fromSystemColor(newui::SystemColor::ButtonHighlight));

	labelStyle->text = "Hello, World!";
    labelStyle->textColor = newui::Color::fromName("black").toBLRgba32();

    // Grab the raw address before ownership moves into v below - the
    // AnimatedLabelStyle itself lives on the heap and doesn't move; only
    // which unique_ptr owns it changes, so labelStylePtr stays valid for
    // as long as v keeps this style set.
    AnimatedLabelStyle* labelStylePtr = labelStyle.get();
    v.setStyle(std::move(labelStyle));

    // Animate the root view's background color from red to blue over 3
    // seconds via a Property bound directly to the LabelStyle instance
    // attached to v - registerProperty()'s source is that same instance,
    // field is its own bgColor member. PropertyManager/AnimationManager
    // are both process-wide singletons - a Property or a playback clock
    // can only ever be reached through them, never constructed directly.
    newui::PropertyManager::clear();
    auto* bgColorProperty =
        newui::PropertyManager::registerProperty(labelStylePtr, &labelStylePtr->bgColor, "backgroundColor");
    bgColorProperty->onValueChanged.add(&BackgroundColorChanged);

    
    newui::AnimationManager::clear();  // defaults to 30 fps
    constexpr int kBackgroundColorDurationFrames = 3 * 30;  // 3 seconds @ 30 fps

    newui::Animation* bgColorAnimation =
        newui::AnimationManager::addAnimation("background-color", 0, kBackgroundColorDurationFrames);
    bgColorAnimation->addKey("red", 10)->setValue(bgColorProperty, newui::Color::fromName("red"));
    bgColorAnimation->addKey("blue", kBackgroundColorDurationFrames)
        ->setValue(bgColorProperty, newui::Color::fromName("blue"),
            [](newui::Color start, newui::Color end, float t) {
                return newui::Color(
                    start.r + (end.r - start.r) * t,
                    start.g + (end.g - start.g) * t,
                    start.b + (end.b - start.b) * t,
                    start.a + (end.a - start.a) * t);
                });

    // Apply the starting color immediately so the very first paint
    // already shows red, rather than the LabelStyle's original
    // ButtonHighlight background until AnimationManager's idle-driven
    // clock reaches its first frame.
    bgColorAnimation->processFrame(0);

    newui::AnimationManager::addToRunLoop(app.runLoop());

    app.run();

    return 0;
}

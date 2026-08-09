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

// property.source() was set to the RootView being animated (see main()),
// so this trampoline can push the interpolated color into that view's
// style and get it repainted, the way a Delegate callback bound to a
// specific instance normally would (see delegates1.cpp's Demo 7) - a
// plain function pointer is all onValueChanged can hold.
newui::SyncReturn BackgroundColorChanged(newui::PropertyBase& property) {
	auto& colorProperty = static_cast<newui::Property<newui::Color>&>(property);
	auto* view = static_cast<newui::RootView*>(property.source());

	view->style().backgroundFill = colorProperty.get().toBLRgba32();
	// invalidate() alone would just re-blit the existing pixel buffer -
	// markDirty() is what re-runs paintStyle()/paint() into it first.
	view->markDirty();

	return newui::SyncReturn::Handled;
}


int main() {
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
    auto labelStyle = std::make_unique<newui::LabelStyle>();
	
    labelStyle->backgroundFill = newui::Color::fromSystemColor(newui::SystemColor::ButtonHighlight).toBLRgba32();
    
	labelStyle->text = "Hello, World!";
    labelStyle->textColor = newui::Color::fromName("black").toBLRgba32();
    
    v.setStyle(std::move(labelStyle));

    // Animate the root view's background color from red to blue over 3
    // seconds. bgColor/bgColorProperty/animationManager all need to stay
    // alive until app.run() returns, since AnimationManager drives the
    // animation from RunLoop idle time for as long as the loop runs.
    newui::Color bgColor = newui::Color::fromName("red");
    newui::Property<newui::Color> bgColorProperty("backgroundColor", &v, &bgColor);
    bgColorProperty.onValueChanged.add(&BackgroundColorChanged);

    newui::AnimationManager animationManager;  // defaults to 30 fps
    constexpr int kBackgroundColorDurationFrames = 3 * 30;  // 3 seconds @ 30 fps

    newui::Animation* bgColorAnimation =
        animationManager.addAnimation("background-color", 0, kBackgroundColorDurationFrames);
    bgColorAnimation->addKey("red", 0)->setValue(&bgColorProperty, newui::Color::fromName("red"));
    bgColorAnimation->addKey("blue", kBackgroundColorDurationFrames)
        ->setValue(&bgColorProperty, newui::Color::fromName("blue"),
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

    animationManager.run(app.runLoop());

    /*
    v.onRedrawNeeded += [](newui::RootView& view) -> newui::SyncReturn {
        BLContext ctx(view.getImageBuffer());

        ctx.fill_all(BLRgba32(255, 0, 0));

        newui::Size size = view.getBounds().size();
        double cx = size.width * 0.5;
        double top = size.height * 0.2;
        double left = size.width * 0.2;
        double right = size.width * 0.8;
        double bottom = size.height * 0.8;

        ctx.set_fill_style(BLRgba32(0, 255, 0));
        ctx.fill_triangle(cx, top, left, bottom, right, bottom);

        ctx.set_stroke_style(BLRgba32(0, 0, 255));
        ctx.set_stroke_width(4.0);
        ctx.stroke_triangle(cx, top, left, bottom, right, bottom);

        ctx.end();

        return newui::SyncReturn::Handled;
        };
        */
	v.onSizeChanged += [](newui::View& v, const newui::Size& newSize) -> newui::SyncReturn {
		newui::RootView& view = reinterpret_cast<newui::RootView&>(v);
		printf("View (%p, hwnd: %p) size changed to %.0fx%.0f\n", &view, view.getFrame()->frameHandle(), newSize.width, newSize.height);
        return newui::SyncReturn::Handled;
		};  


    v.onMouseMove += [](newui::View& v, const newui::Point& pt, std::uint32_t btnMask, std::uint32_t keyMask) -> newui::SyncReturn {
		
		printf("View (%p) mouse moved to %.0f,%.0f\n", &v, pt.x, pt.y);
        return newui::SyncReturn::Handled;
		};


    v.onKeyPress += [](newui::View& v, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode) -> newui::SyncReturn {

		char c = (char)keyCharVal;
        printf("View (%p) key pressed: '%c'\n", &v, c);
        return newui::SyncReturn::Handled;
        };

    app.run();

    return 0;
}

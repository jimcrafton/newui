#include "newui/newui.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/color.h"
#include <blend2d/blend2d.h>

#include <iostream>


newui::SyncReturn FrameClosed(newui::Frame& frame) {
	printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
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

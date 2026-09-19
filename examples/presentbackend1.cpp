// Shows which PresentSurface a real RootView ended up with (presentsurface.h): GDI (BitBlt on WM_PAINT)
// or DXGI (a flip-model swap chain), and lets you flip between them at startup:
//
//   presentbackend1.exe            -> defaultPresentBackend(): NEWUI_PRESENT env var, else GDI
//   presentbackend1.exe gdi|dxgi   -> that backend (dxgi still falls back to GDI on a machine that can't do it)
//   set NEWUI_PRESENT=dxgi         -> same, without touching the command line
//
// The label reports what was actually chosen once the window exists - "DXGI" only if a swap chain really
// came up (also printed to stdout). A few controls are here so hover/press produce small partial repaints, i.e. the dirty-box
// upload path, not just full frames.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/presentsurface.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"

#include <cstdio>
#include <functional>
#include <memory>
#include <optional>
#include <string>

extern void registerReflectionData();

int main(int argc, char** argv) {
    registerReflectionData();

    if (argc > 1) {
        newui::PresentBackend requested;
        if (newui::parsePresentBackend(argv[1], requested)) {
            newui::setDefaultPresentBackend(requested);
        }
        else {
            printf("usage: presentbackend1 [gdi|dxgi]   (unrecognized: '%s')\n", argv[1]);
        }
    }

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("presentbackend1");
    app.setFrame(&frame);

    frame.setTitle("Present backend example");
    frame.setBounds(newui::Rect(10, 10, 360, 180));

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(10.0f);
    rootLayout->setPadding(16.0f);
    root.setLayout(std::move(rootLayout));

    auto* label = new newui::Label();
    label->setVisible(true);
    label->setText("Starting...");
    label->setDesiredSize(newui::Size(0.0f, 28.0f));
    root.addChild(label);

    for (const char* text : { "First", "Second", "Third" }) {
        auto* button = new newui::Button();
        button->setVisible(true);
        button->setText(text);
        button->setDesiredSize(newui::Size(0.0f, 28.0f));
        root.addChild(button);
    }

    // The swap chain only comes up once the window exists and has been sized, and a DXGI surface that
    // can't work here gives up (falling back to GDI) at that same point - so ask after the first frame, and
    // again after every later resize (a minimize/restore rebuilds the swap chain), printing on every change.
    auto lastReported = std::make_shared<std::optional<newui::PresentBackend>>();
    auto report = [label, &root, lastReported]() {
        const newui::PresentBackend now = root.presentBackend();
        if (*lastReported == now) {
            return;
        }
        *lastReported = now;
        const char* name = now == newui::PresentBackend::Dxgi ? "DXGI" : "GDI";
        printf("presentbackend1: presenting via %s\n", name);
        fflush(stdout);
        label->setText(std::string("Presenting via ") + name + " - hover the buttons.");
    };
    newui::RunLoop::current().post(report);
    root.onSizeChanged.add(std::function<newui::SyncReturn(newui::View&, const newui::Size&)>(
        [report](newui::View&, const newui::Size&) {
            newui::RunLoop::current().post(report);
            return newui::SyncReturn::Handled;
        }));

    app.run();
    return 0;
}

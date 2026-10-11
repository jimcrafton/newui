// A small look at newui::PopupTool/newui::Underlay (popuptool.h/underlay.h) -
// a real WS_POPUP + WS_EX_LAYERED, per-pixel-transparent, topmost popup
// whose shape comes from Blend2D and is pushed via UpdateLayeredWindow,
// instead of the normal WM_PAINT-driven RootView/SubView tree every other
// example in this directory uses. See popup-tool-plan.md (repo root) for
// the design background.
//
// Two buttons show the two ways to give one its shape - a rounded white
// card, like a small popup menu/template picker:
//   - "SVG Popup": Underlay::loadImageFile() rasterizes popup-card.svg
//     (examples/, a single rounded <rect>) at the popup's own size - the
//     SVG's own transparent background (everywhere outside that rect) is
//     what gives the popup its rounded-corner silhouette.
//   - "Shape Popup": a newui::CalloutTool (popuptool.h) instead of a plain
//     PopupTool - it builds its own shapes::CalloutRoundRect (shapes.h)
//     into the Underlay automatically, the same rounded card but with a
//     small triangular tail pointing back at the button that opened it.
// Both popups also get a real Label and a "Close" Button, added as
// ordinary SubView children (AddDemoControls() below) - PopupTool
// composites its own SubView tree over the Underlay's background (see
// PopupTool::present()'s own comment, popuptool.h), so these paint and
// take real clicks exactly like they would in any other RootView.
// Escape (real window-level input - the popup gets focus when shown), the
// Close button, or clicking its "*Popup" button again, closes whichever
// popup is open.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/popuptool.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/underlay.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cstdio>
#include <functional>
#include <string>

// Defined in the reflectgen-generated .cpp - see shapes1.cpp's own comment
// on this same forward declaration.
extern void registerReflectionData();

namespace {

    constexpr float kPopupWidth = 320.0f;
    constexpr float kPopupHeight = 180.0f;

    // No file-scope tracking pointer needed - PopupTool owns its own
    // post-dismiss lifetime (dismiss() destroy()s the real window and
    // deletes itself, see popuptool.h), and dismissOnFocusLost()'s
    // default of true already closes whatever popup was open as soon as
    // the user clicks a "*Popup" button (or anything else) on the main
    // window - that click moves focus away from the popup before its own
    // onClick even runs.
    //
    // Creates+initializes a fresh T (PopupTool, or a subclass of it - e.g.
    // CalloutTool, popuptool.h - the constructor shape is the same for
    // any of them) centered over ownerHwnd. Callers still need to fill in
    // underlay()'s actual content and call present() themselves.
    template<typename T>
    T* OpenPopupNear(HWND ownerHwnd) {
        RECT ownerRect{};
        ::GetWindowRect(ownerHwnd, &ownerRect);
        int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - int(kPopupWidth)) / 2;
        int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - int(kPopupHeight)) / 2;

        auto* popup = new T(ownerHwnd, newui::Application::instance().instanceHandle(),
            newui::Rect(float(x), float(y), kPopupWidth, kPopupHeight), "popupToolDemo");

        if (!popup->initialize()) {
            printf("PopupTool::initialize() failed.\n");
            delete popup;
            return nullptr;
        }

        return popup;
    }

    // Adds a real Label + "Close" Button as ordinary SubView children of
    // popup - painted via the normal RootView/SubView tree
    // (getImageBuffer(), now alpha-aware - see PopupTool::
    // imageBufferFormat()) and composited on top of whatever the Underlay
    // itself drew (PopupTool::present()'s own comment, popuptool.h).
    void AddDemoControls(newui::PopupTool& popup) {
        auto* label = new newui::Label();
        label->setVisible(true);
        label->setText("A real Label + Button, composited over the Underlay");
        label->setBounds(newui::Rect(24.0f, 24.0f, kPopupWidth - 48.0f, 36.0f));
        popup.addChild(label);

        auto* closeButton = new newui::Button();
        closeButton->setVisible(true);
        closeButton->setText("Close");
        closeButton->setBounds(newui::Rect(24.0f, kPopupHeight - 56.0f, 100.0f, 28.0f));
        // dismiss() is safe to call from here even though this click
        // handler runs from inside popup's own event dispatch (a child
        // SubView's onClick) - see dismiss()'s own doc comment, popuptool.h.
        closeButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
            [&popup](newui::Control&) -> newui::SyncReturn {
                popup.dismiss();
                return newui::SyncReturn::Handled;
            }));
        popup.addChild(closeButton);
    }

    // Version 1: the popup's shape comes straight from an SVG file -
    // Underlay::loadImageFile() rasterizes it at the popup's own size, and
    // its own transparent background becomes the popup's silhouette.
    void ShowSvgPopup(HWND ownerHwnd) {
        newui::PopupTool* popup = OpenPopupNear<newui::PopupTool>(ownerHwnd);
        if (popup == nullptr) {
            return;
        }

        std::string svgPath = newui::Bundle::instance().resourcePath("popup-card.svg");
        if (svgPath.empty() || !popup->underlay()->loadImageFile(svgPath)) {
            printf("Could not load popup-card.svg under Resources/.\n");
        }

        AddDemoControls(*popup);
        popup->present();
    }

    // Version 2: the same rounded-card look, but via newui::CalloutTool
    // (popuptool.h) instead of a plain PopupTool - it builds its own
    // shapes::CalloutRoundRect (shapes.h) into the Underlay, tail
    // pointing up out of the top edge, centered (tailPosition()'s default
    // of 0.5), toward the "Shape Popup" button that opened it.
    void ShowShapePopup(HWND ownerHwnd) {
        newui::CalloutTool* popup = OpenPopupNear<newui::CalloutTool>(ownerHwnd);
        if (popup == nullptr) {
            return;
        }

        AddDemoControls(*popup);
        popup->present();
    }

    newui::SyncReturn FrameClosed(newui::Frame& frame) {
        printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
        return newui::SyncReturn::Handled;
    }

}

int main() {
    printf("newui %s - PopupTool/Underlay example\n", newui::version());
    printf("Two buttons, two ways to shape the same real WS_POPUP + WS_EX_LAYERED popup:\n");
    printf("an SVG file (Underlay::loadImageFile()) or code (Underlay::shapeLayer()).\n");
    printf("Esc, or the button again, closes it.\n");

    registerReflectionData();

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("popuptool1");
    app.setFrame(&frame);

    frame.setTitle("PopupTool Example");
    frame.setBounds(newui::Rect(10, 10, 320, 140));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(10.0f);
    rootLayout->setPadding(16.0f);
    root.setLayout(std::move(rootLayout));

    auto* svgButton = new newui::Button();
    svgButton->setVisible(true);
    svgButton->setText("SVG Popup");
    svgButton->setDesiredSize(newui::Size(0.0f, 28.0f));
    svgButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&frame](newui::Control&) -> newui::SyncReturn {
            ShowSvgPopup(frame.frameHandle());
            return newui::SyncReturn::Handled;
        }));
    root.addChild(svgButton);

    auto* shapeButton = new newui::Button();
    shapeButton->setVisible(true);
    shapeButton->setText("Shape Popup");
    shapeButton->setDesiredSize(newui::Size(0.0f, 28.0f));
    shapeButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&frame](newui::Control&) -> newui::SyncReturn {
            ShowShapePopup(frame.frameHandle());
            return newui::SyncReturn::Handled;
        }));
    root.addChild(shapeButton);

    app.run();

    return 0;
}

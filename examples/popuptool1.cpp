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
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/popuptool.h"
#include "newui/rootview.h"
#include "newui/shapes.h"
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

    // Which edge CalloutRoundRect's tail points out of.
    enum class TailSide { Top, Bottom, Left, Right };

    // A rounded-rect "callout" card - like newui::shapes::RoundRect, but
    // with a small triangular tail poking out of one edge (the classic
    // popover/tooltip shape, pointing back at whatever triggered it).
    // Built as one continuous BLPath rather than a RoundRect plus a
    // separate triangle Shape - two overlapping shapes would each stroke
    // their own full outline, leaving a visible seam line where the tail
    // meets the body; one path has no such seam. Same rounded-corner
    // technique newui::shapes::buildPartiallyRoundedRectPath() (shapes.h)
    // uses (BLPath::arc_quadrant_to() per corner), just with the tail's
    // two extra line_to() calls spliced into whichever edge tailSide()
    // names, keeping the same clockwise winding the rest of the contour
    // already has (deviate outward, then back - never backtrack).
    class CalloutRoundRect : public newui::shapes::Shape {
    public:
        CalloutRoundRect() = default;

        float x() const { return x_; }
        void setX(float value) { x_ = value; }
        float y() const { return y_; }
        void setY(float value) { y_ = value; }
        float width() const { return width_; }
        void setWidth(float value) { width_ = value; }
        float height() const { return height_; }
        void setHeight(float value) { height_ = value; }
        float radius() const { return radius_; }
        void setRadius(float value) { radius_ = value; }

        // Width of the tail's base and how far it pokes out past its edge.
        float tailWidth() const { return tailWidth_; }
        void setTailWidth(float value) { tailWidth_ = value; }
        float tailHeight() const { return tailHeight_; }
        void setTailHeight(float value) { tailHeight_ = value; }

        TailSide tailSide() const { return tailSide_; }
        void setTailSide(TailSide value) { tailSide_ = value; }

        // Position of the tail's tip along tailSide()'s edge, as a 0-1
        // fraction of that edge's own length (0 = its start - the left
        // end for Top/Bottom, the top end for Left/Right; 1 = its end;
        // 0.5, the default, centers it). Not clamped - a value outside
        // 0-1 places the tip past the corresponding rounded corner,
        // which buildPath() doesn't account for.
        float tailPosition() const { return tailPosition_; }
        void setTailPosition(float value) { tailPosition_ = value; }

        newui::Rect localBounds() const override {
            float left = x_, top = y_, right = x_ + width_, bottom = y_ + height_;
            switch (tailSide_) {
                case TailSide::Top:    top -= tailHeight_; break;
                case TailSide::Bottom: bottom += tailHeight_; break;
                case TailSide::Left:   left -= tailHeight_; break;
                case TailSide::Right:  right += tailHeight_; break;
            }
            return newui::Rect(left, top, right - left, bottom - top);
        }

    protected:
        void buildPath(BLPath& path) const override {
            double x0 = double(x_);
            double y0 = double(y_);
            double x1 = double(x_ + width_);
            double y1 = double(y_ + height_);
            double rad = double(radius_);
            double half = double(tailWidth_) * 0.5;
            double tip = double(tailHeight_);
            double pos = (tailSide_ == TailSide::Top || tailSide_ == TailSide::Bottom)
                ? x0 + double(tailPosition_) * (x1 - x0)
                : y0 + double(tailPosition_) * (y1 - y0);

            // Top edge: x0+rad -> x1-rad, at y0.
            path.move_to(x0 + rad, y0);
            if (tailSide_ == TailSide::Top) {
                path.line_to(pos - half, y0);
                path.line_to(pos, y0 - tip);
                path.line_to(pos + half, y0);
            }
            path.line_to(x1 - rad, y0);
            path.arc_quadrant_to(BLPoint(x1, y0), BLPoint(x1, y0 + rad));

            // Right edge: y0+rad -> y1-rad, at x1.
            if (tailSide_ == TailSide::Right) {
                path.line_to(x1, pos - half);
                path.line_to(x1 + tip, pos);
                path.line_to(x1, pos + half);
            }
            path.line_to(x1, y1 - rad);
            path.arc_quadrant_to(BLPoint(x1, y1), BLPoint(x1 - rad, y1));

            // Bottom edge: x1-rad -> x0+rad, at y1 (right-to-left).
            if (tailSide_ == TailSide::Bottom) {
                path.line_to(pos + half, y1);
                path.line_to(pos, y1 + tip);
                path.line_to(pos - half, y1);
            }
            path.line_to(x0 + rad, y1);
            path.arc_quadrant_to(BLPoint(x0, y1), BLPoint(x0, y1 - rad));

            // Left edge: y1-rad -> y0+rad, at x0 (bottom-to-top).
            if (tailSide_ == TailSide::Left) {
                path.line_to(x0, pos + half);
                path.line_to(x0 - tip, pos);
                path.line_to(x0, pos - half);
            }
            path.line_to(x0, y0 + rad);
            path.arc_quadrant_to(BLPoint(x0, y0), BLPoint(x0 + rad, y0));

            path.close();
        }

    private:
        float x_ = 0.0f;
        float y_ = 0.0f;
        float width_ = 0.0f;
        float height_ = 0.0f;
        float radius_ = 12.0f;
        float tailWidth_ = 24.0f;
        float tailHeight_ = 14.0f;
        TailSide tailSide_ = TailSide::Top;
        float tailPosition_ = 0.5f;
    };

    // No file-scope tracking pointer needed - PopupTool owns its own
    // post-dismiss lifetime (dismiss() destroy()s the real window and
    // deletes itself, see popuptool.h), and dismissOnFocusLost()'s
    // default of true already closes whatever popup was open as soon as
    // the user clicks a "*Popup" button (or anything else) on the main
    // window - that click moves focus away from the popup before its own
    // onClick even runs.
    //
    // Creates+initializes a fresh PopupTool centered over ownerHwnd.
    // Callers still need to fill in underlay()'s actual content and call
    // present() themselves.
    newui::PopupTool* OpenPopupNear(HWND ownerHwnd) {
        RECT ownerRect{};
        ::GetWindowRect(ownerHwnd, &ownerRect);
        int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - int(kPopupWidth)) / 2;
        int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - int(kPopupHeight)) / 2;

        auto* popup = new newui::PopupTool(ownerHwnd, newui::Application::instance().instanceHandle(),
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
        newui::PopupTool* popup = OpenPopupNear(ownerHwnd);
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

    // Version 2: the same rounded-card look, but as a CalloutRoundRect
    // (defined above) instead of a plain newui::shapes::RoundRect - a
    // tail pointing up out of its top edge, centered (tailPosition()'s
    // default of 0.5), toward the "Shape Popup" button that opened it.
    // Inset from the canvas top by tailHeight() so the tail has room to
    // poke up without clipping against the underlay's own edge.
    void ShowShapePopup(HWND ownerHwnd) {
        newui::PopupTool* popup = OpenPopupNear(ownerHwnd);
        if (popup == nullptr) {
            return;
        }

        constexpr float kTailHeight = 14.0f;

        auto* card = new CalloutRoundRect();
        card->setX(2.0f);
        card->setY(2.0f + kTailHeight);
        card->setWidth(kPopupWidth - 4.0f);
        card->setHeight(kPopupHeight - 4.0f - kTailHeight);
        card->setRadius(12.0f);
        card->setTailSide(TailSide::Top);
        card->setTailWidth(24.0f);
        card->setTailHeight(kTailHeight);
        card->style().fill().setColor(newui::Color(1.0f, 1.0f, 1.0f, 1.0f));
        card->style().fill().setKind(newui::gfx::PaintKind::Color);
        card->style().stroke().setColor(newui::Color(0.78f, 0.78f, 0.78f, 1.0f));
        card->style().stroke().setKind(newui::gfx::PaintKind::Color);
        card->style().stroke().setWidth(1.5f);
        popup->underlay()->shapeLayer().addShape(card);

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

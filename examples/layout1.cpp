// A tour of newui::Layout: arranging SubView children automatically
// instead of hand-computing bounds() for each one. See layout.h for the
// three Layout subclasses this demonstrates:
//   - StackLayout arranges the root view's direct children in a
//     horizontal row - a fixed-width sidebar plus three flexible
//     content panels sharing the leftover width by weight.
//   - AnchorLayout, nested inside one of those content panels, pins a
//     small badge to its top-right corner - showing that a Layout
//     arranges whatever View it's attached to, not just the root, and
//     composes naturally with whatever layout the parent itself uses.
// Every panel is otherwise empty (just a background/border color) so
// the arrangement is the only thing on screen - resize the window to
// see StackLayout re-flow it live (SubView::setBounds()/
// RootView::setBounds() call View::updateLayout() automatically; see
// view.h).



#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>


#include "newui/newui.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/layout.h"
#include "newui/color.h"
#include <blend2d/blend2d.h>

#include <iostream>
#include <memory>

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    return newui::SyncReturn::Handled;
}

// Every panel in this demo is otherwise empty - a flat background fill
// plus a contrasting border is the only thing that makes the
// arrangement visible - so this is the one place colors are chosen,
// rather than repeating the same four lines at each call site.
newui::SubView* MakePanel(const std::string& name, const std::string& backgroundColorName,
        const std::string& borderColorName, float borderWidth = 3.0f) {
    auto* panel = new newui::SubView();
    panel->setName(name);
    panel->setVisible(true);
    panel->style().backgroundFill = newui::Color::fromName(backgroundColorName).toBLRgba32();
    panel->style().borderFill = newui::Color::fromName(borderColorName).toBLRgba32();
    panel->style().borderWidth = borderWidth;
    return panel;
}

int main() {

    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);



    std::cout << "newui " << newui::version() << " - layout example\n";
    std::cout << "Sidebar + 3 flexible panels (StackLayout), badge pinned via a nested AnchorLayout.\n";
    std::cout << "Resize the window to see StackLayout re-flow the row live.\n";

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("layout1");
    app.setFrame(&frame);

    frame.setTitle("Layout Example");
    frame.setBounds(newui::Rect(10, 10, 900, 500));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.getView();
    root.style().backgroundFill = newui::Color::fromName("white").toBLRgba32();

    // A horizontal row directly on the root view - spacing between
    // panels, padding from the window's own edges.
    auto stackLayout = std::make_unique<newui::StackLayout>(newui::Orientation::Horizontal);
    stackLayout->setSpacing(12.0f);
    stackLayout->setPadding(16.0f);
    root.setLayout(std::move(stackLayout));

    // Sidebar: fixed width (its "natural" size - see StackLayoutParams'
    // weight comment), full height via the default
    // CrossAxisAlignment::Stretch. Bounds have to be set before
    // addChild() - that's what triggers the first arrange() pass, and
    // StackLayout reads a weight-0 child's starting bounds as its
    // natural main-axis size.
    auto* sidebar = MakePanel("sidebar", "steelblue", "navy");
    sidebar->setBounds(newui::Rect(0, 0, 160, 0));
    root.addChild(sidebar);

    // Three flexible panels sharing the row's leftover width by weight
    // (CSS flex-grow) - content2 is twice as wide as content1/content3
    // since its weight is double theirs.
    auto* content1 = MakePanel("content1", "lightcoral", "darkred");
    content1->setLayoutParams(std::make_unique<newui::StackLayoutParams>(1.0f));
    root.addChild(content1);

    auto* content2 = MakePanel("content2", "khaki", "darkgoldenrod");
    content2->setLayoutParams(std::make_unique<newui::StackLayoutParams>(2.0f));
    root.addChild(content2);

    auto* content3 = MakePanel("content3", "mediumseagreen", "darkgreen");
    content3->setLayoutParams(std::make_unique<newui::StackLayoutParams>(1.0f));
    root.addChild(content3);

    // A small badge nested inside content1, pinned to its top-right
    // corner via a second, independent Layout - content1's own, not
    // the root's. AnchorLayoutParams' margins are relative to whichever
    // View the AnchorLayout is attached to, so this doesn't need to
    // know anything about content1's position within the row.
    content1->setLayout(std::make_unique<newui::AnchorLayout>());

    auto* badge = MakePanel("badge", "white", "darkred", 2.0f);
    auto badgeParams = std::make_unique<newui::AnchorLayoutParams>(newui::Anchor::Right | newui::Anchor::Top);
    badgeParams->rightMargin = 8.0f;
    badgeParams->topMargin = 8.0f;
    badgeParams->width = 24.0f;
    badgeParams->height = 24.0f;
    badge->setLayoutParams(std::move(badgeParams));
    content1->addChild(badge);

    app.run();

    return 0;
}

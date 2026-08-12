// A tour of newui::Layout: arranging SubView children automatically
// instead of hand-computing bounds() for each one. See layout.h for the
// Layout subclasses this demonstrates:
//   - FlexLayout arranges the root view's direct children in a
//     horizontal row - a fixed-width sidebar plus three flexible
//     content panels sharing the leftover width by weight.
//   - AnchorLayout, nested inside one of those content panels, pins a
//     small badge to its top-right corner - showing that a Layout
//     arranges whatever View it's attached to, not just the root, and
//     composes naturally with whatever layout the parent itself uses.
//   - GridLayout, nested inside a different content panel, lays out a
//     small 2-column "form" (an Auto-sized label column next to a
//     Star-sized input column) - see AddGridDemo() below.
//   - The sidebar hosts a native-themed button and checkbox
//     (ThemedButtonStyle/ThemedCheckBoxStyle - see viewstyle.h), stacked
//     via its own nested vertical FlexLayout, to show a themed style
//     composing with Layout the same way the hand-drawn styles do.
// Every panel is otherwise empty (just a background/border color) so
// the arrangement is the only thing on screen - resize the window to
// see FlexLayout re-flow it live (SubView::setBounds()/
// RootView::setBounds() call View::updateLayout() automatically; see
// view.h).



#include "newui/newui.h"
#include "newui/application.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/layout.h"
#include "newui/color.h"
#include "newui/serialization.h"
#include <blend2d/blend2d.h>

#include <iostream>
#include <memory>

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());

    //saveFrameToFile(frame, "frame.json");

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

// Builds a small 2-column "form" grid as parent's own Layout - column 0
// is Auto-sized to whichever label cell's desiredSize() is widest (see
// View::setDesiredSize()), column 1 is a single Star track filling
// whatever's left. Both cells default to CrossAxisAlignment::Stretch, so
// each label fills the *resolved* Auto column width, not just its own
// desiredSize() - label2 (below) is what actually drives that width.
void AddGridDemo(newui::SubView* parent) {
    auto grid = std::make_unique<newui::GridLayout>();
    grid->addAutoColumn();
    grid->addStarColumn();
    grid->addFixedRow(28.0f);
    grid->addFixedRow(28.0f);
    grid->setColumnSpacing(6.0f);
    grid->setRowSpacing(6.0f);
    parent->setLayout(std::move(grid));

    auto* label1 = MakePanel("label1", "gainsboro", "gray", 1.0f);
    label1->setDesiredSize(newui::Size(60.0f, 20.0f));
    label1->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 0));
    parent->addChild(label1);

    auto* input1 = MakePanel("input1", "white", "gray", 1.0f);
    input1->setLayoutParams(std::make_unique<newui::GridLayoutParams>(0, 1));
    parent->addChild(input1);

    // Wider than label1 - grows the Auto column to fit this one instead.
    auto* label2 = MakePanel("label2", "gainsboro", "gray", 1.0f);
    label2->setDesiredSize(newui::Size(90.0f, 20.0f));
    label2->setLayoutParams(std::make_unique<newui::GridLayoutParams>(1, 0));
    parent->addChild(label2);

    auto* input2 = MakePanel("input2", "white", "gray", 1.0f);
    input2->setLayoutParams(std::make_unique<newui::GridLayoutParams>(1, 1));
    parent->addChild(input2);
}

int main() {

    std::cout << "newui " << newui::version() << " - layout example\n";
    std::cout << "Sidebar + 3 flexible panels (FlexLayout), badge pinned via a nested AnchorLayout,\n";
    std::cout << "a small form grid (GridLayout) nested in the third panel,\n";
    std::cout << "a themed button + checkbox (uxtheme) stacked in the sidebar.\n";
    std::cout << "Resize the window to see FlexLayout re-flow the row live.\n";

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
    auto flexLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    flexLayout->setSpacing(12.0f);
    flexLayout->setPadding(16.0f);
    root.setLayout(std::move(flexLayout));

    // Sidebar: fixed width (its "natural" size - see FlexLayoutParams'
    // weight comment), full height via the default
    // CrossAxisAlignment::Stretch. Bounds have to be set before
    // addChild() - that's what triggers the first arrange() pass, and
    // FlexLayout reads a weight-0 child's starting desiredSize() (which
    // falls back to bounds size by default - see View::desiredSize()) as
    // its natural main-axis size.
    auto* sidebar = MakePanel("sidebar", "steelblue", "navy");
    sidebar->setBounds(newui::Rect(0, 0, 160, 0));
    root.addChild(sidebar);

    // A couple of real Win32 controls (drawn via uxtheme, not this
    // toolkit's own hand-drawn chrome) stacked in the sidebar via its own
    // nested vertical FlexLayout. Left-aligned rather than the default
    // Stretch - DrawThemeBackground() stretches a native part's artwork
    // to fill whatever rect it's given, and a BP_CHECKBOX/BP_PUSHBUTTON
    // part stretched to the sidebar's full width would look distorted -
    // so each child gets a fixed desiredSize() and stays natural-width.
    auto sidebarLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    sidebarLayout->setSpacing(8.0f);
    sidebarLayout->setPadding(12.0f);
    sidebar->setLayout(std::move(sidebarLayout));

    auto* themedButton = new newui::SubView();
    themedButton->setName("themedButton");
    themedButton->setVisible(true);
    themedButton->setStyle(std::make_unique<newui::ThemedButtonStyle>());
    themedButton->setDesiredSize(newui::Size(120.0f, 28.0f));
    auto themedButtonParams = std::make_unique<newui::FlexLayoutParams>();
    themedButtonParams->crossAxisAlignment = newui::CrossAxisAlignment::Start;
    themedButton->setLayoutParams(std::move(themedButtonParams));
    sidebar->addChild(themedButton);

    auto* themedCheckBox = new newui::SubView();
    themedCheckBox->setName("themedCheckBox");
    themedCheckBox->setVisible(true);
    auto themedCheckBoxStyle = std::make_unique<newui::ThemedCheckBoxStyle>();
    themedCheckBoxStyle->checked = true;
    themedCheckBox->setStyle(std::move(themedCheckBoxStyle));
    themedCheckBox->setDesiredSize(newui::Size(20.0f, 20.0f));
    auto themedCheckBoxParams = std::make_unique<newui::FlexLayoutParams>();
    themedCheckBoxParams->crossAxisAlignment = newui::CrossAxisAlignment::Start;
    themedCheckBox->setLayoutParams(std::move(themedCheckBoxParams));
    sidebar->addChild(themedCheckBox);

    // Three flexible panels sharing the row's leftover width by weight
    // (CSS flex-grow) - content2 is twice as wide as content1/content3
    // since its weight is double theirs.
    auto* content1 = MakePanel("content1", "lightcoral", "darkred");
    content1->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    root.addChild(content1);

    auto* content2 = MakePanel("content2", "khaki", "darkgoldenrod");
    content2->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(2.0f));
    root.addChild(content2);

    auto* content3 = MakePanel("content3", "mediumseagreen", "darkgreen");
    content3->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
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

    // A small form grid nested inside content3 - see AddGridDemo() above.
    AddGridDemo(content3);

    app.run();

    return 0;
}

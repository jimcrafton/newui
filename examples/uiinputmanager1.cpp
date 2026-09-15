// uiinputmanager1 - an interactive tour of everything newui::UIInputManager
// (uiinputmanager.h) does: which View a mouse click actually focuses,
// Tab/Shift+Tab navigation in reading order, the focus ring
// (ViewStyle::paintFocusRing()) and the native ETS_FOCUSED border
// (ThemedEditStyle::focused), focus scopes that trap Tab inside a panel
// (View::isFocusScope()), a FocusGuide redirecting Tab to an out-of-order
// destination, keyboard navigation inside ListView/TreeView (Up/Down/Home/
// End/Shift/Ctrl+Arrow/Ctrl+Space, Left/Right for the tree), TabControl's
// own Left/Right (or Up/Down) tab switching, a cross-control spatial
// arrow-jump once Up/Down/Left/Right runs out of somewhere local to go, and
// focus recovering to a surviving ancestor rather than vanishing when the
// focused View is destroyed out from under it, and a preview of the
// custom-drawn Fluent-style ViewStyle subclasses (checkbox/radio/toolbar
// button/text field/an elevated Card/a slider/a progress bar/a GroupBox
// frame) alongside their native-look counterparts used everywhere else
// in this file.
//
// Eleven labeled sections, top to bottom - try each with Tab, Shift+Tab,
// and the arrow keys:
//   1. A plain row of controls (including one disabled Button) - proves
//      basic geometric Tab order, the visible focus ring, and that a
//      disabled control is skipped entirely.
//   2. A bordered "Panel" with isFocusScope(true) - Tab from outside lands
//      on the panel itself as a single stop; Tab again enters it, and
//      further Tab/Shift+Tab only ever cycles among its own three
//      TextFields (each showing the real native ETS_FOCUSED border, not
//      the generic dashed ring - see ThemedEditStyle::paintFocusRing()).
//   3. Four buttons A/B/C/D - a FocusGuide sits between A and B, so Tab
//      from A jumps straight to D, skipping B and C.
//   4. A ListView (StringListModel, same shape as mvc1.cpp's) - click a
//      row, then try the arrow keys.
//   5. A TreeView (StringTreeModel) - same as above, plus Left/Right to
//      collapse/expand or move to the parent/first child.
//   6. A TabControl - Tab into the strip, then Left/Right switches tabs
//      (TabControl::handleKeyDown(), tabcontrol.cpp), wrapping at both
//      ends.
//   7. A 2x2 grid of plain Buttons, none of which hook arrow keys
//      themselves - Up/Down/Left/Right jumps to whichever neighbor is
//      actually positioned in that direction (edge-filtered, nearest by
//      center distance - a diagonal neighbor is never a valid target no
//      matter how close), via UIInputManager::moveFocusSpatially().
//   8. "Focus Me" inside a bordered, focusable panel, plus a "Destroy It"
//      button - focus it, then destroy it, and watch focus recover to the
//      panel (RootView::notifySubViewRemoved()) instead of disappearing.
//   9. A row of Fluent-style previews: a checkbox, a radio button, a
//      toggled-on toolbar button, a text field (with its own focused-state
//      accent underline), and a "Card" panel with a real Card-level drop
//      shadow (FluentCardStyle's default ElevationLevel::Card).
//  10. A Fluent-style Slider (a filled circular thumb on a thin rounded
//      groove - drag it) and a Fluent-style Progress bar (a rounded pill,
//      fixed at 60%).
//  11. A native GroupBox and a Fluent one (a plain rounded-stroke frame,
//      no fill), each grouping two checkboxes of the matching look.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/controls.h"
#include "newui/fontmanager.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/models.h"
#include "newui/reflection.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/tabcontrol.h"
#include "newui/uicolormanager.h"
#include "newui/uiinputmanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <any>
#include <cstdio>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

// See mvc1.cpp's own doc comment on this - reflectgen-generated,
// compiled into newui.lib, but still needs calling once at startup so
// ListController::createItem()/TreeController::createItem() can actually
// resolve "ListItem"/"TreeItem" by name.
extern void registerReflectionData();

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    return newui::SyncReturn::Handled;
}

// Same minimal real Model mvc1.cpp already established - see its own doc
// comment for why value() hands back std::string, not std::wstring.
class StringListModel : public newui::ListModel {
public:
    std::vector<std::string> rows;

    std::any value(const std::any& key) override {
        if (const std::size_t* index = std::any_cast<std::size_t>(&key)) {
            if (*index < rows.size()) {
                return rows[*index];
            }
        }
        return std::any();
    }

    std::size_t size() const override {
        return rows.size();
    }
};

// Same StringTreeModel shape as mvc1.cpp - see its own doc comment.
class StringTreeModel : public newui::TreeModel {
public:
    std::map<std::vector<std::size_t>, std::vector<std::string>> childrenByParent;

    std::size_t childCount(const std::vector<std::size_t>& path) const override {
        auto it = childrenByParent.find(path);
        return it != childrenByParent.end() ? it->second.size() : 0;
    }

    std::any value(const std::any& key) override {
        if (const std::vector<std::size_t>* path = std::any_cast<std::vector<std::size_t>>(&key)) {
            if (!path->empty()) {
                std::vector<std::size_t> parentPath(path->begin(), path->end() - 1);
                std::size_t lastIndex = path->back();
                auto it = childrenByParent.find(parentPath);
                if (it != childrenByParent.end() && lastIndex < it->second.size()) {
                    return it->second[lastIndex];
                }
            }
        }
        return std::any();
    }
};

// A short caption Label - see controls1.cpp's own MakeLabel() for why
// UIColorManager::colorFor() (not a hardcoded color) for both text and
// background.
newui::SubView* MakeLabel(const std::string& text, bool bold = false) {
    auto* label = new newui::SubView();
    label->setVisible(true);
    auto style = std::make_unique<newui::LabelStyle>();
    style->setText(text);
    style->setTextColor(newui::UIColorManager::colorFor(
        bold ? newui::UIColorRole::WindowText : newui::UIColorRole::WindowText));
    style->setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
    label->setStyle(std::move(style));
    label->setDesiredSize(newui::Size(0.0f, bold ? 22.0f : 18.0f));
    return label;
}

// A section header: bold-ish caption above a short instruction line -
// both just plain Labels (LabelStyle has no real bold flag), added
// straight to root so they stack in the vertical FlexLayout.
void AddSectionHeader(newui::View* root, const std::string& title, const std::string& instructions) {
    root->addChild(MakeLabel(title, /*bold=*/true));
    root->addChild(MakeLabel(instructions));
}

int main() {
    registerReflectionData();

    std::cout << "newui " << newui::version() << " - UIInputManager example\n";
    std::cout << "Try Tab / Shift+Tab throughout. Arrow keys work once you click into the\n";
    std::cout << "ListView/TreeView near the bottom. See the window itself for section-by-\n";
    std::cout << "section instructions.\n";

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("uiinputmanager1");
    app.setFrame(&frame);

    frame.setTitle("UIInputManager Example");
    frame.setBounds(newui::Rect(10, 10, 560, 900));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
    root.setLayout(std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical));

    // All eight sections stack inside contentContainer, not root directly -
    // eight sections is taller than most screens can show at once, and root
    // itself has no scrolling of its own (unlike each ListView/TreeView
    // below, individually hosted in its own ScrollView). contentContainer
    // never answers onQueryContentSize (it's a plain SubView, not a
    // virtualized ListView/TreeView/TextControl - see ScrollView::
    // virtualizedContentChild(), controls.cpp), so nothing computes its
    // natural height automatically - setBounds()/setContentSize() below set
    // it by hand instead, the same manual pattern themes1.cpp's own
    // "content4" ScrollView demo uses for a plain container of manually-
    // sized children.
    auto* outerScrollView = new newui::ScrollView();
    outerScrollView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    root.addChild(outerScrollView);

    auto* contentContainer = new newui::SubView();
    contentContainer->setVisible(true);
    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(8.0f);
    rootLayout->setPadding(16.0f);
    contentContainer->setLayout(std::move(rootLayout));
    // A generous fixed size comfortably covering all eleven sections at
    // this window's width - not pixel-measured against their real desired
    // sizes (nothing here computes that automatically for a plain
    // container, see above), just large enough nothing is ever clipped.
    contentContainer->setBounds(newui::Rect(0.0f, 0.0f, 520.0f, 1850.0f));
    outerScrollView->addChild(contentContainer);
    outerScrollView->setContentSize(newui::Size(520.0f, 1850.0f));

    // -----------------------------------------------------------------
    // 1) Plain Tab order - a Button, a TextField, a disabled Button (must
    // be skipped entirely, even though Button::Button() sets
    // acceptsFocus(true) - Control::canBecomeFocused() also requires
    // isEnabled()), and a Toggle. Watch the dashed focus ring
    // (ViewStyle::paintFocusRing()) move between them, and the real
    // native ETS_FOCUSED border appear/disappear on the TextField.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "1) Plain Tab order",
        "Tab/Shift+Tab through: Button One, a TextField, a DISABLED Button (skipped), a Toggle.");

    auto* row1 = new newui::SubView();
    row1->setVisible(true);
    // Height grown by 2*kRingRoomPadding beyond the buttons' own 28px, and
    // that same amount as real padding on all four sides - proves the new
    // 3-phase paint split (View::paintChildren(), view.cpp) actually works:
    // ViewStyle::postPaint()'s default focus ring (viewstyle.cpp) now
    // paints in its own unclipped-at-the-child's-own-level phase, but a
    // *tight* ancestor (zero slack, the way row1 used to be sized) still
    // clips it via that ancestor's own phase-2 clip - ctx.save()/restore()
    // can only skip adding a *further* clip at a child's own level, it can
    // never undo a still-active clip an ancestor already established
    // (BLContext::clip_to_rect(), like virtually every 2D vector API, only
    // ever intersects/narrows, never widens back out). Confirmed live:
    // with zero slack, "One"'s ring only showed on the one side (right)
    // where an inter-child FlexLayout gap happened to leave room; giving
    // row1 itself real padding is what lets it show on all four.
    constexpr float kRingRoomPadding = 4.0f;
    row1->setDesiredSize(newui::Size(0.0f, 28.0f + kRingRoomPadding * 2.0f));
    auto row1Layout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    row1Layout->setSpacing(10.0f);
    row1Layout->setPadding(kRingRoomPadding);
    row1->setLayout(std::move(row1Layout));
    contentContainer->addChild(row1);

    auto* buttonOne = new newui::Button();
    buttonOne->setText("One");
    buttonOne->setDesiredSize(newui::Size(80.0f, 28.0f));
    // Experimental Fluent-style rounded chrome (viewstyle.h) instead of
    // the default native ThemedButtonStyle - see its own class comment
    // for why (DrawThemeBackground still renders the classic square-
    // cornered look, confirmed live, even on Windows 11). Only this one
    // button previews it; every other Button/ToolbarButton in this file
    // is untouched, still the native look, for a direct side-by-side
    // comparison ("Disabled" right next to it, e.g.).
    //
    // setFont() before setStyle(), same as Button::Button()'s own
    // constructor does for the default ThemedButtonStyle it builds
    // internally - a freshly constructed style otherwise has no font at
    // all, which Button::paint() treats as "unresolved" and silently
    // skips drawing the text for (real bug caught live: the button
    // rendered with correct rounded chrome but completely blank, no
    // "One" text at all, until this was added).
    auto fluentButtonOneStyle = std::make_unique<newui::FluentButtonStyle>();
    fluentButtonOneStyle->setFont(newui::FontManager::getSystemFont(newui::SystemUIFont::Message));
    buttonOne->setStyle(std::move(fluentButtonOneStyle));
    row1->addChild(buttonOne);

    auto* fieldOne = new newui::TextField();
    fieldOne->setText(L"Click or Tab here");
    fieldOne->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    row1->addChild(fieldOne);

    auto* disabledButton = new newui::Button();
    disabledButton->setText("Disabled");
    disabledButton->setDesiredSize(newui::Size(90.0f, 28.0f));
    disabledButton->setEnabled(false);
    row1->addChild(disabledButton);

    auto* toggleOne = new newui::Toggle();
    toggleOne->setDesiredSize(newui::Size(16.0f, 16.0f));
    row1->addChild(toggleOne);

    // -----------------------------------------------------------------
    // 2) Focus scope panel - bordered so it reads as a real panel. Three
    // TextFields inside; the panel itself opts into acceptsFocus() too so
    // it can act as its own "door" from outside (see
    // UIInputManager::moveFocus()'s own doc comment on why a non-
    // focusable scope can't work as one).
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "2) Focus scope",
        "Tab into the bordered panel below - further Tab/Shift+Tab stays trapped inside it.");

    auto* panel = new newui::SubView();
    
    panel->style().setElevation(3);

    panel->setVisible(true);
    panel->setAcceptsFocus(true);
    panel->setFocusScope(true);
    panel->style().setBorderFill(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder));
    panel->style().setBorderWidth(2.0f);
    panel->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
    panel->setDesiredSize(newui::Size(0.0f, 44.0f));
    auto panelLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    panelLayout->setSpacing(10.0f);
    panelLayout->setPadding(8.0f);
    panel->setLayout(std::move(panelLayout));
    contentContainer->addChild(panel);

    for (int i = 1; i <= 3; ++i) {
        auto* field = new newui::TextField();
        field->setText(L"Panel field " + std::to_wstring(i));
        field->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        panel->addChild(field);
    }

    auto* afterPanelButton = new newui::Button();
    afterPanelButton->setText("After panel");
    afterPanelButton->setDesiredSize(newui::Size(120.0f, 28.0f));
    contentContainer->addChild(afterPanelButton);

    // -----------------------------------------------------------------
    // 3) FocusGuide redirect - A, B, C, D laid out explicitly (no Layout
    // on this row) so the guide's own position between A and B is exact
    // and unambiguous, not left to FlexLayout spacing/tie-break rules.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "3) FocusGuide redirect",
        "Tab from 'A' jumps straight to 'D' - the guide between A and B skips B and C.");

    auto* guideRow = new newui::SubView();
    guideRow->setVisible(true);
    guideRow->setDesiredSize(newui::Size(0.0f, 28.0f));
    contentContainer->addChild(guideRow);

    const float kGuideButtonWidth = 90.0f;
    const float kGuideButtonGap = 20.0f;

    auto* buttonA = new newui::Button();
    buttonA->setText("A");
    buttonA->setBounds(newui::Rect(0.0f, 0.0f, kGuideButtonWidth, 28.0f));
    guideRow->addChild(buttonA);

    auto* buttonB = new newui::Button();
    buttonB->setText("B");
    buttonB->setBounds(newui::Rect(kGuideButtonWidth + kGuideButtonGap, 0.0f, kGuideButtonWidth, 28.0f));
    guideRow->addChild(buttonB);

    auto* buttonC = new newui::Button();
    buttonC->setText("C");
    buttonC->setBounds(newui::Rect(2.0f * (kGuideButtonWidth + kGuideButtonGap), 0.0f, kGuideButtonWidth, 28.0f));
    guideRow->addChild(buttonC);

    auto* buttonD = new newui::Button();
    buttonD->setText("D");
    buttonD->setBounds(newui::Rect(3.0f * (kGuideButtonWidth + kGuideButtonGap), 0.0f, kGuideButtonWidth, 28.0f));
    guideRow->addChild(buttonD);

    // Positioned between A and B in reading order (setBounds() forces its
    // size to (0,0) regardless of what's passed here - only pos() drives
    // where it sorts into the Tab sequence).
    auto* guide = new newui::FocusGuide();
    guide->setBounds(newui::Rect(kGuideButtonWidth + kGuideButtonGap * 0.5f, 0.0f, 0.0f, 0.0f));
    guide->setRedirectTarget(buttonD);
    guideRow->addChild(guide);

    // -----------------------------------------------------------------
    // 4) ListView keyboard navigation.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "4) ListView keyboard navigation",
        "Click a row, then Up/Down/Home/End move it; Shift extends; Ctrl+Arrow+Ctrl+Space multi-selects.");

    StringListModel listModel;
    for (int i = 0; i < 20; ++i) {
        listModel.rows.push_back("Row " + std::to_string(i));
    }

    auto* listScrollView = new newui::ScrollView();
    listScrollView->setDesiredSize(newui::Size(0.0f, 150.0f));
    contentContainer->addChild(listScrollView);

    auto* listView = new newui::ListView();
    listView->setModel(&listModel);
    listScrollView->addChild(listView);

    auto* listSelectionLabel = new newui::Label();
    listSelectionLabel->setText("(nothing selected)");
    listSelectionLabel->setDesiredSize(newui::Size(0.0f, 22.0f));
    contentContainer->addChild(listSelectionLabel);

    listView->onSelectionChanged.add([listSelectionLabel](newui::ListView& sender) {
        if (sender.selectedIndices().empty()) {
            listSelectionLabel->setText("(nothing selected)");
            return newui::SyncReturn::Handled;
        }
        std::string text = "Selected: ";
        bool first = true;
        for (std::size_t index : sender.selectedIndices()) {
            std::any value = sender.model()->value(index);
            if (const std::string* rowText = std::any_cast<std::string>(&value)) {
                if (!first) {
                    text += ", ";
                }
                text += *rowText;
                first = false;
            }
        }
        listSelectionLabel->setText(text);
        return newui::SyncReturn::Handled;
    });

    // -----------------------------------------------------------------
    // 5) TreeView keyboard navigation.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "5) TreeView keyboard navigation",
        "Same as above, plus Left/Right to collapse/expand or move to the parent/first child.");

    StringTreeModel treeModel;
    treeModel.childrenByParent[{}] = { "Fruits", "Vegetables" };
    treeModel.childrenByParent[{ 0u }] = { "Apple", "Banana", "Cherry" };
    treeModel.childrenByParent[{ 1u }] = { "Carrot", "Potato" };

    auto* treeScrollView = new newui::ScrollView();
    treeScrollView->setDesiredSize(newui::Size(0.0f, 150.0f));
    contentContainer->addChild(treeScrollView);

    auto* treeView = new newui::TreeView();
    treeView->setModel(&treeModel);
    treeScrollView->addChild(treeView);

    auto* treeSelectionLabel = new newui::Label();
    treeSelectionLabel->setText("(nothing selected)");
    treeSelectionLabel->setDesiredSize(newui::Size(0.0f, 22.0f));
    contentContainer->addChild(treeSelectionLabel);

    treeView->onSelectionChanged.add([treeSelectionLabel](newui::TreeView& sender) {
        if (sender.selectedPaths().empty()) {
            treeSelectionLabel->setText("(nothing selected)");
            return newui::SyncReturn::Handled;
        }
        std::string text = "Selected: ";
        bool first = true;
        for (const std::vector<std::size_t>& path : sender.selectedPaths()) {
            std::any value = sender.model()->value(path);
            if (const std::string* label = std::any_cast<std::string>(&value)) {
                if (!first) {
                    text += ", ";
                }
                text += *label;
                first = false;
            }
        }
        treeSelectionLabel->setText(text);
        return newui::SyncReturn::Handled;
    });

    // -----------------------------------------------------------------
    // 6) TabControl arrow-key switching.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "6) TabControl arrow-key switching",
        "Tab into the strip below, then Left/Right switches tabs (wraps at both ends).");

    auto* tabs = new newui::TabControl(newui::ThemedTabItemStyle::TabAlignment::Top);
    tabs->setDesiredSize(newui::Size(0.0f, 90.0f));
    contentContainer->addChild(tabs);

    for (const char* tabLabel : { "Alpha", "Beta", "Gamma" }) {
        tabs->addTab(tabLabel, MakeLabel(std::string("Page: ") + tabLabel));
    }

    // -----------------------------------------------------------------
    // 7) Cross-control spatial arrow-jump - a 2x2 grid of plain Buttons,
    // none of which hook arrow keys themselves, so any arrow RootView::
    // keyEvent() sees for one of them is Ignored and falls back to
    // UIInputManager::routeArrowKeyDown()'s spatial jump.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "7) Cross-control spatial arrow-jump",
        "Click a corner button, then arrow keys jump to whichever neighbor is actually in that direction.");

    auto* gridContainer = new newui::SubView();
    gridContainer->setVisible(true);
    gridContainer->setDesiredSize(newui::Size(0.0f, 66.0f));
    contentContainer->addChild(gridContainer);

    const float kGridButtonWidth = 110.0f;
    const float kGridButtonHeight = 28.0f;
    const float kGridGap = 10.0f;

    auto* gridTopLeft = new newui::Button();
    gridTopLeft->setText("Top-Left");
    gridTopLeft->setBounds(newui::Rect(0.0f, 0.0f, kGridButtonWidth, kGridButtonHeight));
    gridContainer->addChild(gridTopLeft);

    auto* gridTopRight = new newui::Button();
    gridTopRight->setText("Top-Right");
    gridTopRight->setBounds(newui::Rect(kGridButtonWidth + kGridGap, 0.0f, kGridButtonWidth, kGridButtonHeight));
    gridContainer->addChild(gridTopRight);

    auto* gridBottomLeft = new newui::Button();
    gridBottomLeft->setText("Bottom-Left");
    gridBottomLeft->setBounds(newui::Rect(0.0f, kGridButtonHeight + kGridGap, kGridButtonWidth, kGridButtonHeight));
    gridContainer->addChild(gridBottomLeft);

    auto* gridBottomRight = new newui::Button();
    gridBottomRight->setText("Bottom-Right");
    gridBottomRight->setBounds(newui::Rect(kGridButtonWidth + kGridGap, kGridButtonHeight + kGridGap, kGridButtonWidth, kGridButtonHeight));
    gridContainer->addChild(gridBottomRight);

    // -----------------------------------------------------------------
    // 8) Focus recovery on destroy - "Destroy It" tears down "Focus Me"
    // (SubView::destroy(), which always removes itself from its parent
    // first, cascading into RootView::notifySubViewRemoved()) while it's
    // the focused View; watch the status line, and the focus ring, land
    // on the bordered panel instead of nothing.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "8) Focus recovery on destroy",
        "Click 'Focus Me', then 'Destroy It' - focus recovers to the bordered panel instead of vanishing.");

    auto* recoveryPanel = new newui::SubView();
    recoveryPanel->setVisible(true);
    recoveryPanel->setName("Recovery panel");
    recoveryPanel->setAcceptsFocus(true);
    recoveryPanel->style().setBorderFill(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBorder));
    recoveryPanel->style().setBorderWidth(2.0f);
    recoveryPanel->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
    recoveryPanel->setDesiredSize(newui::Size(0.0f, 44.0f));
    auto recoveryPanelLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    recoveryPanelLayout->setSpacing(10.0f);
    recoveryPanelLayout->setPadding(8.0f);
    recoveryPanel->setLayout(std::move(recoveryPanelLayout));
    contentContainer->addChild(recoveryPanel);

    auto* focusMeButton = new newui::Button();
    focusMeButton->setText("Focus Me");
    focusMeButton->setDesiredSize(newui::Size(100.0f, 28.0f));
    recoveryPanel->addChild(focusMeButton);

    auto* recoveryStatusRow = new newui::SubView();
    recoveryStatusRow->setVisible(true);
    recoveryStatusRow->setDesiredSize(newui::Size(0.0f, 28.0f));
    auto recoveryStatusRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    recoveryStatusRowLayout->setSpacing(10.0f);
    recoveryStatusRow->setLayout(std::move(recoveryStatusRowLayout));
    contentContainer->addChild(recoveryStatusRow);

    auto* destroyItButton = new newui::Button();
    destroyItButton->setText("Destroy It");
    destroyItButton->setDesiredSize(newui::Size(100.0f, 28.0f));
    recoveryStatusRow->addChild(destroyItButton);

    auto* recoveryStatusLabel = new newui::Label();
    recoveryStatusLabel->setText("(click Focus Me, then Destroy It)");
    recoveryStatusLabel->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    recoveryStatusRow->addChild(recoveryStatusLabel);

    destroyItButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&root, focusMeButton, recoveryStatusLabel](newui::Control&) mutable -> newui::SyncReturn {
            if (focusMeButton == nullptr) {
                return newui::SyncReturn::Handled;
            }
            bool wasFocused = root.focusedSubView() == focusMeButton;

            // destroy() always removes itself from its own parent first
            // (SubView::destroy(), subview.cpp) - no manual removeChild()
            // needed here, same as every unit test in this codebase that
            // tears a SubView down.
            focusMeButton->destroy();
            delete focusMeButton;
            focusMeButton = nullptr;

            newui::SubView* focused = root.focusedSubView();
            std::string text;
            if (!wasFocused) {
                text = "Destroyed (it wasn't focused, so nothing to recover)";
            } else if (focused != nullptr) {
                text = "Destroyed while focused - focus recovered to '" + focused->name() + "'";
            } else {
                text = "Destroyed while focused - focus cleared (no focusable ancestor)";
            }
            recoveryStatusLabel->setText(text);
            return newui::SyncReturn::Handled;
        }));

    // -----------------------------------------------------------------
    // 9) Fluent-style previews - the newer custom-drawn ViewStyle
    // subclasses (viewstyle.h): FluentCheckBoxStyle/FluentRadioButtonStyle
    // on a Toggle, FluentToolbarButtonStyle on a ToolbarButton,
    // FluentEditStyle on a TextField, and a FluentCardStyle panel with its
    // own Card-level drop shadow (ElevationLevel::Card) painted for free.
    // Everything else in this file stays the native look, same "one
    // preview, direct side-by-side comparison" approach Button One's own
    // FluentButtonStyle already established in section 1 above.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "9) Fluent-style previews",
        "Custom-drawn Fluent chrome: checkbox, radio, a toolbar button, a text field, and an elevated Card.");

    auto* fluentRow = new newui::SubView();
    fluentRow->setVisible(true);
    // Same "give the row itself real slack" reasoning as row1's own
    // comment (section 1, above) - there it was buttonOne's focus ring
    // needing room in row1's own padding; here it's the Card's drop
    // shadow needing room in fluentRow's, but the mechanism (an
    // ancestor's own tight phase-2 clip capping every descendant's
    // unclipped phase-1/3 room - see row1's comment for the full
    // explanation) is identical. 48px comfortably covers a Card-level
    // elevation shadow's own pad (ElevationLevel::Card = 8 -> roughly
    // 30-40px, see elevationBlurRadius()/elevationShadowOffsetMagnitude(),
    // viewstyle.cpp) with room to spare.
    constexpr float kFluentRowPadding = 48.0f;
    fluentRow->setDesiredSize(newui::Size(0.0f, 36.0f + kFluentRowPadding * 2.0f));
    auto fluentRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    fluentRowLayout->setSpacing(16.0f);
    fluentRowLayout->setPadding(kFluentRowPadding);
    fluentRowLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Center);
    fluentRow->setLayout(std::move(fluentRowLayout));
    contentContainer->addChild(fluentRow);

    auto* fluentCheckbox = new newui::Toggle();
    fluentCheckbox->setDesiredSize(newui::Size(20.0f, 20.0f));
    fluentCheckbox->setStyle(std::make_unique<newui::FluentCheckBoxStyle>());
    fluentCheckbox->setChecked(true);
    fluentRow->addChild(fluentCheckbox);

    auto* fluentRadio = new newui::Toggle();
    // setRadioStyle() itself calls Toggle::rebuildStyle(), which replaces
    // style() with a fresh native ThemedRadioButtonStyle - it has to run
    // *before* setStyle() below, or it would immediately clobber the
    // Fluent style just installed.
    fluentRadio->setRadioStyle(true);
    fluentRadio->setDesiredSize(newui::Size(20.0f, 20.0f));
    fluentRadio->setStyle(std::make_unique<newui::FluentRadioButtonStyle>());
    fluentRadio->setChecked(true);
    fluentRow->addChild(fluentRadio);

    auto* fluentToolbarButton = new newui::ToolbarButton();
    fluentToolbarButton->setText("Bold");
    fluentToolbarButton->setDesiredSize(newui::Size(60.0f, 28.0f));
    auto fluentToolbarButtonStyle = std::make_unique<newui::FluentToolbarButtonStyle>();
    // Same "a freshly constructed style has no font" gotcha row1's own
    // comment describes for buttonOne - ToolbarButton::paint() silently
    // skips drawing text with an unresolved font.
    fluentToolbarButtonStyle->setFont(newui::FontManager::getSystemFont(newui::SystemUIFont::Message));
    fluentToolbarButton->setStyle(std::move(fluentToolbarButtonStyle));
    // setToggleButton()/setChecked() have to run *after* setStyle() above,
    // not before - setChecked() (via updatePressedVisual()) writes
    // `checked` onto whatever style() is installed *right now*; called
    // any earlier, it would land on the constructor's own default
    // ThemedToolbarButtonStyle and then get silently discarded the
    // instant setStyle() replaces it (confirmed live: the accent tint
    // that setChecked(true) should produce never showed up until this
    // was reordered - same "swap the style first" ordering
    // fluentRadio's own comment above already gets right).
    fluentToolbarButton->setToggleButton(true);
    fluentToolbarButton->setChecked(true);
    fluentRow->addChild(fluentToolbarButton);

    auto* fluentField = new newui::TextField();
    fluentField->setText(L"Fluent field");
    // fluentRowLayout uses CrossAxisAlignment::Center (above), not the
    // vertical FlexLayout default's implicit stretch row1 relies on for
    // fieldOne (section 1) - a child here sizes to its own desiredSize()
    // on the cross axis instead of automatically filling the row's
    // height, so this needs a real height or it collapses to 0 and
    // never paints at all (confirmed live: with only the width-stretch
    // FlexLayoutParams below and no desiredSize, the field was
    // completely invisible - zero-height, not just zero-content).
    fluentField->setDesiredSize(newui::Size(0.0f, 32.0f));
    fluentField->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    fluentField->setStyle(std::make_unique<newui::FluentEditStyle>());
    fluentRow->addChild(fluentField);

    auto* fluentCard = new newui::SubView();
    fluentCard->setVisible(true);
    fluentCard->setName("Fluent card");
    // FluentCardStyle's own constructor already sets a sensible corner
    // radius and ElevationLevel::Card - nothing further to configure for
    // a working preview.
    fluentCard->setStyle(std::make_unique<newui::FluentCardStyle>());
    fluentCard->setDesiredSize(newui::Size(90.0f, 36.0f));
    auto fluentCardLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    fluentCardLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Center);
    fluentCard->setLayout(std::move(fluentCardLayout));
    fluentRow->addChild(fluentCard);

    // A real Label with a main-axis stretch factor, not MakeLabel() -
    // MakeLabel() sets desiredSize width 0.0f, tuned for a *vertical*
    // FlexLayout (contentContainer's own) where width is the cross axis
    // and defaults to stretch; here, inside fluentCard's *horizontal*
    // FlexLayout, width is the main axis, so a 0-width desiredSize with
    // no stretch factor rendered as genuinely zero-width - confirmed
    // live, "Card" never appeared. FlexLayoutParams(1.0f) is the same
    // "stretch to fill the main axis" fix recoveryStatusLabel (section
    // 8, above) already uses in an identical horizontal-row position.
    auto* fluentCardLabel = new newui::Label();
    fluentCardLabel->setText("Card");
    // fluentCardLayout's own CrossAxisAlignment::Center (above) means a
    // child sizes to its own desiredSize() on the cross axis rather than
    // stretching to fill it, same gotcha fluentField's own comment above
    // already covers for the row one level up - without this, the label
    // collapsed to 0 height and never painted (confirmed live: "Card"
    // never appeared, even after fluentCardLabel's width-stretch alone
    // was already fixed).
    fluentCardLabel->setDesiredSize(newui::Size(0.0f, 18.0f));
    fluentCardLabel->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    fluentCard->addChild(fluentCardLabel);

    // -----------------------------------------------------------------
    // 10) Fluent Slider/Progress previews - FluentTrackbarTrackStyle/
    // FluentTrackbarThumbStyle on the Slider (style() and thumb() each
    // opted in separately, same two-piece shape as everywhere else this
    // pair is styled), FluentProgressBarTrackStyle/FluentProgressBarFillStyle
    // on the Progress (style() and fill()). Both stay native everywhere
    // else in this file for the same side-by-side comparison every other
    // Fluent preview here already gives.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "10) Fluent Slider/Progress previews",
        "Drag the slider - the progress bar underneath is just a fixed value.");

    auto* fluentSliderRow = new newui::SubView();
    fluentSliderRow->setVisible(true);
    constexpr float kFluentSliderRowPadding = 4.0f;
    fluentSliderRow->setDesiredSize(newui::Size(0.0f, 20.0f + kFluentSliderRowPadding * 2.0f));
    auto fluentSliderRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    fluentSliderRowLayout->setPadding(kFluentSliderRowPadding);
    fluentSliderRowLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Center);
    fluentSliderRow->setLayout(std::move(fluentSliderRowLayout));
    contentContainer->addChild(fluentSliderRow);

    auto* fluentSlider = new newui::Slider();
    fluentSlider->setValue(40.0f);
    // Same CrossAxisAlignment::Center gotcha section 9's own comments
    // (above) cover in full - a real height here, not just the width-
    // stretch FlexLayoutParams below.
    fluentSlider->setDesiredSize(newui::Size(0.0f, 20.0f));
    fluentSlider->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    fluentSlider->setStyle(std::make_unique<newui::FluentTrackbarTrackStyle>());
    fluentSlider->thumb()->setStyle(std::make_unique<newui::FluentTrackbarThumbStyle>());
    fluentSliderRow->addChild(fluentSlider);

    auto* fluentProgressRow = new newui::SubView();
    fluentProgressRow->setVisible(true);
    fluentProgressRow->setDesiredSize(newui::Size(0.0f, 8.0f));
    auto fluentProgressRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    fluentProgressRow->setLayout(std::move(fluentProgressRowLayout));
    contentContainer->addChild(fluentProgressRow);

    auto* fluentProgress = new newui::Progress();
    fluentProgress->setValue(0.6f);
    fluentProgress->setDesiredSize(newui::Size(0.0f, 8.0f));
    fluentProgress->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    fluentProgress->setStyle(std::make_unique<newui::FluentProgressBarTrackStyle>());
    fluentProgress->fill()->setStyle(std::make_unique<newui::FluentProgressBarFillStyle>());
    fluentProgressRow->addChild(fluentProgress);

    // -----------------------------------------------------------------
    // 11) GroupBox previews - a native ThemedGroupBoxStyle GroupBox next
    // to a FluentGroupBoxStyle one, each wrapping two Toggles of the
    // matching look. GroupBox itself is a plain container (own class
    // comment, controls.h) - its own vertical FlexLayout below is just
    // this example wiring it up like any other child content, not
    // something GroupBox provides automatically.
    // -----------------------------------------------------------------
    AddSectionHeader(contentContainer, "11) GroupBox previews",
        "A native GroupBox and a Fluent one, each grouping two checkboxes.");

    auto* groupBoxRow = new newui::SubView();
    groupBoxRow->setVisible(true);
    groupBoxRow->setDesiredSize(newui::Size(0.0f, 90.0f));
    auto groupBoxRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    groupBoxRowLayout->setSpacing(16.0f);
    groupBoxRow->setLayout(std::move(groupBoxRowLayout));
    contentContainer->addChild(groupBoxRow);

    auto* nativeGroupBox = new newui::GroupBox();
    nativeGroupBox->setText("Native");
    nativeGroupBox->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    // 20px padding - comfortably clears GroupBox::paint()'s own caption
    // (drawn just inside the frame's own top-left corner, see its class
    // comment) so the first child never visually collides with it.
    auto nativeGroupBoxLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    nativeGroupBoxLayout->setSpacing(6.0f);
    nativeGroupBoxLayout->setPadding(20.0f);
    // CrossAxisAlignment::Start, not the vertical FlexLayout default's
    // implicit Stretch - a Toggle's own setDesiredSize() width (16px,
    // below) otherwise gets overridden by stretching to the full
    // available width. The native checkbox theme part happens to ignore
    // that extra width and still draws its small fixed-size glyph
    // regardless (so this bug was invisible on the native side) but
    // FluentCheckBoxStyle draws a rounded rect proportional to its
    // *whole* given size - confirmed live, it rendered as a wide short
    // bar instead of a small square until this was added.
    nativeGroupBoxLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Start);
    nativeGroupBox->setLayout(std::move(nativeGroupBoxLayout));
    groupBoxRow->addChild(nativeGroupBox);

    auto* nativeToggleA = new newui::Toggle();
    nativeToggleA->setDesiredSize(newui::Size(16.0f, 16.0f));
    nativeGroupBox->addChild(nativeToggleA);

    auto* nativeToggleB = new newui::Toggle();
    nativeToggleB->setDesiredSize(newui::Size(16.0f, 16.0f));
    nativeGroupBox->addChild(nativeToggleB);

    auto* fluentGroupBox = new newui::GroupBox();
    fluentGroupBox->setText("Fluent");
    auto fluentGroupBoxStyle = std::make_unique<newui::FluentGroupBoxStyle>();
    // Same "a freshly constructed style has no font" gotcha row1's own
    // comment (section 1, above) describes for buttonOne -
    // GroupBox::paint() silently skips drawing the caption with an
    // unresolved font.
    fluentGroupBoxStyle->setFont(newui::FontManager::getSystemFont(newui::SystemUIFont::Message));
    fluentGroupBox->setStyle(std::move(fluentGroupBoxStyle));
    fluentGroupBox->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    auto fluentGroupBoxLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    fluentGroupBoxLayout->setSpacing(6.0f);
    fluentGroupBoxLayout->setPadding(20.0f);
    fluentGroupBoxLayout->setCrossAxisAlignment(newui::CrossAxisAlignment::Start);
    fluentGroupBox->setLayout(std::move(fluentGroupBoxLayout));
    groupBoxRow->addChild(fluentGroupBox);

    auto* fluentToggleA = new newui::Toggle();
    fluentToggleA->setDesiredSize(newui::Size(16.0f, 16.0f));
    fluentToggleA->setStyle(std::make_unique<newui::FluentCheckBoxStyle>());
    fluentGroupBox->addChild(fluentToggleA);

    auto* fluentToggleB = new newui::Toggle();
    fluentToggleB->setDesiredSize(newui::Size(16.0f, 16.0f));
    fluentToggleB->setStyle(std::make_unique<newui::FluentCheckBoxStyle>());
    fluentGroupBox->addChild(fluentToggleB);

    app.run();

    return 0;
}

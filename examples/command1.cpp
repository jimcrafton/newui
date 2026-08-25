// Demonstrates the CommandId/Action/CommandTable responder-chain command
// dispatch added to View/RootView (see command.h, action.h) - a UIKit-
// responder-chain-inspired mechanism, deliberately narrower than UIKit's
// full event-bubbling chain: just validated focus transfer
// (canResignFocus()/canBecomeFocused()) plus a chain-walked "does whatever
// is focused answer this command" query (RootView::canPerformCommand()/
// performCommand()).
//
// Two NoteField "documents" sit side by side, each with its own Lock
// toggle. A single File/Edit MenuBar at the top has one Copy and one
// Paste item, wired generically against RootView - not against either
// NoteField by name - via commands::copy/commands::paste. Click into
// either NoteField, then use Edit > Copy/Paste (or Ctrl+C/Ctrl+V, wired as
// real hotkeys on the same Action objects): the *same* menu items work
// correctly no matter which NoteField is currently focused, because the
// menu never has to know which concrete control that is - it just asks
// RootView. Checking a NoteField's Lock toggle makes that NoteField
// refuse to give up focus (canResignFocus() returns false) until
// unlocked - try clicking the other NoteField while locked.

#include "newui/newui.h"
#include "newui/action.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/command.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/keyboard_constants.h"
#include "newui/layout.h"
#include "newui/menus.h"
#include "newui/rootview.h"
#include "newui/runloop.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

namespace {

newui::Frame* g_demoFrame = nullptr;

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    return newui::SyncReturn::Handled;
}

// A focusable "document" - just enough of one to make Copy/Paste mean
// something (a plain std::string caption, no real text editing). Draws
// itself via a LabelStyle it owns directly (same approach controls1.cpp's
// MakeLabel() uses for a static caption; NoteField's own text changes
// after construction, unlike MakeLabel's, so it keeps the LabelStyle
// pointer around to mutate later instead of just setting it once).
//
// Registers its own Copy/Paste Actions into a CommandTable (command.h) -
// this is the only place that knows NoteField exists at all. RootView's
// chain walk (canPerformCommand()/performCommand(), rootview.h) and the
// Edit menu built in main() both go through CommandId alone.
class NoteField : public newui::SubView {
public:
    explicit NoteField(std::string* clipboard, std::string label)
        : clipboard_(clipboard), label_(std::move(label)),
          copyAction_(newui::commands::copy, "Copy"),
          pasteAction_(newui::commands::paste, "Paste") {
        auto style = std::make_unique<newui::LabelStyle>();
        labelStyle_ = style.get();
        labelStyle_->textColor = newui::UIColorManager::colorFor(newui::UIColorRole::WindowText).toBLRgba32();
        setStyle(std::move(style));
        setVisible(true);
        setDesiredSize(newui::Size(0.0f, 60.0f));
        setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));

        copyAction_.onActionUpdated.add([this](newui::Action&, bool& enabled) {
            enabled = !text_.empty();
            return newui::SyncReturn::Handled;
        });
        copyAction_.onActionPerformed.add([this](newui::Action&) {
            *clipboard_ = text_;
            printf("Copy: \"%s\" <- %s\n", clipboard_->c_str(), label_.c_str());
            fflush(stdout);
            return newui::SyncReturn::Handled;
        });

        pasteAction_.onActionUpdated.add([this](newui::Action&, bool& enabled) {
            enabled = !clipboard_->empty();
            return newui::SyncReturn::Handled;
        });
        pasteAction_.onActionPerformed.add([this](newui::Action&) {
            setText(*clipboard_);
            printf("Paste: %s <- \"%s\"\n", label_.c_str(), text_.c_str());
            fflush(stdout);
            return newui::SyncReturn::Handled;
        });

        commands_.add(copyAction_);
        commands_.add(pasteAction_);

        onGotFocus.add(this, &NoteField::handleGotFocus);
        onLostFocus.add(this, &NoteField::handleLostFocus);

        // Sets labelStyle_'s background for the very first paint, not
        // just on a later focus/lock change - an opaque fill is needed
        // unconditionally (see controls1.cpp's MakeLabel() comment on
        // why), and this is the only place besides
        // handleGotFocus()/handleLostFocus()/setLocked() that never ran
        // updateBackground() at all before the first repaint.
        updateBackground();
        setText("");
    }

    void setText(std::string text) {
        text_ = std::move(text);
        labelStyle_->text = label_ + ": " + (text_.empty() ? "(empty)" : text_);
        style().markDirty();
    }

    void setLocked(bool locked) {
        locked_ = locked;
        updateBackground();
    }

    bool canPerformCommand(const newui::CommandId& cmd) const override {
        return commands_.canPerform(cmd);
    }
    void performCommand(const newui::CommandId& cmd) override {
        commands_.perform(cmd);
    }

    bool canResignFocus() const override {
        return !locked_;
    }

private:
    void updateBackground() {
        newui::UIColorRole role = locked_ ? newui::UIColorRole::ControlBorder
                                           : (isFocused_ ? newui::UIColorRole::HighlightBackground
                                                          : newui::UIColorRole::WindowBackground);
        labelStyle_->setBackgroundColor(newui::UIColorManager::colorFor(role));
        style().markDirty();
    }

    newui::SyncReturn handleGotFocus(newui::View&) {
        isFocused_ = true;
        updateBackground();
        return newui::SyncReturn::Handled;
    }

    newui::SyncReturn handleLostFocus(newui::View&) {
        isFocused_ = false;
        updateBackground();
        return newui::SyncReturn::Handled;
    }

    std::string* clipboard_;
    std::string label_;
    std::string text_;
    bool locked_ = false;
    bool isFocused_ = false;
    newui::LabelStyle* labelStyle_ = nullptr;
    newui::Action copyAction_;
    newui::Action pasteAction_;
    newui::CommandTable commands_;
};

}  // namespace

int main() {
    std::cout << "newui " << newui::version() << " - command dispatch example\n";
    std::cout << "Click a note, then Edit > Copy/Paste (or Ctrl+C/Ctrl+V) - works on\n";
    std::cout << "whichever note is focused without the menu knowing which one that is.\n";
    std::cout << "Check a note's Lock box to see canResignFocus() veto a focus change.\n";

    newui::Frame frame;
    g_demoFrame = &frame;

    newui::Application& app = newui::Application::instance();
    app.setName("command1");
    app.setFrame(&frame);

    frame.setTitle("Command Dispatch Example");
    frame.setBounds(newui::Rect(10, 10, 640, 320));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(10.0f);
    rootLayout->setPadding(16.0f);
    root.setLayout(std::move(rootLayout));

    // One Action per command, shared by both entry points below (the
    // menu item and the Ctrl+C/Ctrl+V hotkey) - onActionUpdated re-runs
    // canPerformCommand() fresh, both right before the Edit menu opens
    // (MenuItem::setAction() below, plus the ContextMenu::buildMenuLevel()
    // call in menus.cpp that fires it) and whenever anything else asks
    // enabled(), so grayed-out-or-not always matches whatever's actually
    // focused right now. onActionPerformed drives the exact same
    // RootView::performCommand() call either way.
    newui::Action copyAction(newui::commands::copy, "Copy");
    copyAction.setHotkey(newui::vkLetterC, newui::kmCtrl);
    copyAction.onActionUpdated.add([&root](newui::Action&, bool& enabled) {
        enabled = root.canPerformCommand(newui::commands::copy);
        return newui::SyncReturn::Handled;
    });
    copyAction.onActionPerformed.add([&root](newui::Action&) {
        root.performCommand(newui::commands::copy);
        return newui::SyncReturn::Handled;
    });
    app.runLoop().registerAction(&copyAction);

    newui::Action pasteAction(newui::commands::paste, "Paste");
    pasteAction.setHotkey(newui::vkLetterV, newui::kmCtrl);
    pasteAction.onActionUpdated.add([&root](newui::Action&, bool& enabled) {
        enabled = root.canPerformCommand(newui::commands::paste);
        return newui::SyncReturn::Handled;
    });
    pasteAction.onActionPerformed.add([&root](newui::Action&) {
        root.performCommand(newui::commands::paste);
        return newui::SyncReturn::Handled;
    });
    app.runLoop().registerAction(&pasteAction);

    // Menu bar - one File > Exit, one Edit > Copy/Paste. The Edit items
    // are wired only against RootView and CommandId (via copyAction/
    // pasteAction above), never against a NoteField directly.
    std::vector<std::unique_ptr<newui::MenuItem>> menuItems;

    auto fileMenu = std::make_unique<newui::MenuItem>("File");
    fileMenu->addChild(std::make_unique<newui::MenuItem>("Exit"))->onClick.add(
        [](newui::MenuItem&) {
            if (g_demoFrame != nullptr && g_demoFrame->frameHandle() != nullptr) {
                ::SendMessage(g_demoFrame->frameHandle(), WM_CLOSE, 0, 0);
            }
            return newui::SyncReturn::Handled;
        });
    menuItems.push_back(std::move(fileMenu));

    auto editMenu = std::make_unique<newui::MenuItem>("Edit");
    newui::MenuItem* copyItem = editMenu->addChild(std::make_unique<newui::MenuItem>("Copy"));
    copyItem->shortcutText = "Ctrl+C";
    // setAction() is what makes this item's enabled/grayed-out state
    // track copyAction.onActionUpdated automatically (see the
    // ContextMenu::buildMenuLevel() comment, menus.cpp) - onClick still
    // has to be wired separately, setAction() alone doesn't do that.
    copyItem->setAction(&copyAction);
    copyItem->onClick.add([&root](newui::MenuItem&) {
        root.performCommand(newui::commands::copy);
        return newui::SyncReturn::Handled;
    });
    newui::MenuItem* pasteItem = editMenu->addChild(std::make_unique<newui::MenuItem>("Paste"));
    pasteItem->shortcutText = "Ctrl+V";
    pasteItem->setAction(&pasteAction);
    pasteItem->onClick.add([&root](newui::MenuItem&) {
        root.performCommand(newui::commands::paste);
        return newui::SyncReturn::Handled;
    });
    menuItems.push_back(std::move(editMenu));

    auto* menuBar = new newui::MenuBar();
    menuBar->setMenuItems(std::move(menuItems));
    menuBar->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    root.addChild(menuBar);

    auto* instructions = new newui::SubView();
    instructions->setVisible(true);
    auto instructionsStyle = std::make_unique<newui::LabelStyle>();
    instructionsStyle->text = "Click a note below, then Edit > Copy/Paste (or Ctrl+C/Ctrl+V). Lock a note to veto focus leaving it.";
    instructionsStyle->textColor = newui::UIColorManager::colorFor(newui::UIColorRole::WindowText).toBLRgba32();
    instructionsStyle->setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));
    instructions->setStyle(std::move(instructionsStyle));
    instructions->setDesiredSize(newui::Size(0.0f, 20.0f));
    root.addChild(instructions);

    std::string clipboard;

    auto* notesRow = new newui::SubView();
    notesRow->setVisible(true);
    auto notesRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    notesRowLayout->setSpacing(16.0f);
    notesRow->setLayout(std::move(notesRowLayout));
    notesRow->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    root.addChild(notesRow);

    const char* seedText[2] = {"First note's text", ""};
    NoteField* firstNote = nullptr;
    for (int i = 0; i < 2; ++i) {
        auto* column = new newui::SubView();
        column->setVisible(true);
        auto columnLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
        columnLayout->setSpacing(6.0f);
        column->setLayout(std::move(columnLayout));
        column->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        notesRow->addChild(column);

        auto* lockRow = new newui::SubView();
        lockRow->setVisible(true);
        lockRow->setDesiredSize(newui::Size(0.0f, 20.0f));
        auto lockRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
        lockRowLayout->setSpacing(6.0f);
        lockRow->setLayout(std::move(lockRowLayout));
        column->addChild(lockRow);

        auto* note = new NoteField(&clipboard, i == 0 ? "Note 1" : "Note 2");
        note->setText(seedText[i]);
        if (i == 0) {
            firstNote = note;
        }

        auto* lockToggle = new newui::Toggle();
        lockToggle->setDesiredSize(newui::Size(16.0f, 16.0f));
        lockToggle->onCheckedChanged.add([&root, note](newui::Toggle& sender) {
            note->setLocked(sender.isChecked());
            // Clicking the checkbox itself already moved focus onto it
            // (RootView::mouseDown() unconditionally focuses whatever it
            // hit, before this onCheckedChanged even fires) - re-focus
            // note now that it's actually locked, or canResignFocus()
            // would have nothing left to protect: the checkbox, not
            // note, would be focusedSubView_ by the time anything tried
            // to click away.
            root.setFocusedSubView(note);
            return newui::SyncReturn::Handled;
        });
        lockRow->addChild(lockToggle);

        auto* lockLabel = new newui::Label();
        lockLabel->setText("Lock");
        lockLabel->setDesiredSize(newui::Size(60.0f, 20.0f));
        lockRow->addChild(lockLabel);

        column->addChild(note);
    }

    // Starts with something focused, both so the window is immediately
    // usable (no unexplained "nothing happens" first click) and so the
    // Edit menu's Copy/Paste enabled state has something real to
    // reflect the very first time it's opened.
    root.setFocusedSubView(firstNote);

    app.run();

    return 0;
}

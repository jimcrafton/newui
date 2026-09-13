// Loads examples/confirmdialog.newui - a hand-written JSON5 View document
// (see .claude/skills/newui-view-json5/SKILL.md), never produced by
// ObjectWriter - and shows it as a real window, proving the reflection
// read path (HANDOFF.md Part 95: reader-driven, base-chain-aware Class
// lookups, genuinely-safe-to-omit properties) works on a document nobody
// ever wrote programmatically, not just a round-tripped one.
//
// The file describes a Frame with a Label asking a question and an OK/
// Cancel button row - every property present in it round-trips through
// Bundle::loadFrame() into a live Frame/RootView/Label/Button tree, and
// every property NOT mentioned (desiredSizeOverride, highlighted,
// clientBounds, ...) comes from each object's own constructor defaults,
// untouched - exactly the guarantee this session's read-path redesign
// exists to provide.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/view.h"

#include <cstdio>

// Defined in the reflectgen-generated .cpp - see shapes1.cpp's own comment
// on this same forward declaration for why it's needed (every reflectable
// class in newui/*.h gets its Class/Property data registered there, and
// nothing calls this automatically for a standalone exe).
extern void registerReflectionData();

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Dialog window closed.\n");
    return newui::SyncReturn::Handled;
}

int main() {
    printf("newui %s - loaddialog1: load a hand-written .newui file, show it live\n", newui::version());

    registerReflectionData();

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("loaddialog1");
    app.setFrame(&frame);

    // Matches confirmdialog.newui's own "name" property - Bundle::
    // loadFrame() resolves "Resources/<frame.getName()>.newui" under the
    // running exe's own directory (bundle.h's own class comment); the
    // CMake target below copies the source-tree file there at build time.
    frame.setName("confirmdialog");
    frame.onClosed += FrameClosed;

    if (!newui::Bundle::instance().loadFrame(frame)) {
        printf("Could not load Resources/confirmdialog.newui - see examples/CMakeLists.txt's "
               "loaddialog1 post-build copy step.\n");
        return 1;
    }

    newui::RootView& root = frame.rootView();

    // Prove the load actually reconstructed real content from the file,
    // not just an empty window - printed before the window is even shown.
    auto* questionLabel = dynamic_cast<newui::Label*>(root.findView("questionLabel"));
    auto* okButton = dynamic_cast<newui::Button*>(root.findView("okButton"));
    auto* cancelButton = dynamic_cast<newui::Button*>(root.findView("cancelButton"));
    if (questionLabel == nullptr || okButton == nullptr || cancelButton == nullptr) {
        printf("Resources/confirmdialog.newui is missing one of the named views this example wires up "
               "(questionLabel/okButton/cancelButton) - can't continue.\n");
        return 1;
    }

    printf("Loaded from file - title=\"%s\" question=\"%s\" okText=\"%s\" cancelText=\"%s\"\n",
           frame.getTitle().c_str(), questionLabel->text().c_str(), okButton->text().c_str(),
           cancelButton->text().c_str());

    okButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&frame](newui::Control&) -> newui::SyncReturn {
            printf("OK pressed.\n");
            ::PostMessage(frame.frameHandle(), WM_CLOSE, 0, 0);
            return newui::SyncReturn::Handled;
        }));

    cancelButton->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&frame](newui::Control&) -> newui::SyncReturn {
            printf("Cancel pressed.\n");
            ::PostMessage(frame.frameHandle(), WM_CLOSE, 0, 0);
            return newui::SyncReturn::Handled;
        }));

    app.run();

    return 0;
}

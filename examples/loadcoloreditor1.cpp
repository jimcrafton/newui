// Loads examples/coloreditordialog.newui - a .newui file generated from an
// HTML/CSS mockup (D:\code\cpp_codetools\design\reference\color_editor_dialog.html)
// via the newui-view-json5 skill's HTML-mockup-conversion process (see
// .claude/skills/newui-view-json5/references/html-mockup-conversion.md) -
// and shows it as a real window. Same shape as loaddialog1.cpp, a
// substantially richer document: nested FlexLayout rows/columns standing in
// for the mockup's CSS flex/grid layout, a real multi-stop Gradient for the
// hue rail, and solid-color placeholders where the mockup used a 2-axis
// SV blend / checkerboard alpha this format has no equivalent for.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/bundle.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/utils.h"
#include "newui/view.h"

#include <cstdio>
#include <functional>

// Defined in the reflectgen-generated .cpp - see shapes1.cpp's own comment
// on this same forward declaration for why it's needed (every reflectable
// class in newui/*.h gets its Class/Property data registered there, and
// nothing calls this automatically for a standalone exe).
extern void registerReflectionData();

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Color editor dialog closed.\n");
    return newui::SyncReturn::Handled;
}

int main() {
    printf("newui %s - loadcoloreditor1: load an HTML-mockup-derived .newui file, show it live\n",
           newui::version());

    registerReflectionData();

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("loadcoloreditor1");
    app.setFrame(&frame);

    // Matches coloreditordialog.newui's own "name" property - Bundle::
    // loadFrame() resolves "Resources/<frame.getName()>.newui" under the
    // running exe's own directory; the CMake target below copies the
    // source-tree file there at build time.
    frame.setName("coloreditordialog");
    frame.onClosed += FrameClosed;

    if (!newui::Bundle::instance().loadFrame(frame)) {
        printf("Could not load Resources/coloreditordialog.newui - see examples/CMakeLists.txt's "
               "loadcoloreditor1 post-build copy step.\n");
        return 1;
    }

    newui::RootView& root = frame.rootView();

    auto* closeBtn = dynamic_cast<newui::Button*>(root.findView("closeBtn"));
    auto* cancelBtn = dynamic_cast<newui::Button*>(root.findView("cancelBtn"));
    auto* applyBtn = dynamic_cast<newui::Button*>(root.findView("applyBtn"));
    auto* hexInput = dynamic_cast<newui::TextField*>(root.findView("hexInput"));
    auto* compareHex = dynamic_cast<newui::Label*>(root.findView("compareHex"));
    if (closeBtn == nullptr || cancelBtn == nullptr || applyBtn == nullptr ||
            hexInput == nullptr || compareHex == nullptr) {
        printf("Resources/coloreditordialog.newui is missing one of the named views this example "
               "wires up (closeBtn/cancelBtn/applyBtn/hexInput/compareHex) - can't continue.\n");
        return 1;
    }

    // Prove the load actually reconstructed real content from the file,
    // not just an empty window. TextField::text() is std::wstring (unlike
    // Label::text()/Frame::getTitle()'s std::string) - narrow it for
    // printf via the same newui::wideToUtf8() the reflection read path
    // itself uses for this property.
    printf("Loaded from file - title=\"%s\" hexInput=\"%s\" compareHex=\"%s\"\n",
           frame.getTitle().c_str(), newui::wideToUtf8(hexInput->text()).c_str(), compareHex->text().c_str());

    auto closeDialog = [&frame](const char* which) {
        printf("%s pressed.\n", which);
        ::PostMessage(frame.frameHandle(), WM_CLOSE, 0, 0);
        return newui::SyncReturn::Handled;
    };
    closeBtn->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&](newui::Control&) { return closeDialog("Close"); }));
    cancelBtn->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&](newui::Control&) { return closeDialog("Cancel"); }));
    applyBtn->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&](newui::Control&) { return closeDialog("Apply"); }));

    app.run();

    return 0;
}

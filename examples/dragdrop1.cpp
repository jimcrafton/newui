// Demonstrates newui's OLE drag-and-drop support (dragndrop.h) through its
// per-View association layer: View::setDragSource()/dragSource() for
// originating a drag, View::setDropTarget()/dropTarget() for accepting
// one - both plain, Delegate-based objects (newui::DropSource/
// newui::DropTarget), not COM types. RootView's own mouse dispatch
// generically detects a press-and-drag gesture on any View with a
// DragSource (no per-app gesture code needed any more), and the one
// per-window COMDropTarget hit-tests the cursor against the View tree on
// every incoming DragEnter/DragOver/Drop.
//
// Layout: an editable "Drag text" TextField, a "Drag Source" box (press
// and drag from inside it, past a small threshold, to start a real OLE
// text drag carrying whatever the TextField currently holds), a "Drag
// File Source" box (same gesture, but starts a virtual in-memory file
// drag instead - CFSTR_FILEDESCRIPTORW/CFSTR_FILECONTENTS, no temp file
// ever written to disk by this app), and a separate "Drop Target" box -
// deliberately its own View, not root itself, so the shell's accept
// cursor only ever shows over that one box, never the rest of the window.
// A status Label reports what happened; dropping onto the Drop Target box
// fills in the "Dropped text"/"Dropped files"/"Dropped image" rows below
// it.
//
// Things worth trying:
// - Drag the Drag Source box's text into Notepad (or the Windows search
//   box, a browser's address bar, etc.) - watch the shell's own
//   semi-transparent drag thumbnail follow the cursor and confirm the
//   text lands correctly on drop.
// - Drag the Drag File Source box into a real Windows Explorer folder -
//   confirms a real physical file ("newui-drag.txt", holding whatever's
//   in the TextField, UTF-8 encoded) materializes with the correct name
//   and byte-for-byte contents, entirely from an in-memory
//   VirtualFileDataObject.
// - Press Escape mid-drag - the status line should report no drop rather
//   than a completed one.
// - Edit the TextField, then drag again - onProvideText/onProvideFiles
//   always read whatever's in the field *at the moment the drag starts*.
// - Move the cursor around the window while dragging something in from
//   outside (e.g. drag a file out of Explorer without dropping it yet) -
//   the accept cursor should only ever appear over the Drop Target box,
//   never either Drag Source box, the TextField, or empty window
//   background.
// - Drag one or more files from Explorer onto the Drop Target box -
//   confirms CF_HDROP extraction end to end.
// - Drag an image out of a browser or Paint onto the Drop Target box -
//   confirms CF_DIB extraction (dibimage.h's dibBytesToImage()) end to
//   end.
// - Drag highlighted text from another app onto the Drop Target box -
//   confirms CF_UNICODETEXT extraction independent of this app's own
//   outgoing drags above.
//
// Known rough edge (real, not swept under the rug): DoDragDrop() runs its
// own modal loop and consumes the terminating WM_LBUTTONUP itself, so
// RootView's own mouse-capture bookkeeping (capturedSubView_) never sees a
// matching mouseUp() for the button-down that started the drag. In
// practice this only affects mouse-*move* routing until the next real
// click anywhere in the window (mouseDown always resets it) - clicking
// once after a drag completes clears it. This now lives inside RootView's
// own generic drag-gesture code (rootview.cpp), not demo-specific
// workaround code - still a known, documented limitation pending a real
// fix (letting go of capture more deliberately before a drag starts).
//
// The one per-window COMDropTarget itself is registered/revoked
// automatically by RootView (viewCreated()/destroy(), rootview.cpp) -
// nothing in this file has to do that lifecycle management at all.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/dragndrop.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

newui::Frame* g_demoFrame = nullptr;

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    fflush(stdout);
    return newui::SyncReturn::Handled;
}

std::wstring joinPaths(const std::vector<std::wstring>& paths) {
    std::wstring joined;
    for (std::size_t i = 0; i < paths.size(); ++i) {
        if (i != 0) {
            joined += L"; ";
        }
        joined += paths[i];
    }
    return joined;
}

const char* dropEffectName(newui::DropEffect effect) {
    switch (effect) {
    case newui::DropEffect::Copy: return "Copy";
    case newui::DropEffect::Move: return "Move";
    case newui::DropEffect::Link: return "Link";
    default: return "None";
    }
}

// UTF-8 encodes text - the virtual file's real, on-disk contents once
// Explorer materializes it, same conversion clipboard1.cpp's own
// toUtf8Bytes() uses for its custom-format demo.
std::vector<std::uint8_t> toUtf8Bytes(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    int required = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(required));
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), reinterpret_cast<char*>(bytes.data()), required, nullptr, nullptr);
    return bytes;
}

}  // namespace

int main() {
    std::cout << "newui " << newui::version() << " - drag-and-drop example\n";
    std::cout << "Press and drag from inside the \"Drag Source\" box - see the comment at\n";
    std::cout << "the top of dragdrop1.cpp for things worth trying, including dragging\n";
    std::cout << "into a real app like Notepad.\n";

    newui::Frame frame;
    g_demoFrame = &frame;

    newui::Application& app = newui::Application::instance();
    app.setName("dragdrop1");
    app.setFrame(&frame);

    frame.setTitle("Drag-and-Drop Example");
    frame.setBounds(newui::Rect(10, 10, 520, 700));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(10.0f);
    rootLayout->setPadding(16.0f);
    root.setLayout(std::move(rootLayout));

    auto* statusLabel = new newui::Label();
    statusLabel->setDesiredSize(newui::Size(0.0f, 40.0f));
    statusLabel->setText("Ready.");
    root.addChild(statusLabel);

    auto* fieldRow = new newui::SubView();
    fieldRow->setVisible(true);
    fieldRow->setDesiredSize(newui::Size(0.0f, 28.0f));
    auto fieldRowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
    fieldRowLayout->setSpacing(8.0f);
    fieldRow->setLayout(std::move(fieldRowLayout));
    root.addChild(fieldRow);

    auto* fieldLabel = new newui::Label();
    fieldLabel->setText("Drag text:");
    fieldLabel->setDesiredSize(newui::Size(80.0f, 24.0f));
    fieldRow->addChild(fieldLabel);

    auto* textField = new newui::TextField();
    textField->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    textField->setText(L"Hello from newui drag!");
    fieldRow->addChild(textField);

    auto* dragSourceHint = new newui::Label();
    dragSourceHint->setDesiredSize(newui::Size(0.0f, 20.0f));
    dragSourceHint->setText("Drag Source - press and drag from inside the box below:");
    root.addChild(dragSourceHint);

    // A plain, childless colored box - deliberately no nested Label here:
    // RootView::hitTestChildren() dispatches to the deepest child under
    // the cursor, so a child covering part of this box would steal mouse
    // events over its own bounds and this box's own DragSource gesture
    // would never be detected there.
    auto* dragSource = new newui::SubView();
    dragSource->setVisible(true);
    dragSource->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    dragSource->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
    dragSource->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
    dragSource->style().borderWidth = 1.0f;
    root.addChild(dragSource);

    auto* dragFileSourceHint = new newui::Label();
    dragFileSourceHint->setDesiredSize(newui::Size(0.0f, 20.0f));
    dragFileSourceHint->setText("Drag File Source - press and drag a virtual file from inside the box below:");
    root.addChild(dragFileSourceHint);

    // Same "no nested Label" reasoning as dragSource above.
    auto* dragFileSource = new newui::SubView();
    dragFileSource->setVisible(true);
    dragFileSource->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    dragFileSource->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
    dragFileSource->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
    dragFileSource->style().borderWidth = 1.0f;
    root.addChild(dragFileSource);

    auto* dropTargetHint = new newui::Label();
    dropTargetHint->setDesiredSize(newui::Size(0.0f, 20.0f));
    dropTargetHint->setText("Drop Target - only this box accepts an incoming drag:");
    root.addChild(dropTargetHint);

    // Deliberately its own box, separate from dragSource above and never
    // registered on root - proves the point of this whole redesign: the
    // shell's accept cursor should only ever show over a View that
    // actually registered a DropTarget, not the whole window.
    auto* dropTargetBox = new newui::SubView();
    dropTargetBox->setVisible(true);
    dropTargetBox->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    dropTargetBox->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
    dropTargetBox->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
    dropTargetBox->style().borderWidth = 1.0f;
    root.addChild(dropTargetBox);

    auto makeRow = [&root](float height) {
        auto* row = new newui::SubView();
        row->setVisible(true);
        row->setDesiredSize(newui::Size(0.0f, height));
        auto rowLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Horizontal);
        rowLayout->setSpacing(8.0f);
        row->setLayout(std::move(rowLayout));
        root.addChild(row);
        return row;
    };

    auto* droppedTextRow = makeRow(28.0f);
    auto* droppedTextLabel = new newui::Label();
    droppedTextLabel->setText("Dropped text:");
    droppedTextLabel->setDesiredSize(newui::Size(90.0f, 24.0f));
    droppedTextRow->addChild(droppedTextLabel);
    auto* droppedTextField = new newui::TextField();
    droppedTextField->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    droppedTextRow->addChild(droppedTextField);

    auto* droppedFilesRow = makeRow(28.0f);
    auto* droppedFilesLabel = new newui::Label();
    droppedFilesLabel->setText("Dropped files:");
    droppedFilesLabel->setDesiredSize(newui::Size(90.0f, 24.0f));
    droppedFilesRow->addChild(droppedFilesLabel);
    auto* droppedFilesField = new newui::TextField();
    droppedFilesField->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    droppedFilesRow->addChild(droppedFilesField);

    auto* droppedImageRow = makeRow(48.0f);
    auto* droppedImageLabel = new newui::Label();
    droppedImageLabel->setText("Dropped image:");
    droppedImageLabel->setDesiredSize(newui::Size(90.0f, 24.0f));
    droppedImageRow->addChild(droppedImageLabel);
    auto* droppedImageSwatch = new newui::SubView();
    droppedImageSwatch->setVisible(true);
    droppedImageSwatch->setDesiredSize(newui::Size(48.0f, 48.0f));
    droppedImageSwatch->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    droppedImageSwatch->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
    droppedImageSwatch->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
    droppedImageSwatch->style().borderWidth = 1.0f;
    droppedImageRow->addChild(droppedImageSwatch);

    // Makes dragSource a real outgoing-drag origin - RootView's own mouse
    // dispatch (rootview.cpp) detects the press-and-move-past-threshold
    // gesture generically and calls StartDragOperation() itself, only if
    // onProvideText is actually handled.
    dragSource->setDragSource(std::make_unique<newui::DropSource>());
    dragSource->dragSource()->onProvideText.add(
        [textField](newui::DropSource&, std::wstring& outText) {
            outText = textField->text();
            printf("Starting outgoing drag: \"%ls\"\n", outText.c_str());
            fflush(stdout);
            return newui::SyncReturn::Handled;
        });
    dragSource->dragSource()->onDragComplete.add(
        [statusLabel](newui::DropSource&, newui::DropEffect effect) {
            char message[64];
            snprintf(message, sizeof(message), "Drag ended (%s).", dropEffectName(effect));
            statusLabel->setText(message);
            printf("%s\n", message);
            fflush(stdout);
            return newui::SyncReturn::Handled;
        });

    // Makes dragFileSource a real outgoing *virtual file* drag origin -
    // same generic gesture detection as dragSource above, but
    // onProvideFiles takes priority over onProvideText (dragndrop.h's own
    // documented order), so RootView calls StartVirtualFileDrag() instead
    // of StartDragOperation() for this box.
    dragFileSource->setDragSource(std::make_unique<newui::DropSource>());
    dragFileSource->dragSource()->onProvideFiles.add(
        [textField](newui::DropSource&, std::vector<newui::VirtualFile>& outFiles) {
            newui::VirtualFile file;
            file.fileName = L"newui-drag.txt";
            file.contents = toUtf8Bytes(textField->text());
            outFiles.push_back(std::move(file));
            printf("Starting outgoing virtual file drag: \"newui-drag.txt\"\n");
            fflush(stdout);
            return newui::SyncReturn::Handled;
        });
    dragFileSource->dragSource()->onDragComplete.add(
        [statusLabel](newui::DropSource&, newui::DropEffect effect) {
            char message[64];
            snprintf(message, sizeof(message), "Virtual file drag ended (%s).", dropEffectName(effect));
            statusLabel->setText(message);
            printf("%s\n", message);
            fflush(stdout);
            return newui::SyncReturn::Handled;
        });

    // Makes only dropTargetBox above a drop target - deliberately *not*
    // root, so the shell's accept cursor only ever shows over that one
    // box, never the rest of the window (the whole reason this session
    // moved drag-and-drop from a single window-global DropCallbacks to a
    // per-View association in the first place).
    dropTargetBox->setDropTarget(std::make_unique<newui::DropTarget>());
    dropTargetBox->dropTarget()->onFilesDropped.add(
        [statusLabel, droppedFilesField](newui::DropTarget&, const std::vector<std::wstring>& paths) {
            droppedFilesField->setText(joinPaths(paths));
            char message[64];
            snprintf(message, sizeof(message), "Dropped %zu file(s).", paths.size());
            statusLabel->setText(message);
            return newui::SyncReturn::Handled;
        });
    dropTargetBox->dropTarget()->onImageDropped.add(
        [statusLabel, droppedImageSwatch](newui::DropTarget&, const BLImage& image) {
            droppedImageSwatch->style().setBackgroundImage(image);
            statusLabel->setText("Dropped an image (CF_DIB).");
            return newui::SyncReturn::Handled;
        });
    dropTargetBox->dropTarget()->onTextDropped.add(
        [statusLabel, droppedTextField](newui::DropTarget&, const std::wstring& text) {
            droppedTextField->setText(text);
            statusLabel->setText("Dropped text.");
            return newui::SyncReturn::Handled;
        });

    // RootView registers itself as a real drop target automatically
    // (RootView::viewCreated(), rootview.cpp) - nothing to do here beyond
    // giving the field its initial focus.
    app.runLoop().onStart.add([&root, textField](newui::RunLoop&) {
        root.setFocusedSubView(textField);
        return newui::SyncReturn::Handled;
    });

    app.run();

    return 0;
}

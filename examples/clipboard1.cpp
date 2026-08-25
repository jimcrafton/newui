// Demonstrates newui::ClipboardManager (clipboardmgr.h): plain text
// through the real system clipboard, a custom binary format independent
// of CF_UNICODETEXT, and delayed rendering (WM_RENDERFORMAT/
// WM_RENDERALLFORMATS) for CF_UNICODETEXT itself - registering a
// renderer for a *real* standard format means pasting into any other
// real Windows app (Notepad, etc.) demonstrates it rendering lazily on
// demand, not just this app talking to itself.
//
// Layout: an editable "Source" TextField, five buttons, a read-only-in-
// spirit "Result" TextField showing whatever was last pasted, and a
// status Label logging what each button actually did.
//
// Things worth trying:
// - "Copy Text ->", then Alt+Tab to Notepad and Ctrl+V.
// - Type something in Notepad, Ctrl+C there, then "<- Paste Text" here.
// - "Copy Custom ->", then "<- Paste Text" (fails - the plain
//   CF_UNICODETEXT slot was never touched by it) vs. "<- Paste Custom"
//   (succeeds) - two independent clipboard slots from one EmptyClipboard().
// - "Copy (Delayed) ->", then Alt+Tab to Notepad and Ctrl+V - watch this
//   app's own console print "Rendering delayed clipboard text now" at
//   the exact moment Notepad asks for it, not when the button was
//   clicked. Closing this window without ever pasting still renders it
//   (WM_RENDERALLFORMATS) - watch the console on exit too.
// - "Copy Image ->", then Alt+Tab to Paint (or Word) and Ctrl+V - a real
//   CF_DIB bitmap, not a custom format, so any Win32 app that pastes
//   images at all can read it. Copy an image in Paint/a browser and
//   click "<- Paste Image" here to bring it back the other way.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/clipboardmgr.h"
#include "newui/color.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"
#include "newui/viewstyle.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
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

// UTF-8 encodes/decodes text for the custom-format demo below - just
// enough to prove the custom format really carries independent bytes,
// not a general-purpose Unicode conversion utility.
std::vector<std::uint8_t> toUtf8Bytes(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    int required = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(required));
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), reinterpret_cast<char*>(bytes.data()), required, nullptr, nullptr);
    return bytes;
}

std::wstring fromUtf8Bytes(const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty()) {
        return L"";
    }
    int required = ::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(static_cast<std::size_t>(required), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), &text[0], required);
    return text;
}

// A small, deliberately colorful swatch to copy - four quadrants plus a
// border, same idea as imagefill1.cpp's own BuildSwatchImage(), just
// smaller (this is a live demo image, not a reference swatch).
BLImage buildSwatchImage() {
    BLImage image(64, 64, BL_FORMAT_PRGB32);
    BLContext ctx(image);

    ctx.set_fill_style(newui::Color::fromName("tomato").toBLRgba32());
    ctx.fill_rect(BLRect(0, 0, 32, 32));
    ctx.set_fill_style(newui::Color::fromName("mediumseagreen").toBLRgba32());
    ctx.fill_rect(BLRect(32, 0, 32, 32));
    ctx.set_fill_style(newui::Color::fromName("steelblue").toBLRgba32());
    ctx.fill_rect(BLRect(0, 32, 32, 32));
    ctx.set_fill_style(newui::Color::fromName("goldenrod").toBLRgba32());
    ctx.fill_rect(BLRect(32, 32, 32, 32));

    ctx.set_stroke_style(newui::Color::fromName("black").toBLRgba32());
    ctx.set_stroke_width(2.0f);
    ctx.stroke_box(1.0, 1.0, 63.0, 63.0);

    ctx.end();
    return image;
}

}  // namespace

int main() {
    std::cout << "newui " << newui::version() << " - clipboard example\n";
    std::cout << "Type in the Source field, then try each button - see the comment at\n";
    std::cout << "the top of clipboard1.cpp for things worth trying, including pasting\n";
    std::cout << "into/from a real app like Notepad.\n";

    newui::Frame frame;
    g_demoFrame = &frame;

    newui::Application& app = newui::Application::instance();
    app.setName("clipboard1");
    app.setFrame(&frame);

    frame.setTitle("Clipboard Example");
    frame.setBounds(newui::Rect(10, 10, 640, 440));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(10.0f);
    rootLayout->setPadding(16.0f);
    root.setLayout(std::move(rootLayout));

    UINT customFormat = newui::ClipboardManager::registerCustomFormat(L"application/x-newui-clipboard1-example");

    auto* statusLabel = new newui::Label();
    statusLabel->setDesiredSize(newui::Size(0.0f, 40.0f));
    statusLabel->setText("Ready.");
    root.addChild(statusLabel);

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

    auto* sourceRow = makeRow(28.0f);
    auto* sourceLabel = new newui::Label();
    sourceLabel->setText("Source:");
    sourceLabel->setDesiredSize(newui::Size(70.0f, 24.0f));
    sourceRow->addChild(sourceLabel);

    auto* sourceField = new newui::TextField();
    sourceField->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    sourceField->setText(L"Hello from newui!");
    sourceRow->addChild(sourceField);

    auto* buttonsRow = makeRow(28.0f);
    auto addButton = [buttonsRow](const std::string& text) {
        auto* button = new newui::Button();
        button->setText(text);
        button->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
        buttonsRow->addChild(button);
        return button;
    };

    newui::Button* copyTextButton = addButton("Copy Text ->");
    newui::Button* pasteTextButton = addButton("<- Paste Text");
    newui::Button* copyCustomButton = addButton("Copy Custom ->");
    newui::Button* pasteCustomButton = addButton("<- Paste Custom");
    newui::Button* copyDelayedButton = addButton("Copy (Delayed) ->");

    // Image swatches - a fixed-size source swatch to copy, image
    // buttons, and a placeholder swatch that shows whatever was last
    // pasted. Both swatches are plain SubViews with an image fill
    // (ViewStyle::setBackgroundImage()), the same technique
    // imagefill1.cpp uses to display an in-memory BLImage with no file
    // round trip involved.
    BLImage sourceImage = buildSwatchImage();

    auto* imageRow = makeRow(74.0f);
    auto* sourceSwatch = new newui::SubView();
    sourceSwatch->setVisible(true);
    sourceSwatch->setDesiredSize(newui::Size(74.0f, 74.0f));
    sourceSwatch->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    sourceSwatch->style().setBackgroundImage(sourceImage);
    sourceSwatch->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
    sourceSwatch->style().borderWidth = 1.0f;
    imageRow->addChild(sourceSwatch);

    auto* copyImageButton = new newui::Button();
    copyImageButton->setText("Copy Image ->");
    copyImageButton->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    imageRow->addChild(copyImageButton);

    auto* pasteImageButton = new newui::Button();
    pasteImageButton->setText("<- Paste Image");
    pasteImageButton->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    imageRow->addChild(pasteImageButton);

    auto* pastedSwatch = new newui::SubView();
    pastedSwatch->setVisible(true);
    pastedSwatch->setDesiredSize(newui::Size(74.0f, 74.0f));
    pastedSwatch->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(0.0f));
    pastedSwatch->style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::ControlBackground));
    pastedSwatch->style().borderFill = newui::Color::fromName("dimgray").toBLRgba32();
    pastedSwatch->style().borderWidth = 1.0f;
    imageRow->addChild(pastedSwatch);

    auto* resultRow = makeRow(28.0f);
    auto* resultLabel = new newui::Label();
    resultLabel->setText("Result:");
    resultLabel->setDesiredSize(newui::Size(70.0f, 24.0f));
    resultRow->addChild(resultLabel);

    auto* resultField = new newui::TextField();
    resultField->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    resultRow->addChild(resultField);

    copyTextButton->onClick.add([&root, statusLabel, sourceField](newui::Control&) {
        bool ok = newui::ClipboardManager::setText(sourceField->text(), &root);
        statusLabel->setText(ok ? "Copied text to the real system clipboard - try pasting into Notepad."
                                 : "Copy failed (clipboard busy?).");
        return newui::SyncReturn::Handled;
    });

    pasteTextButton->onClick.add([statusLabel, resultField](newui::Control&) {
        std::wstring text;
        if (newui::ClipboardManager::getText(text)) {
            resultField->setText(text);
            statusLabel->setText("Pasted plain text from the system clipboard.");
        } else {
            statusLabel->setText("Clipboard has no plain text right now.");
        }
        return newui::SyncReturn::Handled;
    });

    copyCustomButton->onClick.add([&root, statusLabel, sourceField, customFormat](newui::Control&) {
        bool ok = newui::ClipboardManager::setCustomData(customFormat, toUtf8Bytes(sourceField->text()), &root);
        statusLabel->setText(ok ? "Copied to a custom format only this app understands (not plain text)."
                                 : "Copy failed (clipboard busy?).");
        return newui::SyncReturn::Handled;
    });

    pasteCustomButton->onClick.add([statusLabel, resultField, customFormat](newui::Control&) {
        std::vector<std::uint8_t> bytes;
        if (newui::ClipboardManager::getCustomData(customFormat, bytes)) {
            resultField->setText(fromUtf8Bytes(bytes));
            statusLabel->setText("Pasted from the custom format.");
        } else {
            statusLabel->setText("Clipboard has no data in the custom format right now.");
        }
        return newui::SyncReturn::Handled;
    });

    copyDelayedButton->onClick.add([&root, statusLabel, sourceField](newui::Control&) {
        // Captured by value - the renderer may run long after this
        // click, possibly after sourceField's own text has changed
        // again, so it needs its own snapshot rather than reading
        // sourceField live.
        std::wstring snapshot = sourceField->text();
        bool ok = newui::ClipboardManager::setDelayedRenderer(
            CF_UNICODETEXT,
            [snapshot]() {
                printf("Rendering delayed clipboard text now: \"%ls\"\n", snapshot.c_str());
                fflush(stdout);
                std::vector<std::uint8_t> bytes((snapshot.size() + 1) * sizeof(wchar_t));
                std::memcpy(bytes.data(), snapshot.c_str(), bytes.size());
                return bytes;
            },
            &root);
        statusLabel->setText(ok ? "Registered a delayed renderer for CF_UNICODETEXT - nothing rendered yet. "
                                   "Paste anywhere (e.g. Notepad) to see it render on demand - watch the console."
                                 : "Delayed renderer registration failed (no live window?).");
        return newui::SyncReturn::Handled;
    });

    copyImageButton->onClick.add([&root, statusLabel, sourceImage](newui::Control&) {
        bool ok = newui::ClipboardManager::setImage(sourceImage, &root);
        statusLabel->setText(ok ? "Copied the swatch image to the real system clipboard as CF_DIB - try pasting into Paint."
                                 : "Copy failed (clipboard busy?).");
        return newui::SyncReturn::Handled;
    });

    pasteImageButton->onClick.add([statusLabel, pastedSwatch](newui::Control&) {
        BLImage pasted;
        if (newui::ClipboardManager::getImage(pasted)) {
            pastedSwatch->style().setBackgroundImage(pasted);
            statusLabel->setText("Pasted an image from the system clipboard.");
        } else {
            statusLabel->setText("Clipboard has no image (CF_DIB) right now.");
        }
        return newui::SyncReturn::Handled;
    });

    // Deferred to onStart, not called directly here: focusing sourceField
    // starts its TextField caret blinking via RunLoop::postDelayed(),
    // which throws (RunLoop::checkCalledFromLoopThread(), runloop.cpp) if
    // called before run() has actually started pumping this thread's
    // message queue - true right now, still inside main(), before
    // app.run() below. onStart fires right as that pump begins, so the
    // same call is safe there.
    app.runLoop().onStart.add([&root, sourceField](newui::RunLoop&) {
        root.setFocusedSubView(sourceField);
        return newui::SyncReturn::Handled;
    });

    app.run();

    return 0;
}

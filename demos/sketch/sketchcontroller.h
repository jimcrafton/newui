#pragma once

#include "sketchdocument.h"

#include <newui/controllers.h>
#include <newui/delegate.h>
#include <newui/frame.h>

#include <string>

namespace sketch {

    // sketch's DocumentController: the File menu's New / Open / Save / Save As and the "save changes?"
    // question, bound to the one SketchDocument and the window that shows it. newui's DocumentController
    // supplies the logic; this adds SVG file filters, the owner window for the dialogs, and events the
    // window listens to (status text, title, a replaced document).
    // @reflect ignore=true
    class SketchDocumentController : public newui::DocumentController {
    public:
        SketchDocumentController(SketchDocument& document, newui::Frame& frame);

        SketchDocument& document() { return document_; }

        // Each returns false when cancelled or failed (the user has already been told about a failure).
        bool newDocument();
        // An empty path asks with the Open dialog.
        bool open(const std::string& path = std::string());
        bool save();
        bool saveAs();
        // True when it is fine to close the window / leave the app: nothing unsaved, or saved/discarded.
        bool canClose();

        // The file name (or Untitled) with a * while modified - what the title bar shows after "sketch - ".
        std::string displayName() const;

        // Short progress text for the status bar ("Saved C:\a.svg").
        typedef newui::Delegate<SketchDocumentController, std::string> StatusDelegate;
        StatusDelegate onStatus;
        // After any command that may change displayName() (the title should refresh).
        typedef newui::Delegate<SketchDocumentController> StateDelegate;
        StateDelegate onStateChanged;
        // After New or Open replaced the document's content (the view should refit, etc.).
        StateDelegate onDocumentReplaced;

    protected:
        newui::UnsavedChangesChoice askUnsavedChanges(newui::Document& document) override;
        std::string chooseSavePath(newui::Document& document) override;
        std::string chooseOpenPath() override;
        void reportError(const std::string& message) override;

    private:
        SketchDocument& document_;
        newui::Frame& frame_;
    };

}

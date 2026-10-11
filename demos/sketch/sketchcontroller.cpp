#include "sketchcontroller.h"

#include <newui/dialogs.h>

#include <filesystem>

namespace sketch {

    SketchDocumentController::SketchDocumentController(SketchDocument& document, newui::Frame& frame)
        : document_(document), frame_(frame) {
        document_.onModifiedChanged += [this](newui::Document&) {
            onStateChanged(*this);
            return newui::SyncReturn::Handled;
        };
    }

    bool SketchDocumentController::newDocument() {
        if (!newInto(document_, [this]() { document_.clearObjects(); })) {
            return false;
        }
        onStatus(*this, "New document");
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::open(const std::string& path) {
        if (!openInto(document_, path)) {
            return false;
        }
        onStatus(*this, "Opened " + document_.filePath());
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::save() {
        if (!saveDocument(document_)) {
            return false;
        }
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::saveAs() {
        if (!saveDocumentAs(document_)) {
            return false;
        }
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::canClose() {
        return confirmDiscardChanges(document_);
    }

    std::string SketchDocumentController::displayName() const {
        std::string name = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).filename().string() : std::string("Untitled");
        return name + (document_.isModified() ? " *" : "");
    }

    namespace {
    newui::FileDialogOptions fileOptions() {
        newui::FileDialogOptions options;
        options.filters.push_back({"SVG Files", "*.svg"});
        options.filters.push_back({"All Files", "*.*"});
        options.defaultExtension = "svg";
        return options;
    }
    }

    newui::UnsavedChangesChoice SketchDocumentController::askUnsavedChanges(newui::Document& document) {
        std::string name = document.hasFilePath() ? std::filesystem::path(document.filePath()).filename().string() : std::string("Untitled");
        newui::DialogResult answer = newui::Dialog::showMessageBox(frame_.frameHandle(), "Save changes to " + name + "?", "sketch",
            newui::MessageBoxButtons::YesNoCancel, newui::MessageBoxIcon::Question);
        return answer == newui::DialogResult::Yes ? newui::UnsavedChangesChoice::Save
            : answer == newui::DialogResult::No ? newui::UnsavedChangesChoice::Discard
            : newui::UnsavedChangesChoice::Cancel;
    }

    std::string SketchDocumentController::chooseSavePath(newui::Document& document) {
        newui::FileDialogOptions options = fileOptions();
        options.title = "Save As";
        if (document.hasFilePath()) {
            options.defaultFileName = std::filesystem::path(document.filePath()).filename().string();
        }
        std::string path;
        newui::Dialog::showSaveFile(frame_.frameHandle(), options, path);
        return path;
    }

    std::string SketchDocumentController::chooseOpenPath() {
        newui::FileDialogOptions options = fileOptions();
        options.title = "Open";
        std::string path;
        newui::Dialog::showOpenFile(frame_.frameHandle(), options, path);
        return path;
    }

    void SketchDocumentController::reportError(const std::string& message) {
        newui::Dialog::showMessageBox(frame_.frameHandle(), message, "sketch", newui::MessageBoxButtons::Ok, newui::MessageBoxIcon::Error);
    }

}

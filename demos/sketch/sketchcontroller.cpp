#include "sketchcontroller.h"

#include "newdocumentdialog.h"

#include <newui/dialogs.h>

#include <newui/utils.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>

namespace sketch {

    SketchDocumentController::SketchDocumentController(SketchDocument& document, newui::Frame& frame)
        : document_(document), frame_(frame) {
        document_.onModifiedChanged += [this](newui::Document&) {
            onStateChanged(*this);
            return newui::SyncReturn::Handled;
        };
        std::string folder = newui::SpecialFolders::roamingAppData();
        if (!folder.empty()) {
            setRecentFilesPath(folder + "/sketch/recent.txt");
        }
    }

    // ---- Recent files ----

    void SketchDocumentController::setRecentFilesPath(const std::string& path) {
        recentPath_ = path;
        loadRecent();
        onRecentChanged(*this);
    }

    void SketchDocumentController::loadRecent() {
        recent_.clear();
        if (recentPath_.empty()) {
            return;
        }
        std::ifstream in(std::filesystem::u8path(recentPath_));
        std::string line;
        while (std::getline(in, line) && recent_.size() < kMaxRecent) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
                line.pop_back();
            }
            if (!line.empty()) {
                recent_.push_back(line);
            }
        }
    }

    void SketchDocumentController::saveRecent() const {
        if (recentPath_.empty()) {
            return;
        }
        std::error_code ignored;
        std::filesystem::create_directories(std::filesystem::u8path(recentPath_).parent_path(), ignored);
        std::ofstream out(std::filesystem::u8path(recentPath_), std::ios::trunc);
        for (const std::string& path : recent_) {
            out << path << "\n";
        }
    }

    void SketchDocumentController::remember(const std::string& path) {
        if (path.empty()) {
            return;
        }
        recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
        recent_.insert(recent_.begin(), path);
        if (recent_.size() > kMaxRecent) {
            recent_.resize(kMaxRecent);
        }
        saveRecent();
        onRecentChanged(*this);
    }

    void SketchDocumentController::forget(const std::string& path) {
        auto before = recent_.size();
        recent_.erase(std::remove(recent_.begin(), recent_.end(), path), recent_.end());
        if (recent_.size() != before) {
            saveRecent();
            onRecentChanged(*this);
        }
    }

    void SketchDocumentController::clearRecentFiles() {
        recent_.clear();
        saveRecent();
        onRecentChanged(*this);
    }

    bool SketchDocumentController::newDocument() {
        if (!confirmDiscardChanges(document_)) {
            return false;
        }
        NewDocumentModel settings;
        if (!NewDocumentDialog::run(frame_, settings)) {
            return false;
        }
        document_.clearObjects();
        document_.setPageSize(float(settings.widthPoints()), float(settings.heightPoints()));
        document_.setPpi(float(settings.ppi()));
        document_.setColorMode(settings.colorMode());
        switch (settings.background()) {
            case PageBackground::Transparent:
                document_.setPageBackground(std::nullopt);
                break;
            case PageBackground::CurrentFill:
                document_.setPageBackground(document_.defaultFill().isNone() ? SketchColor::rgb(1.0f, 1.0f, 1.0f) : document_.defaultFill().color());
                break;
            default:
                document_.setPageBackground(SketchColor::rgb(1.0f, 1.0f, 1.0f));
                break;
        }
        document_.reset();  // untitled and unmodified
        untitledName_ = settings.name();
        onStatus(*this, "New document " + untitledName_);
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::open(const std::string& path) {
        if (!openInto(document_, path)) {
            if (!path.empty() && !std::filesystem::exists(std::filesystem::u8path(path))) {
                forget(path);  // a recent file that is gone
            }
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Opened " + document_.filePath());
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::save() {
        if (!saveDocument(document_)) {
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::saveAs() {
        if (!saveDocumentAs(document_)) {
            return false;
        }
        remember(document_.filePath());
        onStatus(*this, "Saved " + document_.filePath());
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::documentSetup() {
        NewDocumentModel settings;
        PageBackground background = PageBackground::White;
        if (!document_.pageBackground()) {
            background = PageBackground::Transparent;
        } else if (document_.pageBackground()->toRgba32().value != 0xFFFFFFFFu) {
            background = PageBackground::CurrentFill;
        }
        settings.loadSetup(document_.pageWidth(), document_.pageHeight(), document_.ppi(), document_.colorMode(), background);
        if (!NewDocumentDialog::runSetup(frame_, settings)) {
            return false;
        }
        document_.setPageSize(float(settings.widthPoints()), float(settings.heightPoints()));
        document_.setPpi(float(settings.ppi()));
        switch (settings.background()) {
            case PageBackground::Transparent:
                document_.setPageBackground(std::nullopt);
                break;
            case PageBackground::CurrentFill:
                if (background != PageBackground::CurrentFill) {  // keep an existing custom color when it is unchanged
                    document_.setPageBackground(document_.defaultFill().isNone() ? SketchColor::rgb(1.0f, 1.0f, 1.0f) : document_.defaultFill().color());
                }
                break;
            default:
                document_.setPageBackground(SketchColor::rgb(1.0f, 1.0f, 1.0f));
                break;
        }
        document_.convertToColorMode(settings.colorMode());  // a no-op (and no undo step) when it is already that mode
        onStatus(*this, "Document setup changed");
        onDocumentReplaced(*this);  // refit the view to the new page
        onStateChanged(*this);
        return true;
    }

    bool SketchDocumentController::canClose() {
        return confirmDiscardChanges(document_);
    }

    std::string SketchDocumentController::displayName() const {
        std::string name = document_.hasFilePath() ? std::filesystem::path(document_.filePath()).filename().string() : untitledName_;
        return name + (document_.isModified() ? " *" : "");
    }

    namespace {
    newui::FileDialogOptions fileOptions() {
        newui::FileDialogOptions options;
        options.filters.push_back({"SVG Files", "*.svg;*.svgz"});
        options.filters.push_back({"Sketch Binary Files", "*.skb"});
        options.filters.push_back({"All Files", "*.*"});
        options.defaultExtension = "svg";
        return options;
    }
    }

    newui::UnsavedChangesChoice SketchDocumentController::askUnsavedChanges(newui::Document& document) {
        std::string name = document.hasFilePath() ? std::filesystem::path(document.filePath()).filename().string() : untitledName_;
        newui::DialogResult answer = newui::Dialog::showMessageBox(frame_.frameHandle(), "Save changes to " + name + "?", "sketch",
            newui::MessageBoxButtons::YesNoCancel, newui::MessageBoxIcon::Question);
        return answer == newui::DialogResult::Yes ? newui::UnsavedChangesChoice::Save
            : answer == newui::DialogResult::No ? newui::UnsavedChangesChoice::Discard
            : newui::UnsavedChangesChoice::Cancel;
    }

    std::string SketchDocumentController::chooseSavePath(newui::Document& document) {
        newui::FileDialogOptions options = fileOptions();
        options.title = "Save As";
        options.defaultFileName = document.hasFilePath() ? std::filesystem::path(document.filePath()).filename().string() : untitledName_ + ".svg";
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
        if (errorReporter_) {
            errorReporter_(message);
            return;
        }
        newui::Dialog::showMessageBox(frame_.frameHandle(), message, "sketch", newui::MessageBoxButtons::Ok, newui::MessageBoxIcon::Error);
    }

}

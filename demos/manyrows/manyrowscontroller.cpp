#include "manyrowscontroller.h"

#include "io/formatregistry.h"

#include <newui/dialogs.h>
#include <newui/utils.h>

#include <chrono>
#include <filesystem>

namespace manyrows {

    namespace {
        newui::FileDialogOptions fileOptions(bool forSave) {
            // "*.csv;*.tsv;*.xls" - every extension the registered formats read (or, for saving, can write).
            std::string patterns;
            for (const std::string& extension : io::FormatRegistry::extensions(forSave)) {
                patterns += (patterns.empty() ? "*" : ";*") + extension;
            }
            newui::FileDialogOptions options;
            options.filters.push_back({"Spreadsheet Files", patterns});
            options.filters.push_back({"All Files", "*.*"});
            options.defaultExtension = "csv";
            return options;
        }
    }

    namespace {
        // False for a path whose format can only be read (.xls); an unknown extension is written as CSV.
        bool canWritePath(const std::string& path) {
            const io::TableFormat* format = io::FormatRegistry::forPath(path);
            return format == nullptr || format->canWrite();
        }
    }

    ManyRowsController::ManyRowsController(ManyRowsDocument& document, newui::Frame& frame)
        : document_(document), frame_(frame) {
        document_.onModifiedChanged += [this](newui::Document&) {
            onStateChanged(*this);
            return newui::SyncReturn::Handled;
        };
    }

    bool ManyRowsController::refuseWhileLoading() {
        if (!isBusy()) {
            return false;
        }
        onStatus(*this, busyVerb() + " " + busyFileName() + " - wait for it, or cancel");
        return true;
    }

    std::string ManyRowsController::busyFileName() const {
        const std::string* path = loader_ ? &loader_->path() : saver_ ? &saver_->target() : nullptr;
        return path ? std::filesystem::path(newui::utf8ToWide(*path)).filename().string() : std::string();
    }

    float ManyRowsController::busyFraction() const {
        return loader_ ? loader_->fraction() : saver_ ? saver_->fraction() : 0.0f;
    }

    std::size_t ManyRowsController::busyRows() const {
        return loader_ ? loader_->rowsRead() : saver_ ? saver_->rowsWritten() : 0;
    }

    bool ManyRowsController::newDocument() {
        if (refuseWhileLoading() || !confirmDiscardChanges(document_)) {
            return false;
        }
        document_.model().makeBlank(kBlankRows, kBlankColumns);
        document_.reset();  // untitled and unmodified
        untitledName_ = "Untitled";
        onStatus(*this, "New blank sheet");
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool ManyRowsController::sampleData() {
        if (refuseWhileLoading() || !confirmDiscardChanges(document_)) {
            return false;
        }
        document_.model().makeGenerated();
        document_.reset();
        untitledName_ = "Sample data";
        onStatus(*this, "Sample data: 1,000,000 generated rows");
        onDocumentReplaced(*this);
        onStateChanged(*this);
        return true;
    }

    bool ManyRowsController::open(const std::string& path) {
        if (refuseWhileLoading() || !confirmDiscardChanges(document_)) {
            return false;
        }
        std::string chosen = path.empty() ? chooseOpenPath() : path;
        if (chosen.empty()) {
            return false;
        }
        sheetAsked_ = false;
        loader_ = std::make_unique<ManyRowsLoader>(chosen);
        onStatus(*this, "Loading " + busyFileName() + "...");
        onBusyStarted(*this);
        startTimer();
        return true;
    }

    void ManyRowsController::startTimer() {
        busyTimer_ = newui::RunLoop::current().postDelayed(std::chrono::milliseconds(50), [this]() { return pollBusy(); });
    }

    bool ManyRowsController::pollBusy() {
        if (!isBusy()) {
            busyTimer_ = newui::RunLoop::kInvalidTimerHandle;
            return true;   // cancelled meanwhile: stop the timer
        }
        bool finished = loader_ ? loader_->finished() : saver_->finished();
        if (isAwaitingSheet() && !sheetAsked_) {
            sheetAsked_ = true;
            onSheetNeeded(*this);
        }
        if (!finished) {
            onBusyProgress(*this);
            return false;
        }
        busyTimer_ = newui::RunLoop::kInvalidTimerHandle;   // the timer ends itself by returning true
        if (loader_) {
            finishLoad();
        } else {
            finishSave();
        }
        return true;
    }

    const std::vector<std::string>& ManyRowsController::sheetNames() const {
        static const std::vector<std::string> none;
        return isAwaitingSheet() ? loader_->sheetNames() : none;
    }

    void ManyRowsController::chooseSheet(std::size_t index) {
        if (isAwaitingSheet()) {
            loader_->chooseSheet(index);
        }
    }

    void ManyRowsController::finishLoad() {
        std::unique_ptr<ManyRowsLoader> loader = std::move(loader_);   // isLoading() is false from here on
        const std::string name = std::filesystem::path(newui::utf8ToWide(loader->path())).filename().string();
        switch (loader->state()) {
            case ManyRowsLoader::State::Done:
                if (document_.loadStaged(loader->table(), loader->path())) {
                    onStatus(*this, "Opened " + document_.filePath() + (loader->sheetName().empty() ? "" : " [" + loader->sheetName() + "]") + " - " +
                                    std::to_string(document_.loadedRows()) + " rows");
                    onDocumentReplaced(*this);
                } else {
                    showError("Could not open " + loader->path() + "\n\n" + document_.lastError());
                }
                break;
            case ManyRowsLoader::State::Failed:
                onStatus(*this, "Could not open " + name);
                showError("Could not open " + loader->path() + "\n\n" + loader->error());
                break;
            case ManyRowsLoader::State::Cancelled:
            case ManyRowsLoader::State::Running:
                onStatus(*this, "Load of " + name + " cancelled");
                break;
        }
        onBusyFinished(*this);
        onStateChanged(*this);
    }

    void ManyRowsController::finishSave() {
        std::unique_ptr<ManyRowsSaver> saver = std::move(saver_);   // isBusy() is false from here on
        const std::string name = std::filesystem::path(newui::utf8ToWide(saver->target())).filename().string();
        switch (saver->state()) {
            case ManyRowsSaver::State::Done:
                if (document_.saveStaged(saver->tempPath(), saver->target())) {
                    saver->release();   // renamed into place
                    onStatus(*this, "Saved " + document_.filePath());
                } else {
                    onStatus(*this, "Could not save " + name);
                    showError("Could not save " + saver->target() + "\n\n" + document_.lastError());
                }
                break;
            case ManyRowsSaver::State::Failed:
                onStatus(*this, "Could not save " + name);
                showError("Could not save " + saver->target() + "\n\n" + saver->error());
                break;
            case ManyRowsSaver::State::Cancelled:
            case ManyRowsSaver::State::Running:
                onStatus(*this, "Save of " + name + " cancelled");
                break;
        }
        saver.reset();   // deletes the temporary file unless it was renamed
        onBusyFinished(*this);
        onStateChanged(*this);
    }

    bool ManyRowsController::startSave(const std::string& path) {
        saver_ = std::make_unique<ManyRowsSaver>(document_.model(), path);
        onStatus(*this, "Saving " + busyFileName() + "...");
        onBusyStarted(*this);
        startTimer();
        return true;
    }

    void ManyRowsController::cancelBusy() {
        if (!isBusy()) {
            return;
        }
        if (busyTimer_ != newui::RunLoop::kInvalidTimerHandle) {
            newui::RunLoop::current().cancelDelayed(busyTimer_);
            busyTimer_ = newui::RunLoop::kInvalidTimerHandle;
        }
        const bool saving = isSaving();
        loader_.reset();   // cancels and joins the worker (quick: it checks every 256 KB / 1,024 rows)
        saver_.reset();    // ... and deletes its temporary file
        onStatus(*this, saving ? "Save cancelled" : "Load cancelled");
        onBusyFinished(*this);
        onStateChanged(*this);
    }

    bool ManyRowsController::save() {
        if (refuseWhileLoading()) {
            return false;
        }
        if (!document_.hasFilePath()) {
            return saveAs();
        }
        if (!canWritePath(document_.filePath())) {
            onStatus(*this, "This kind of file cannot be saved - choose a .csv name");
            return saveAs();
        }
        return startSave(document_.filePath());
    }

    bool ManyRowsController::saveAs() {
        if (refuseWhileLoading()) {
            return false;
        }
        std::string path = chooseSavePath(document_);
        return !path.empty() && startSave(path);
    }

    bool ManyRowsController::canClose() {
        cancelBusy();   // leaving the app (or the window) abandons a load in progress
        return confirmDiscardChanges(document_);
    }

    std::string ManyRowsController::displayName() const {
        std::string name = document_.hasFilePath() ? std::filesystem::path(newui::utf8ToWide(document_.filePath())).filename().string() : untitledName_;
        return name + (document_.isModified() ? " *" : "");
    }

    newui::UnsavedChangesChoice ManyRowsController::askUnsavedChanges(newui::Document& document) {
        std::string name = document.hasFilePath() ? std::filesystem::path(newui::utf8ToWide(document.filePath())).filename().string() : untitledName_;
        newui::DialogResult answer = newui::Dialog::showMessageBox(frame_.frameHandle(), "Save changes to " + name + "?", "manyrows",
            newui::MessageBoxButtons::YesNoCancel, newui::MessageBoxIcon::Question);
        return answer == newui::DialogResult::Yes ? newui::UnsavedChangesChoice::Save
            : answer == newui::DialogResult::No ? newui::UnsavedChangesChoice::Discard
            : newui::UnsavedChangesChoice::Cancel;
    }

    std::string ManyRowsController::chooseSavePath(newui::Document& document) {
        newui::FileDialogOptions options = fileOptions(true);
        options.title = "Save As";
        std::filesystem::path current(newui::utf8ToWide(document.filePath()));
        options.defaultFileName = document.hasFilePath() ? current.filename().string() : untitledName_ + ".csv";
        if (document.hasFilePath() && !canWritePath(document.filePath())) {
            options.defaultFileName = current.stem().string() + ".csv";   // an opened .xls is saved as CSV
        }
        std::string path;
        newui::Dialog::showSaveFile(frame_.frameHandle(), options, path);
        return path;
    }

    std::string ManyRowsController::chooseOpenPath() {
        newui::FileDialogOptions options = fileOptions(false);
        options.title = "Open";
        std::string path;
        newui::Dialog::showOpenFile(frame_.frameHandle(), options, path);
        return path;
    }

    void ManyRowsController::reportError(const std::string& message) {
        // The document knows the reason behind a generic "Could not open ..." - append it.
        std::string full = message;
        if (!document_.lastError().empty()) {
            full += "\n\n" + document_.lastError();
        }
        showError(full);
    }

    void ManyRowsController::showError(const std::string& message) {
        if (errorReporter_) {
            errorReporter_(message);
            return;
        }
        newui::Dialog::showMessageBox(frame_.frameHandle(), message, "manyrows", newui::MessageBoxButtons::Ok, newui::MessageBoxIcon::Error);
    }

}

#pragma once

#include "manyrowsdocument.h"
#include "manyrowsloader.h"
#include "manyrowssaver.h"

#include <newui/controllers.h>
#include <newui/delegate.h>
#include <newui/frame.h>
#include <newui/runloop.h>

#include <functional>
#include <memory>
#include <vector>
#include <string>

namespace manyrows {

    // manyrows' DocumentController: File > New / Open / Save / Save As and the "save changes?" question,
    // bound to the one ManyRowsDocument and the window showing it. newui's DocumentController supplies the
    // logic; this adds the file dialogs (filters built from the io format registry), the owner window, and
    // events the window listens to (status text, title, replaced content). Same shape as sketch's controller.
    // @reflect ignore=true
    class ManyRowsController : public newui::DocumentController {
    public:
        // New starts a blank sheet of this size.
        static constexpr std::size_t kBlankRows = 100;
        static constexpr std::size_t kBlankColumns = 26;

        ManyRowsController(ManyRowsDocument& document, newui::Frame& frame);

        ManyRowsDocument& document() { return document_; }

        // Each returns false when cancelled or failed (the user has already been told about a failure).
        // New: asks to save changes, then starts a blank sheet.
        bool newDocument();
        // Back to the generated one-million-row sample table (asks to save changes first).
        bool sampleData();
        // Save / Save As write on a worker thread too (same progress events); a document with no path yet goes
        // to Save As. They return true when the save STARTED. (The "save changes?" question on New / Open /
        // Exit saves synchronously - it must finish before the command continues.)
        // Starts opening a file: asks to save changes first, asks with the Open dialog when `path` is empty,
        // then reads the file on a worker thread (ManyRowsLoader) while the window stays responsive. Returns
        // true when the load STARTED (false: cancelled, or another load is running). Progress arrives through
        // onLoadProgress; the end - success, failure or cancel - through onLoadFinished, which also reports
        // errors and fires onDocumentReplaced on success. Must be called once the run loop is running.
        bool open(const std::string& path = std::string());

        // True while a file is being read or written on a worker thread. New / Open / Save / Save As / Sample
        // Data are refused meanwhile (the status line says why), and the content on screen must not be edited.
        bool isBusy() const { return loader_ != nullptr || saver_ != nullptr; }
        bool isLoading() const { return loader_ != nullptr; }
        bool isSaving() const { return saver_ != nullptr; }
        // 0..1 of the running operation (0 when none), the rows done so far, "Loading" / "Saving", and the file.
        float busyFraction() const;
        std::size_t busyRows() const;
        std::string busyVerb() const { return isSaving() ? "Saving" : "Loading"; }
        std::string busyFileName() const;
        // A multi-sheet workbook is being opened and the worker waits for the user's choice: the names to pick
        // from, and the answer (cancelBusy() declines). onSheetNeeded fires once when that begins.
        bool isAwaitingSheet() const { return loader_ != nullptr && loader_->state() == ManyRowsLoader::State::AwaitingSheet; }
        const std::vector<std::string>& sheetNames() const;
        void chooseSheet(std::size_t index);

        // Abandons the running load or save (a load leaves the old content; a save leaves the old file).
        // Safe to call when nothing is running.
        void cancelBusy();
        bool save();
        bool saveAs();
        // True when it is fine to close the window / leave the app: nothing unsaved, or saved/discarded.
        bool canClose();

        // The file name (or the untitled name) with a * while modified - what the title bar shows.
        std::string displayName() const;

        // Short progress text for the status bar ("Saved C:\a.csv").
        typedef newui::Delegate<ManyRowsController, std::string> StatusDelegate;
        StatusDelegate onStatus;
        // After any command that may change displayName() (the title should refresh).
        typedef newui::Delegate<ManyRowsController> StateDelegate;
        StateDelegate onStateChanged;
        // A load or save began / reported new progress / ended (read isBusy(), busyFraction(), busyRows()).
        StateDelegate onBusyStarted;
        StateDelegate onBusyProgress;
        StateDelegate onBusyFinished;
        // The file being opened has several sheets (see isAwaitingSheet()); ask the user, then chooseSheet().
        StateDelegate onSheetNeeded;
        // After New, Open or Sample replaced the content (undo history and selection should reset).
        StateDelegate onDocumentReplaced;

        // Replaces the error message box (for tests and headless runs); an empty function restores it.
        void setErrorReporter(std::function<void(const std::string&)> reporter) { errorReporter_ = std::move(reporter); }

    protected:
        newui::UnsavedChangesChoice askUnsavedChanges(newui::Document& document) override;
        std::string chooseSavePath(newui::Document& document) override;
        std::string chooseOpenPath() override;
        void reportError(const std::string& message) override;

    private:
        // The timer's job: report progress, and finish the operation once the worker has.
        bool pollBusy();
        void finishLoad();
        void finishSave();
        void startTimer();
        // Writes the document to `path` on a worker thread.
        bool startSave(const std::string& path);
        // True (after saying so in the status line) when a load is running and the command must wait.
        bool refuseWhileLoading();
        void showError(const std::string& message);

        ManyRowsDocument& document_;
        std::unique_ptr<ManyRowsLoader> loader_;
        std::unique_ptr<ManyRowsSaver> saver_;
        bool sheetAsked_ = false;   // onSheetNeeded fired for the running load
        newui::RunLoop::TimerHandle busyTimer_ = newui::RunLoop::kInvalidTimerHandle;
        newui::Frame& frame_;
        std::string untitledName_ = "Sample data";
        std::function<void(const std::string&)> errorReporter_;
    };

}

#include "manyrowswindow.h"

#include "menubinding.h"
#include "textformat.h"

#include <newui/keyboard_constants.h>
#include <newui/rootview.h>
#include <newui/runloop.h>

#include <stdexcept>

namespace manyrows {

    namespace {
        template <typename T>
        T* find(newui::Frame& frame, const char* name) {
            return dynamic_cast<T*>(frame.rootView().findView(name));
        }
    }

    ManyRowsWindow::ManyRowsWindow(newui::Frame& frame) : frame_(frame) {
        table_ = find<TimedTableView>(frame, "table");
        auto* status = find<newui::StatusBar>(frame, "statusBar");
        if (table_ == nullptr || status == nullptr) {
            throw std::runtime_error("ManyRowsWindow::ManyRowsWindow: manyrows.newui is missing the table or the status bar");
        }
        menuBar_ = find<newui::MenuBar>(frame, "menuBar");

        // Items are built by class name through reflection (CellItem - column-aware typography).
        table_->controller().setDefaultItemClassName("CellItem");
        auto ownedModel = std::make_unique<ManyRowsModel>();
        model_ = ownedModel.get();
        table_->setModel(std::move(ownedModel));

        // The cells.
        spreadsheet_ = std::make_unique<SpreadsheetController>(*table_, *model_);
        spreadsheet_->setStatusBar(status);
        spreadsheet_->setFormulaBar(find<newui::TextField>(frame, "nameBox"), find<newui::TextField>(frame, "formulaField"));
        spreadsheet_->setEditMenu(menuBar_);
        spreadsheet_->setViewMenu(menuBar_);
        spreadsheet_->setDataMenu(menuBar_);
        filterStrip_ = std::make_unique<FilterStrip>(frame, *spreadsheet_, *model_);
        bindMenuItem(menuBar_, "Data", "Filter...", [this](newui::MenuItem&) { filterStrip_->show(); });
        toolbar_ = std::make_unique<ManyRowsToolbar>(frame, *spreadsheet_, [this]() { filterStrip_->show(); });

        // The file behind them, and how the two meet.
        document_ = std::make_unique<ManyRowsDocument>(*model_);
        files_ = std::make_unique<ManyRowsController>(*document_, frame);
        // Cells are not edited while a file is loading or saving.
        spreadsheet_->setEditGuard([this]() {
            return files_->isBusy() ? files_->busyVerb() + " " + files_->busyFileName() + " - editing resumes when it finishes" : std::string();
        });
        // Any change to the cells (typing, clearing, undo, redo) makes the file modified; undoing back to the
        // saved step makes it clean again.
        spreadsheet_->onEdited += [this](SpreadsheetController& sheet) {
            if (sheet.editor().isAtSavedState() && !model_->isSorted()) {
                document_->markUnmodified();
            } else {
                document_->markModified();
            }
            return newui::SyncReturn::Handled;
        };
        // A save keeps the sort order, so sorting makes the document modified (a filter is only a view).
        spreadsheet_->onViewChanged += [this](SpreadsheetController&) {
            if (model_->isSorted()) {
                document_->markModified();
            }
            return newui::SyncReturn::Handled;
        };
        // Whenever the document becomes clean (saved, loaded, undone back) the history's current step is the saved one.
        document_->onModifiedChanged += [this](newui::Document&) {
            if (!document_->isModified()) {
                spreadsheet_->editor().markSaved();
            }
            return newui::SyncReturn::Handled;
        };
        // New content: the old undo history, selection and formula bar no longer apply.
        files_->onDocumentReplaced += [this](ManyRowsController&) {
            spreadsheet_->documentReplaced();
            filterStrip_->refresh();   // the new file has other columns
            return newui::SyncReturn::Handled;
        };
        files_->onStatus += [this](ManyRowsController&, std::string text) {
            spreadsheet_->say(text);
            return newui::SyncReturn::Handled;
        };

        bindTitle();
        bindFileMenu();
        bindProgressStrip();
        bindSheetPicker();

        // File shortcuts (the editing ones are SpreadsheetController's).
        table_->onKeyDown += [this](newui::View&, std::uint32_t keyMask, int, int, std::uint32_t vkey) {
            bool ctrl = (keyMask & newui::kmCtrl) != 0;
            bool shift = (keyMask & newui::kmShift) != 0;
            if (ctrl && vkey == newui::vkLetterL) { filterStrip_->show(); return newui::SyncReturn::Handled; }
            if (ctrl && vkey == newui::vkLetterN) { files_->newDocument(); return newui::SyncReturn::Handled; }
            if (ctrl && vkey == newui::vkLetterO) { files_->open(); return newui::SyncReturn::Handled; }
            if (ctrl && vkey == newui::vkLetterS) {
                if (shift) { files_->saveAs(); } else { files_->save(); }
                return newui::SyncReturn::Handled;
            }
            return newui::SyncReturn::Ignored;
        };
        frame_.onClosing += [this](newui::Frame&, bool& canClose) {
            canClose = files_->canClose();
            return newui::SyncReturn::Handled;
        };

        spreadsheet_->say("Click a cell; F2, double-click or type to edit; Del clears; Ctrl+Z / Ctrl+Y undo / redo; Shift extends the selection");
    }

    void ManyRowsWindow::openOnStart(const std::string& path) {
        newui::RunLoop::current().onStart += [this, path](newui::RunLoop&) {
            files_->open(path);   // starts the worker and its progress timer, which need the running loop
            return newui::SyncReturn::Handled;
        };
    }

    // The title bar follows the file name (a * while modified).
    void ManyRowsWindow::bindTitle() {
        files_->onStateChanged += [this](ManyRowsController&) {
            frame_.setTitle("manyrows - " + files_->displayName());
            return newui::SyncReturn::Handled;
        };
        frame_.setTitle("manyrows - " + files_->displayName());
    }

    void ManyRowsWindow::bindFileMenu() {
        bindMenuItem(menuBar_, "File", "New", [this](newui::MenuItem&) { files_->newDocument(); });
        bindMenuItem(menuBar_, "File", "Open...", [this](newui::MenuItem&) { files_->open(); });
        bindMenuItem(menuBar_, "File", "Save", [this](newui::MenuItem&) { files_->save(); });
        bindMenuItem(menuBar_, "File", "Save As...", [this](newui::MenuItem&) { files_->saveAs(); });
        bindMenuItem(menuBar_, "File", "Sample Data (1M rows)", [this](newui::MenuItem&) { files_->sampleData(); });
        bindMenuItem(menuBar_, "File", "Exit", [this](newui::MenuItem&) {
            if (files_->canClose()) {
                newui::RunLoop::current().quit();
            }
        });
    }

    // Loading and saving run on a worker thread; the strip above the status bar shows how far it is and can cancel it.
    void ManyRowsWindow::bindProgressStrip() {
        loadStrip_ = frame_.rootView().findView("loadStrip");
        loadLabel_ = find<newui::Label>(frame_, "loadLabel");
        loadProgress_ = find<newui::Progress>(frame_, "loadProgress");
        files_->onBusyStarted += [this](ManyRowsController&) {
            if (loadStrip_ != nullptr) {
                loadStrip_->setVisible(true);
            }
            showProgress();
            return newui::SyncReturn::Handled;
        };
        files_->onBusyProgress += [this](ManyRowsController&) {
            showProgress();
            return newui::SyncReturn::Handled;
        };
        files_->onBusyFinished += [this](ManyRowsController&) {
            if (sheetStrip_ != nullptr) {
                sheetStrip_->setVisible(false);
            }
            if (loadStrip_ != nullptr) {
                loadStrip_->setVisible(false);
            }
            spreadsheet_->showTotals();
            return newui::SyncReturn::Handled;
        };
        if (auto* cancel = find<newui::Button>(frame_, "loadCancel")) {
            cancel->onClick += [this](newui::Control&) {
                files_->cancelBusy();
                return newui::SyncReturn::Handled;
            };
        }
    }

    void ManyRowsWindow::showProgress() {
        float fraction = files_->busyFraction();
        if (loadProgress_ != nullptr) {
            loadProgress_->setValue(fraction);
        }
        if (loadLabel_ != nullptr) {
            loadLabel_->setText(files_->busyVerb() + " " + files_->busyFileName() + " - " + std::to_string(int(fraction * 100.0f)) + "%  (" +
                                withCommas(files_->busyRows()) + " rows)");
        }
        spreadsheet_->setPaneText(SpreadsheetController::TotalPane, withCommas(files_->busyRows()) + " rows...");
    }

    // A workbook with several sheets: the strip swaps to a picker until the user chooses (or cancels).
    void ManyRowsWindow::bindSheetPicker() {
        sheetStrip_ = frame_.rootView().findView("sheetStrip");
        sheetLabel_ = find<newui::Label>(frame_, "sheetLabel");
        sheetCombo_ = find<newui::DropDownList>(frame_, "sheetCombo");
        files_->onSheetNeeded += [this](ManyRowsController&) {
            if (sheetStrip_ == nullptr || sheetCombo_ == nullptr) {
                files_->chooseSheet(0);   // no picker in this layout: the first sheet
                return newui::SyncReturn::Handled;
            }
            auto items = std::make_unique<newui::StringListModel>();
            for (const std::string& name : files_->sheetNames()) {
                items->addItem(name);
            }
            sheetCombo_->setModel(std::move(items));
            sheetCombo_->setSelectedIndex(std::size_t(0));
            if (sheetLabel_ != nullptr) {
                sheetLabel_->setText(files_->busyFileName() + " has " + std::to_string(files_->sheetNames().size()) + " sheets - open:");
            }
            if (loadStrip_ != nullptr) {
                loadStrip_->setVisible(false);
            }
            sheetStrip_->setVisible(true);
            return newui::SyncReturn::Handled;
        };
        if (auto* open = find<newui::Button>(frame_, "sheetOpen")) {
            open->onClick += [this](newui::Control&) {
                std::optional<std::size_t> picked = sheetCombo_ != nullptr ? sheetCombo_->selectedIndex() : std::nullopt;
                if (sheetStrip_ != nullptr) {
                    sheetStrip_->setVisible(false);
                }
                if (loadStrip_ != nullptr) {
                    loadStrip_->setVisible(true);
                }
                files_->chooseSheet(picked.has_value() ? *picked : 0);
                return newui::SyncReturn::Handled;
            };
        }
        if (auto* cancel = find<newui::Button>(frame_, "sheetCancel")) {
            cancel->onClick += [this](newui::Control&) {
                files_->cancelBusy();   // also hides the picker (onBusyFinished)
                return newui::SyncReturn::Handled;
            };
        }
    }

}

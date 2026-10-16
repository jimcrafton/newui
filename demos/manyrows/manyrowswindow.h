#pragma once

#include "manyrowscontroller.h"
#include "manyrowsdocument.h"
#include "manyrowsmodel.h"
#include "spreadsheetcontroller.h"
#include "timedtableview.h"

#include <newui/controls.h>
#include <newui/frame.h>
#include <newui/menus.h>

#include <memory>
#include <string>

namespace manyrows {

    // The full manyrows window: the one place that knows both halves of the app and connects them to the
    // views of manyrows.newui.
    //
    //   SpreadsheetController   the cells: editing, undo, clipboard, fill, formula bar, status panes, Edit menu
    //   ManyRowsController      the file: New / Open / Save, loading and saving on worker threads, save prompts
    //
    // Neither knows the other. This class lets them meet through their delegates - an edit marks the document
    // modified, a replaced document resets the undo history, a running load or save locks editing - and binds
    // what only a window can: the title bar, the File menu and its shortcuts, the progress and sheet-picker
    // strips, and closing the window. main() builds one of these, hands it a file to open if asked, and runs.
    // @reflect ignore=true
    class ManyRowsWindow {
    public:
        // Finds the views by name in the loaded frame, gives the table its million generated rows and wires
        // everything. Throws std::runtime_error naming what manyrows.newui lacks (table, statusBar).
        explicit ManyRowsWindow(newui::Frame& frame);
        ManyRowsWindow(const ManyRowsWindow&) = delete;
        ManyRowsWindow& operator=(const ManyRowsWindow&) = delete;

        TimedTableView& table() { return *table_; }
        ManyRowsModel& model() { return *model_; }
        SpreadsheetController& spreadsheet() { return *spreadsheet_; }
        ManyRowsController& files() { return *files_; }

        // Opens `path` once the run loop is running (the load's worker and progress timer need it).
        void openOnStart(const std::string& path);

    private:
        void bindTitle();
        void bindFileMenu();
        void bindProgressStrip();
        void bindSheetPicker();
        void showProgress();

        newui::Frame& frame_;
        newui::MenuBar* menuBar_ = nullptr;
        TimedTableView* table_ = nullptr;           // owned by the frame's view tree
        ManyRowsModel* model_ = nullptr;            // owned by the table
        std::unique_ptr<SpreadsheetController> spreadsheet_;
        std::unique_ptr<ManyRowsDocument> document_;
        std::unique_ptr<ManyRowsController> files_;

        // The strips above the status bar (any may be missing from a layout).
        newui::View* loadStrip_ = nullptr;
        newui::Label* loadLabel_ = nullptr;
        newui::Progress* loadProgress_ = nullptr;
        newui::View* sheetStrip_ = nullptr;
        newui::Label* sheetLabel_ = nullptr;
        newui::DropDownList* sheetCombo_ = nullptr;
    };

}

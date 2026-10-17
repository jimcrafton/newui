#pragma once

#include "spreadsheetcontroller.h"

#include <newui/controls.h>
#include <newui/frame.h>

#include <functional>

namespace manyrows {

    // The toolbar under the menu bar of the full app (see mockup/manyrows-mockup.html):
    //
    //   [undo] [redo] | [copy] [paste] [fill down] | [sort A-Z] [sort Z-A] [filter] | [Go to]   (icon buttons)
    //
    // Every button is a shortcut for a command that also has a menu item and a key, and runs it through
    // SpreadsheetController, so there is still one code path per command. Undo and Redo enable and disable
    // themselves from the editor's history. Buttons are found by name (tbUndo, tbRedo, tbCopy, tbPaste,
    // tbFillDown, tbSortAsc, tbSortDesc, tbFilter, tbGoTo); a layout may leave any out.
    // @reflect ignore=true
    class ManyRowsToolbar {
    public:
        // `showFilter` opens the filter strip (the toolbar does not know about it).
        ManyRowsToolbar(newui::Frame& frame, SpreadsheetController& sheet, std::function<void()> showFilter);
        ManyRowsToolbar(const ManyRowsToolbar&) = delete;
        ManyRowsToolbar& operator=(const ManyRowsToolbar&) = delete;

    private:
        void syncHistory();

        SpreadsheetController& sheet_;
        newui::Control* undo_ = nullptr;
        newui::Control* redo_ = nullptr;
    };

}

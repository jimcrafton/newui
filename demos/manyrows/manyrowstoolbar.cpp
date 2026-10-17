#include "manyrowstoolbar.h"

#include <newui/rootview.h>

namespace manyrows {

    ManyRowsToolbar::ManyRowsToolbar(newui::Frame& frame, SpreadsheetController& sheet, std::function<void()> showFilter) : sheet_(sheet) {
        auto find = [&frame](const char* name) { return dynamic_cast<newui::Control*>(frame.rootView().findView(name)); };
        auto bind = [&find](const char* name, std::function<void()> action) {
            if (newui::Control* button = find(name)) {
                button->onClick += [action](newui::Control&) {
                    action();
                    return newui::SyncReturn::Handled;
                };
            }
        };
        undo_ = find("tbUndo");
        redo_ = find("tbRedo");
        bind("tbUndo", [this]() { sheet_.undo(); });
        bind("tbRedo", [this]() { sheet_.redo(); });
        bind("tbCopy", [this]() { sheet_.copy(); });
        bind("tbPaste", [this]() { sheet_.paste(); });
        bind("tbFillDown", [this]() { sheet_.fillDown(); });
        bind("tbSortAsc", [this]() { sheet_.sortActiveColumn(true); });
        bind("tbSortDesc", [this]() { sheet_.sortActiveColumn(false); });
        bind("tbFilter", showFilter);
        bind("tbGoTo", [this]() { sheet_.focusNameBox(); });

        sheet_.onHistoryChanged += [this](SpreadsheetController&) {
            syncHistory();
            return newui::SyncReturn::Handled;
        };
        syncHistory();
    }

    void ManyRowsToolbar::syncHistory() {
        if (undo_ != nullptr) {
            undo_->setEnabled(sheet_.editor().canUndo());
        }
        if (redo_ != nullptr) {
            redo_->setEnabled(sheet_.editor().canRedo());
        }
    }

}

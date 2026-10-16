#pragma once

#include "manyrowsmodel.h"
#include "spreadsheetcontroller.h"

#include <newui/controls.h>
#include <newui/frame.h>

namespace manyrows {

    // The strip under the formula bar where a filter is built rule by rule (Data > Filter..., Ctrl+L):
    //
    //   Show rows where [column v] [operator v] [value      ] [Add] [Remove last] [Clear all]        [Close]
    //   Region = North and Qty > 50 - 19,845 of 1,000,000 rows
    //
    // Add appends the rule to the model's filter and applies it at once; rules are ANDed. Close only hides the
    // strip - the filter stays until Clear all. It talks to the table only through SpreadsheetController
    // (setFilters, clearFilters), so every filter change takes the same path as the keyboard shortcuts, and it
    // listens to onViewChanged to keep its summary right however the filter changed. A layout without the
    // strip (the lite app) simply has no FilterStrip; one that lacks some of its views gets a strip that does
    // nothing.
    // @reflect ignore=true
    class FilterStrip {
    public:
        // Finds filterStrip, filterColumn, filterOp, filterValue, filterAdd, filterRemoveLast, filterClear,
        // filterClose and filterSummary by name in the frame.
        FilterStrip(newui::Frame& frame, SpreadsheetController& sheet, ManyRowsModel& model);
        FilterStrip(const FilterStrip&) = delete;
        FilterStrip& operator=(const FilterStrip&) = delete;

        // False when the layout has no strip (show() then does nothing).
        bool isAvailable() const { return strip_ != nullptr && column_ != nullptr && op_ != nullptr; }

        // Shows the strip with the column list current and the active cell's column preselected, and puts the
        // cursor in the value field.
        void show();
        void hide();
        // The model's columns changed (a file was opened): refill the column list and the summary.
        void refresh();

    private:
        void fillColumns();
        void add();
        void removeLast();
        void updateSummary();
        void updateValueField();

        newui::Frame& frame_;
        SpreadsheetController& sheet_;
        ManyRowsModel& model_;
        newui::View* strip_ = nullptr;
        newui::DropDownList* column_ = nullptr;
        newui::DropDownList* op_ = nullptr;
        newui::TextField* value_ = nullptr;
        newui::Label* summary_ = nullptr;
    };

}

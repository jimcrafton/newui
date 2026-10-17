#include "filterstrip.h"

#include "textformat.h"

#include <newui/rootview.h>
#include <newui/utils.h>

namespace manyrows {

    namespace {
        template <typename T>
        T* find(newui::Frame& frame, const char* name) {
            return dynamic_cast<T*>(frame.rootView().findView(name));
        }

        constexpr FilterRule::Op kOps[] = {FilterRule::Op::Equals, FilterRule::Op::NotEquals, FilterRule::Op::Contains,
            FilterRule::Op::NotContains, FilterRule::Op::Greater, FilterRule::Op::GreaterOrEqual, FilterRule::Op::Less,
            FilterRule::Op::LessOrEqual, FilterRule::Op::Blank, FilterRule::Op::NotBlank};

        // The column's title, or its spreadsheet letters when the table has none.
        std::string columnName(const ManyRowsModel& model, std::size_t column) {
            std::string title = model.titleText(column);
            if (!title.empty()) {
                return title;
            }
            for (std::size_t n = column + 1; n > 0; n = (n - 1) / 26) {
                title.insert(title.begin(), char('A' + (n - 1) % 26));
            }
            return title;
        }
    }

    FilterStrip::FilterStrip(newui::Frame& frame, SpreadsheetController& sheet, ManyRowsModel& model)
        : frame_(frame), sheet_(sheet), model_(model) {
        strip_ = frame.rootView().findView("filterStrip");
        column_ = find<newui::DropDownList>(frame, "filterColumn");
        op_ = find<newui::DropDownList>(frame, "filterOp");
        value_ = find<newui::TextField>(frame, "filterValue");
        summary_ = find<newui::Label>(frame, "filterSummary");
        if (!isAvailable()) {
            return;
        }

        auto items = std::make_unique<newui::StringListModel>();
        for (FilterRule::Op op : kOps) {
            items->addItem(FilterRule::opText(op));
        }
        op_->setModel(std::move(items));
        op_->setSelectedIndex(std::size_t(0));
        op_->onSelectionChanged += [this](newui::DropDownList&) {
            updateValueField();
            return newui::SyncReturn::Handled;
        };
        fillColumns();

        auto button = [&frame](const char* name, std::function<void()> action) {
            if (auto* b = find<newui::Button>(frame, name)) {
                b->onClick += [action](newui::Control&) {
                    action();
                    return newui::SyncReturn::Handled;
                };
            }
        };
        button("filterAdd", [this]() { add(); });
        button("filterRemoveLast", [this]() { removeLast(); });
        button("filterClear", [this]() { sheet_.clearFilters(); });
        button("filterClose", [this]() { hide(); });
        if (value_ != nullptr) {
            value_->onReturnPressed += [this](newui::TextField&) {
                add();
                return newui::SyncReturn::Handled;
            };
        }
        // However the filter changed (this strip, Ctrl+Shift+F, Clear Filters, a new document) the summary follows.
        sheet_.onViewChanged += [this](SpreadsheetController&) {
            updateSummary();
            return newui::SyncReturn::Handled;
        };
        updateSummary();
    }

    void FilterStrip::fillColumns() {
        auto items = std::make_unique<newui::StringListModel>();
        for (std::size_t c = 0; c < model_.columnCount(); ++c) {
            items->addItem(columnName(model_, c));
        }
        column_->setModel(std::move(items));
        column_->setSelectedIndex(std::size_t(0));
    }

    void FilterStrip::show() {
        if (!isAvailable()) {
            return;
        }
        fillColumns();
        if (std::optional<std::size_t> active = sheet_.activeColumn()) {
            column_->setSelectedIndex(*active);
        }
        updateSummary();
        updateValueField();
        strip_->setVisible(true);
        if (value_ != nullptr) {
            if (auto* root = value_->rootView()) {
                root->setFocusedSubView(value_);
            }
        }
    }

    void FilterStrip::hide() {
        if (strip_ != nullptr) {
            strip_->setVisible(false);
        }
        sheet_.focusTable();
    }

    void FilterStrip::refresh() {
        if (!isAvailable()) {
            return;
        }
        fillColumns();
        updateSummary();
    }

    void FilterStrip::add() {
        std::optional<std::size_t> column = column_->selectedIndex();
        std::optional<std::size_t> op = op_->selectedIndex();
        if (!column.has_value() || !op.has_value() || *op >= sizeof(kOps) / sizeof(kOps[0])) {
            return;
        }
        std::string text = value_ != nullptr ? newui::wideToUtf8(value_->text()) : std::string();
        std::vector<FilterRule> rules = model_.filters();
        rules.push_back(FilterRule(*column, kOps[*op], FilterRule::needsValue(kOps[*op]) ? text : std::string()));
        sheet_.setFilters(std::move(rules));
    }

    void FilterStrip::removeLast() {
        std::vector<FilterRule> rules = model_.filters();
        if (rules.empty()) {
            return;
        }
        rules.pop_back();
        sheet_.setFilters(std::move(rules));
    }

    // "Region = North and Qty > 50 - 19,845 of 1,000,000 rows", or a hint when there is no filter.
    void FilterStrip::updateSummary() {
        if (summary_ == nullptr) {
            return;
        }
        std::string text;
        for (const FilterRule& rule : model_.filters()) {
            text += (text.empty() ? "" : " and ") + rule.describe(columnName(model_, rule.column));
        }
        if (text.empty()) {
            summary_->setText("No filter - add a rule to show only the rows that match it (rules are combined with AND)");
        } else {
            summary_->setText(text + " - " + withCommas(model_.rowCount()) + " of " + withCommas(model_.sourceRowCount()) + " rows");
        }
    }

    // "is blank" / "is not blank" take no value.
    void FilterStrip::updateValueField() {
        if (value_ == nullptr || !op_->selectedIndex().has_value()) {
            return;
        }
        std::size_t op = *op_->selectedIndex();
        bool needsValue = op < sizeof(kOps) / sizeof(kOps[0]) ? FilterRule::needsValue(kOps[op]) : true;
        value_->controller().inputTraits().setReadOnly(!needsValue);
        if (!needsValue) {
            value_->setText(L"");
        }
    }

}

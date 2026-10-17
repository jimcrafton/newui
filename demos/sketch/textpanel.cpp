#include "textpanel.h"

#include <newui/fontmanager.h>
#include <newui/models.h>
#include <newui/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sketch {

    namespace {
        // 12.5 -> "12.5", 24 -> "24" (up to two decimals, trailing zeros dropped).
        std::wstring numberText(double value) {
            char buffer[48];
            std::snprintf(buffer, sizeof(buffer), "%.2f", value);
            std::string text = buffer;
            while (!text.empty() && text.back() == '0') { text.pop_back(); }
            if (!text.empty() && text.back() == '.') { text.pop_back(); }
            if (text == "-0" || text.empty()) { text = "0"; }
            return newui::utf8ToWide(text);
        }

        bool parseNumber(const std::wstring& text, double& value) {
            std::string narrow = newui::wideToUtf8(text);
            char* end = nullptr;
            double parsed = std::strtod(narrow.c_str(), &end);
            if (end == narrow.c_str() || !std::isfinite(parsed)) {
                return false;
            }
            value = parsed;
            return true;
        }
    }

    TextPanel::TextPanel(newui::RootView& root, SketchDocument& document) : root_(root), document_(document) {
        font_ = dynamic_cast<newui::DropDownList*>(root_.findView("textFontCombo"));
        size_ = dynamic_cast<newui::TextField*>(root_.findView("textSizeField"));
        tracking_ = dynamic_cast<newui::TextField*>(root_.findView("textTrackingField"));
        leading_ = dynamic_cast<newui::TextField*>(root_.findView("textLeadingField"));
        bold_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("textBoldButton"));
        italic_ = dynamic_cast<newui::ToolbarButton*>(root_.findView("textItalicButton"));
        const char* alignNames[3] = {"textAlignLeftButton", "textAlignCenterButton", "textAlignRightButton"};
        for (int i = 0; i < 3; ++i) {
            align_[i] = dynamic_cast<newui::ToolbarButton*>(root_.findView(alignNames[i]));
        }

        // Every font the system offers, once each and in order.
        for (const newui::SystemFontInfo& info : newui::FontManager::listFonts()) {
            fonts_.push_back(info.name);
        }
        std::sort(fonts_.begin(), fonts_.end());
        fonts_.erase(std::unique(fonts_.begin(), fonts_.end()), fonts_.end());
        if (font_ != nullptr) {
            auto model = std::make_unique<newui::StringListModel>();
            model->items() = fonts_;
            font_->setModel(std::move(model));
            font_->onSelectionChanged += [this](newui::DropDownList&) {
                applyFont();
                return newui::SyncReturn::Handled;
            };
        }
        if (size_ != nullptr) {
            size_->onReturnPressed += [this](newui::TextField&) {
                applySize();
                return newui::SyncReturn::Handled;
            };
        }
        if (tracking_ != nullptr) {
            tracking_->onReturnPressed += [this](newui::TextField&) {
                double value;
                if (!updating_ && parseNumber(tracking_->text(), value)) {
                    applyStyle("Tracking", [value](TextStyle& style) { style.tracking = std::max(-50.0, std::min(500.0, value)); });
                }
                refresh();
                return newui::SyncReturn::Handled;
            };
        }
        if (leading_ != nullptr) {
            leading_->onReturnPressed += [this](newui::TextField&) {
                double value;
                if (!updating_ && parseNumber(leading_->text(), value)) {
                    applyStyle("Leading", [value](TextStyle& style) { style.leading = std::max(0.2, std::min(10.0, value / 100.0)); });
                }
                refresh();
                return newui::SyncReturn::Handled;
            };
        }
        auto toggleHandler = [this](newui::ToolbarButton* button, const char* what, std::function<void(TextStyle&, bool)> set) {
            if (button == nullptr) {
                return;
            }
            button->onCheckedChanged += [this, what, set](newui::ToolbarButton& changed) {
                if (updating_) {
                    return newui::SyncReturn::Ignored;
                }
                bool on = changed.isChecked();
                applyStyle(what, [set, on](TextStyle& style) { set(style, on); });
                refresh();
                return newui::SyncReturn::Handled;
            };
        };
        toggleHandler(bold_, "Bold", [](TextStyle& style, bool on) { style.bold = on; });
        toggleHandler(italic_, "Italic", [](TextStyle& style, bool on) { style.italic = on; });
        for (int i = 0; i < 3; ++i) {
            toggleHandler(align_[i], "Align Text", [i](TextStyle& style, bool on) {
                if (on) { style.align = i; }
            });
        }
        document_.onChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        document_.onSelectionChanged += [this](SketchDocument&) { refresh(); return newui::SyncReturn::Handled; };
        refresh();
    }

    const SketchObject* TextPanel::firstText() const {
        for (std::size_t id : document_.selection()) {
            const SketchObject* object = document_.findById(id);
            if (object != nullptr && object->isText()) {
                return object;
            }
        }
        return nullptr;
    }

    void TextPanel::refresh() {
        const SketchObject* text = firstText();
        updating_ = true;
        auto enable = [text](newui::Control* control) {
            if (control != nullptr) {
                control->setEnabled(text != nullptr);
            }
        };
        enable(font_);
        enable(size_);
        enable(tracking_);
        enable(leading_);
        enable(bold_);
        enable(italic_);
        for (newui::ToolbarButton* button : align_) {
            enable(button);
        }
        if (text != nullptr) {
            const TextStyle& style = text->textStyle();
            if (font_ != nullptr) {
                std::string name = text->fontName().empty() ? defaultTextFontName() : text->fontName();
                auto it = std::find(fonts_.begin(), fonts_.end(), name);
                if (it != fonts_.end()) {
                    std::optional<std::size_t> index = std::size_t(it - fonts_.begin());
                    if (font_->selectedIndex() != index) {
                        font_->setSelectedIndex(*index);
                    }
                }
            }
            if (size_ != nullptr) { size_->setText(numberText(text->fontSize())); }
            if (tracking_ != nullptr) { tracking_->setText(numberText(style.tracking)); }
            if (leading_ != nullptr) { leading_->setText(numberText(style.leading * 100.0)); }
            if (bold_ != nullptr) { bold_->setChecked(style.bold); }
            if (italic_ != nullptr) { italic_->setChecked(style.italic); }
            for (int i = 0; i < 3; ++i) {
                if (align_[i] != nullptr) { align_[i]->setChecked(style.align == i); }
            }
        }
        updating_ = false;
    }

    void TextPanel::applyFont() {
        if (updating_ || font_ == nullptr || !font_->selectedIndex().has_value() || *font_->selectedIndex() >= fonts_.size()) {
            return;
        }
        const std::string name = fonts_[*font_->selectedIndex()];
        document_.changeSelectionText([name](SketchObject& object) { object.setFontName(name); }, "Text Font");
    }

    void TextPanel::applySize() {
        double value;
        if (!updating_ && size_ != nullptr && parseNumber(size_->text(), value) && value > 0.0) {
            value = std::max(1.0, std::min(1000.0, value));
            document_.changeSelectionText([value](SketchObject& object) { object.setFontSize(value); }, "Text Size");
        }
        refresh();
    }

    void TextPanel::applyStyle(const char* what, const std::function<void(TextStyle&)>& change) {
        document_.changeSelectionText([&change](SketchObject& object) {
            TextStyle style = object.textStyle();
            change(style);
            object.setTextStyle(style);
        }, what);
    }

}

#pragma once

#include "sketchdocument.h"

#include <newui/controllers.h>
#include <newui/controls.h>
#include <newui/rootview.h>

#include <string>
#include <vector>

namespace sketch {

    // The Text panel of sketch.newui: the font, size, weight / slant, alignment, tracking and leading of the selected text. It shows the
    // first selected Text object's values and applies a change to every selected Text object as one undo step. Disabled when the
    // selection holds no text.
    //
    // A newui::Controller that, like the other controllers here, does not call Controller::setModel(): the document is observed
    // through a plain reference and must outlive it.
    // @reflect ignore=true
    class TextPanel : public newui::Controller {
    public:
        TextPanel(newui::RootView& root, SketchDocument& document);
        ~TextPanel() override = default;
        TextPanel(const TextPanel&) = delete;
        TextPanel& operator=(const TextPanel&) = delete;

        // Re-reads the selected text into the controls.
        void refresh();

    private:
        void applyFont();
        void applySize();
        void applyStyle(const char* what, const std::function<void(TextStyle&)>& change);
        const SketchObject* firstText() const;

        newui::RootView& root_;
        SketchDocument& document_;
        std::vector<std::string> fonts_;
        newui::DropDownList* font_ = nullptr;
        newui::TextField* size_ = nullptr;
        newui::TextField* tracking_ = nullptr;
        newui::TextField* leading_ = nullptr;
        newui::ToolbarButton* bold_ = nullptr;
        newui::ToolbarButton* italic_ = nullptr;
        newui::ToolbarButton* align_[3] = {nullptr, nullptr, nullptr};
        bool updating_ = false;   // refresh() is writing the controls: their events are not edits
    };

}

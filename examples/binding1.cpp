// Data binding (binding.h). The whole binding setup - which control shows which value, how it is
// converted, which rules make it valid, the cross-field "passwords match" rule - is declared in
// binding1.newui. This file only supplies the data ("form", a Model of values by key) and OK/Revert.
//   - Name, age and the passwords are bound OnCommit: tab out of a field and, if the value is valid, it
//     is written to the model; if not, the field is outlined in red and the model keeps its old value.
//   - OK commits the whole "form" group at once: nothing is written unless every field and the
//     cross-field rule pass, and the reasons are listed in the status line.
//   - Revert reloads every field from the model, dropping what you typed.
//   - The volume label follows the slider (control to control binding) with no code at all.

#include "newui/newui.h"
#include "newui/win32/winheaders.h"
#include "newui/application.h"
#include "newui/binding.h"
#include "newui/bundle.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/models.h"
#include "newui/rootview.h"

#include <cstdio>
#include <map>
#include <string>

extern void registerReflectionData();

namespace {

// A Model holding one value per string key.
class FormModel : public newui::Model {
public:
    std::any value(const std::any& key = std::any()) override {
        const std::string* k = std::any_cast<std::string>(&key);
        auto it = values.find(k != nullptr ? *k : std::string());
        return it != values.end() ? it->second : std::any();
    }
    void setValue(const std::any& newValue, const std::any& key = std::any()) override {
        const std::string* k = std::any_cast<std::string>(&key);
        values[k != nullptr ? *k : std::string()] = newValue;
        newui::Model::setValue(newValue, key);
    }
    std::map<std::string, std::any> values;
};

}

int main() {
    std::printf("newui %s - binding1: data binding declared in a .newui file\n", newui::version());
    registerReflectionData();

    newui::Frame frame;
    newui::Application& app = newui::Application::instance();
    app.setName("binding1");
    app.setFrame(&frame);
    frame.setName("binding1");

    FormModel form;
    form.values["name"] = std::string("Ada Lovelace");
    form.values["age"] = 36;
    form.values["password"] = std::string("analytical");
    // Sources are registered before loadFrame(), which resolves the bindings once the tree is built.
    frame.setBindingSource("form", form);

    if (!newui::Bundle::instance().loadFrame(frame)) {
        std::printf("Could not load Resources/binding1.newui - see examples/CMakeLists.txt.\n");
        return 1;
    }

    newui::RootView& root = frame.rootView();
    auto* status = dynamic_cast<newui::Label*>(root.findView("status"));
    auto* ok = dynamic_cast<newui::Button*>(root.findView("okButton"));
    auto* revert = dynamic_cast<newui::Button*>(root.findView("cancelButton"));
    if (status == nullptr || ok == nullptr || revert == nullptr) {
        std::printf("binding1.newui is missing status/okButton/cancelButton.\n");
        return 1;
    }

    newui::BindingGroup& group = frame.bindingGroup("form");

    ok->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&](newui::Control&) -> newui::SyncReturn {
            if (group.commit()) {
                status->setText("Saved: " + std::any_cast<std::string>(form.values["name"]) + ", age "
                                + std::to_string(std::any_cast<int>(form.values["age"])));
            }
            else {
                std::string text;
                for (const std::string& error : group.errors()) {
                    text += error + "\n";
                }
                status->setText(text);
            }
            return newui::SyncReturn::Handled;
        }));

    revert->onClick.add(std::function<newui::SyncReturn(newui::Control&)>(
        [&](newui::Control&) -> newui::SyncReturn {
            group.revert();
            status->setText("Reverted");
            return newui::SyncReturn::Handled;
        }));

    app.run();
    return 0;
}

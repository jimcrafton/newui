// Component-level settings (componentsettings.h): a Component marks some of its reflected properties
// as settings with assignSetting(), registers with Application::settings(), and those values are
// restored on the next run and stored again when the frame closes.
//   - A Splitter's divider position, a Slider's value and a Toggle's checked state are persisted.
//     Drag the divider, move the slider, tick the box, close the window, then start the example again:
//     everything comes back as you left it.
//   - The slider uses SettingScope::Machine, so it is shared by every user of the computer (stored
//     under %PROGRAMDATA%); the rest are per-user (%APPDATA%). If %PROGRAMDATA% is not writable the
//     machine-wide file simply isn't saved.
//   - Pass --reset to delete the stored files before starting; the paths are printed at startup.
//   - setAutosaveInterval() also stores every few seconds, so settings survive a crash.

#include "newui/newui.h"
#include "newui/win32/winheaders.h"
#include "newui/application.h"
#include "newui/componentsettings.h"
#include "newui/controls.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/splitter.h"
#include "newui/subview.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

int main(int argc, char** argv) {
    newui::Frame frame;
    newui::Application& app = newui::Application::instance();
    app.setName("settings1");
    app.setFrame(&frame);

    frame.setTitle("Settings 1 - close and relaunch to see values restored");
    frame.setBounds(newui::Rect(10, 10, 800, 400));

    newui::SettingsManager& settings = app.settings();
    std::printf("user settings:    %s\nmachine settings: %s\n",
                settings.settingsFilePath(newui::SettingScope::User).c_str(),
                settings.settingsFilePath(newui::SettingScope::Machine).c_str());

    if (argc > 1 && std::strcmp(argv[1], "--reset") == 0) {
        for (auto scope : {newui::SettingScope::User, newui::SettingScope::Machine}) {
            std::error_code ec;
            std::filesystem::remove(std::filesystem::u8path(settings.settingsFilePath(scope)), ec);
        }
        std::printf("settings files removed\n");
    }

    newui::RootView& root = frame.rootView();

    auto* splitter = new newui::Splitter(newui::Orientation::Horizontal);
    splitter->setSplitPosition(200.0f);  // default, used until a stored value exists
    root.addChild(splitter);

    auto* left = new newui::SubView();
    left->setVisible(true);
    splitter->addChild(left);

    auto* right = new newui::SubView();
    right->setVisible(true);
    auto rightLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rightLayout->setSpacing(12.0f);
    right->setLayout(std::move(rightLayout));
    splitter->addChild(right);

    auto* valueLabel = new newui::Label();
    valueLabel->setDesiredSize(newui::Size(0.0f, 24.0f));
    right->addChild(valueLabel);

    auto* slider = new newui::Slider();
    slider->setInteger(true);
    slider->setRange(0.0f, 100.0f);
    slider->setValue(25.0f);
    slider->setDesiredSize(newui::Size(0.0f, 24.0f));
    slider->onValueChanged.add(std::function<newui::SyncReturn(newui::Slider&)>(
        [valueLabel](newui::Slider& s) -> newui::SyncReturn {
            valueLabel->setText("Slider (machine-wide setting): " + std::to_string(static_cast<int>(s.value())));
            return newui::SyncReturn::Handled;
        }));
    right->addChild(slider);

    auto* toggle = new newui::Toggle();
    toggle->setDesiredSize(newui::Size(16.0f, 16.0f));
    right->addChild(toggle);

    // Mark the properties as settings, then register: registering applies the stored values right away
    // (so the slider label above updates through onValueChanged), and the frame closing stores them.
    splitter->assignSetting("splitPosition");
    slider->assignSetting("value", newui::SettingScope::Machine);
    toggle->assignSetting("checked");

    settings.registerComponent(*splitter, "mainSplitter");
    settings.registerComponent(*slider, "slider");
    settings.registerComponent(*toggle, "toggle");

    settings.setAutosaveInterval(std::chrono::seconds(5));

    app.run();
    return 0;
}

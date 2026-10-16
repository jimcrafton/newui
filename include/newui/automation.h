#pragma once

#include "newui/newui.h"

#include <string>

namespace newui {

class RunLoop;
class RootView;

// In-process automation for GUI tests: answers newline-delimited JSON requests about the
// view tree so an out-of-process driver can aim real input (SendInput) at named views and
// check what the app shows. Protocol: see automation.cpp.
//
// Opt-in on three counts: it is its own library (newui_automation) that only test builds
// link, the app must call expose() for each window a driver may see, and the app must call
// start() itself. Wrap all of that in the app's own #ifdef so a shipping build has none of it:
//
//   #ifdef APP_AUTOMATION
//       newui::Automation::expose(rootView);
//       newui::Automation::start(pipeName, runLoop);   // pipeName from a command-line option
//   #endif
class Automation {
public:
    static constexpr int kProtocolVersion = 1;

    // Makes a window's views reachable by name. Forgotten automatically when it is destroyed.
    static void expose(RootView& root);
    static void unexpose(RootView& root);

    // Answers one request. Call on the thread that owns the views (the UI loop's).
    static std::string handle(const std::string& requestJson);

    // Serves \\.\pipe\<pipeName> on a background thread; every request is run on loop's thread.
    static bool start(const std::string& pipeName, RunLoop& loop);

    static void stop();
};

}

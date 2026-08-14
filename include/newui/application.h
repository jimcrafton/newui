#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include "newui/color.h"
#include "newui/newui.h"
#include "newui/delegate.h"
#include "newui/runloop.h"
#include "newui/uicomponent.h"

namespace newui {

    class Frame; // Forward declaration of Frame class

class Application : public UIComponent {
public:
    static Application& instance();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;


    typedef Delegate<Application> ApplicationDelegate;
    typedef Delegate<Application, const std::string&, const std::any&> ApplicationConfigDelegate;

    // canEnd starts true; a subscriber that sets it false vetoes ending
    // the session (WM_QUERYENDSESSION's own answer to Windows - subject
    // to Windows' own caveats around ignoring app vetoes for some
    // shutdown/restart paths, same as any native app's). reasonFlags is
    // the raw ENDSESSION_* bitmask from lParam (ENDSESSION_CLOSEAPP/
    // ENDSESSION_CRITICAL/ENDSESSION_LOGOFF).
    typedef Delegate<Application, bool&, std::uint32_t> QueryEndSessionDelegate;

    // Fired after WM_QUERYENDSESSION has been answered (by every top-
    // level window, not just this app) - ending is false if the session
    // turns out not to be ending after all (e.g. another app vetoed it).
    // reasonFlags is the same ENDSESSION_* bitmask as above.
    typedef Delegate<Application, bool, std::uint32_t> EndSessionDelegate;

    // color is WM_DWMCOLORIZATIONCOLORCHANGED's wParam, converted via
    // Color(uint32_t, hasAlpha=true) - that constructor already expects
    // exactly the 0xAARRGGBB layout DWM hands back, so no manual channel
    // unpacking is needed (and no risk of misreading it as a Win32
    // COLORREF, which is a different byte order, 0x00BBGGRR). isOpaqueBlend
    // is lParam's opacity flag.
    typedef Delegate<Application, Color, bool> ColorizationColorChangedDelegate;

    // action is WM_SETTINGCHANGE's wParam (usually 0 - most callers only
    // ever set the SPI_* action when they mean SystemParametersInfo, not
    // this message); settingName is lParam's string, e.g.
    // "ImmersiveColorSet" for a light/dark mode toggle - empty if lParam
    // was null.
    typedef Delegate<Application, std::uint32_t, std::string> SettingChangeDelegate;



    void setName(const std::string& name);
    std::string getName() const {
        return name_;
    }


    void setFrame(Frame* frame);
    Frame* getFrame() {
        return frame_;
    }
    const Frame* getFrame() const {
        return frame_;
    }

    void run();

    // The RunLoop run() pumps. Exposed so code that needs to hook into
    // idle time (e.g. AnimationManager::run()) can do so before calling
    // Application::run() - postIdle()'d tasks queue up regardless of
    // whether the loop has started pumping yet, so registering early is
    // safe (see RunLoop::postIdle()'s comment).
    RunLoop& runLoop() {
        return runLoop_;
    }

    HINSTANCE instanceHandle() const {
        return instanceHandle_;
    }

	HWND dummyWindowHandle() const {
		return dummyWindowHandle_;
	}

    // Open-ended bag for whatever app-specific data a caller wants to
    // persist alongside window state - e.g. "lastOpenedFile". Written/
    // read as a nested "custom" object of strings by writeFields()/
    // readFields() below. name_ (the dummy-window Win32 class-name
    // component) is deliberately not part of that - it's a startup
    // identity set by app code (setName()), not restorable session state.
    void setCustomValue(const std::string& key, const std::string& value) {
        customValues_[key] = value;
    }

    bool getCustomValue(const std::string& key, std::string& outValue) const {
        auto it = customValues_.find(key);
        if (it == customValues_.end()) {
            return false;
        }
        outValue = it->second;
        return true;
    }

    const std::unordered_map<std::string, std::string>& customValues() const {
        return customValues_;
    }

    // UIComponent: just the custom-data bag above.
    void writeFields(json5::builder& w) const override;
    void readFields(const json5::value& obj) override;



    ApplicationDelegate onStartup;
    ApplicationConfigDelegate onConfigOption;
    ApplicationDelegate onShutdownStarted;
    ApplicationDelegate onShutdown;

    // The five below all arrive via Frame::handleMessage() - Windows
    // broadcasts each to every top-level window, and Frame's is the one
    // this toolkit treats as "the" application window (same convention
    // RunLoop::run()'s own WM_KEYDOWN handling already relies on via
    // getFrame()) - so each fires exactly once here per broadcast,
    // regardless of how many Frames exist.
    QueryEndSessionDelegate onQueryEndSession;
    EndSessionDelegate onEndSession;

    // WM_THEMECHANGED - fired after Frame has already refreshed every
    // live RootView (dropped cached HTHEME handles, redrawn) - see
    // RootView::refreshThemes(). Subscribe here only for additional
    // app-specific reactions; the redraw itself needs no handler.
    ApplicationDelegate onThemeChanged;

    // WM_DWMCOLORIZATIONCOLORCHANGED - same redraw-already-happened
    // ordering as onThemeChanged above.
    ColorizationColorChangedDelegate onColorizationColorChanged;

    // WM_SETTINGCHANGE - fires for every setting Windows reports this
    // way, but Frame only redraws automatically (same "drop cached
    // HTHEMEs, redraw" treatment as onThemeChanged/
    // onColorizationColorChanged above) when settingName is
    // "ImmersiveColorSet" specifically - that's what a light/dark mode
    // toggle actually sends (not WM_THEMECHANGED, which is for switching
    // between whole .theme files, a different, less common setting). For
    // any other settingName, this fires with no redraw - a subscriber
    // that cares reacts itself.
    SettingChangeDelegate onSettingChange;

private:
    Application();
    ~Application();

    static std::atomic<bool> instantiated_;

    mutable std::mutex mutex_;
    std::string name_;
    Frame* frame_ = nullptr;
	HWND dummyWindowHandle_ = nullptr;
    HINSTANCE instanceHandle_ = nullptr;
    RunLoop runLoop_;
    std::unordered_map<std::string, std::string> customValues_;

    static LRESULT CALLBACK DummyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    SyncReturn runLoopEnding(RunLoop&);
    SyncReturn runLoopDone(RunLoop&);

};

}

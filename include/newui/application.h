#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include "newui/newui.h"
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

};

}

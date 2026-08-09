#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "newui/newui.h"
#include "newui/runloop.h"

namespace newui {

    class Frame; // Forward declaration of Frame class

class Application {
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

    static LRESULT CALLBACK DummyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

};

}

#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include "newui/newui.h"

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

    static LRESULT CALLBACK DummyWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

};

}

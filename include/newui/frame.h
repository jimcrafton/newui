#pragma once

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/rootview.h>
#include <newui/uicomponent.h>
#include <tuple>


namespace newui {

class Frame : public UIComponent {
public:
    Frame();
    ~Frame();

	typedef Delegate<Frame, std::string, std::string> TitleChangedDelegate;
		
	typedef Delegate<Frame, const Size&> SizeChangedDelegate;
	typedef Delegate<Frame, const Point&> PosChangedDelegate;
	typedef Delegate<Frame> CreatedDelegate;
	typedef Delegate<Frame> DestroyedDelegate;
	typedef Delegate<Frame> ClosedDelegate;
	

    void setTitle(const std::string& title);
	std::string getTitle() const {
		return title_;
	}

	bool initialize();

	void setBounds(const Rect& bounds);

	const Rect& getBounds() const {
		return bounds_;
	}

	std::tuple<Frame*, RootView*> getTarget(HWND hwnd) ;

	TitleChangedDelegate onTitleChanged;
	
	CreatedDelegate onCreated;
	DestroyedDelegate onDestroyed;
	ClosedDelegate onClosed;
	SizeChangedDelegate onSizeChanged;
	PosChangedDelegate onPosChanged;

	HWND frameHandle() const {
		return frameHandle_;
	}


	RootView& getView() {
		return *rootView_;
	}

	const RootView& getView() const {
		return *rootView_;
	}

	// UIComponent: title_/bounds_ directly (plain data members, no live
	// HWND needed - works even before initialize() creates frameHandle_).
	// Also queries/restores the live show-state (normal/maximized/
	// minimized) via WINDOWPLACEMENT - that half needs a live
	// frameHandle_, so readFields() should only be called once
	// initialize() has run; see Frame::onCreated for the recommended
	// integration point.
	void writeFields(json5::builder& w) const override;
	void readFields(const json5::value& obj) override;

    private:
    std::string title_;
	Rect bounds_;


	
	
	RootView* rootView_ = nullptr;

	HWND frameHandle_ = nullptr;
	WNDPROC defaultWndProc_ = nullptr;
	WNDPROC wndProc_ = nullptr;

	bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT);

	static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	bool frameCreated();
	void destroy();
	void sizeChange(const Size& newSize);
	void updateViewBounds();
};

}

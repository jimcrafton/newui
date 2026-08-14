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

	// Forces a fresh paint of the whole rootView_ tree (so this always
	// reflects current state, not whatever was last drawn before some
	// change - e.g. a Light/Dark mode toggle) and dumps the result to
	// path via BLImage::write_to_file() - codec picked from path's own
	// extension (".png"/".bmp"/".qoi", whatever blend2d has a codec
	// for), same inference saveViewTreeToFile() and friends leave to
	// their own file extensions (serialization.h). Returns false if
	// there's no live window yet (rootView_'s backing buffer is only
	// created by RootView::resizeImageBuffer(), itself only reachable
	// once a real HWND exists - see Frame::initialize()) or the encode/
	// write itself fails (bad extension, unwritable path, ...).
	bool renderAllViewsToFile(const std::string& path);

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

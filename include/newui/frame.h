#pragma once

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/rootview.h>
#include <tuple>


namespace newui {

class Frame {
public:
    Frame();
    virtual ~Frame();

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

	virtual bool initialize();

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
	// for). Returns false if there's no live window yet (rootView_'s
	// backing buffer is only created by RootView::resizeImageBuffer(),
	// itself only reachable once a real HWND exists - see
	// Frame::initialize()) or the encode/write itself fails (bad
	// extension, unwritable path, ...).
	bool renderAllViewsToFile(const std::string& path);

    protected:
	// Dispatches through the vtable from the static WndProc below, so a
	// derived class (PopupFrame) overriding this gets called automatically
	// with no need for its own WndProc - see WndProc's own comment.
	virtual bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT);

	// Reused as-is by PopupFrame::initialize() as its own wcex.lpfnWndProc -
	// this already does nothing Frame-specific beyond the GWLP_USERDATA/
	// CREATESTRUCT dance and dispatching to thisPtr->handleMessage(...),
	// which resolves virtually, so a PopupFrame registered with this same
	// function still gets PopupFrame::handleMessage() called.
	static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

	// Both just wrap rootView_->initialize()/destroy() - nothing about
	// either is specific to Frame's own WS_OVERLAPPEDWINDOW styling, so
	// PopupFrame's own WM_CREATE/WM_DESTROY handling reuses them directly.
	bool frameCreated();
	void destroy();

    private:
    std::string title_;
	Rect bounds_;




	RootView* rootView_ = nullptr;

	HWND frameHandle_ = nullptr;
	WNDPROC defaultWndProc_ = nullptr;
	WNDPROC wndProc_ = nullptr;

	void sizeChange(const Size& newSize);
	void updateViewBounds();
};

}

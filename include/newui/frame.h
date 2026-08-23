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

	// Identifies this Frame's own saved-layout file - Bundle::loadFrame()/
	// loadDialog()/loadRootView() (bundle.h) resolve "<name>.newui" from
	// this, not getTitle() (which is the visible window caption, changes
	// at runtime, and has no filesystem-safe-character guarantee). Plain
	// storage, no side effects - unlike setTitle(), nothing observes this
	// changing.
	void setName(const std::string& name) {
		name_ = name;
	}
	std::string getName() const {
		return name_;
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

	//@reflect ignore=true
	HWND frameHandle() const {
		return frameHandle_;
	}

	// NOT reflectgen-registered as a "rootView" property, despite the bare-
	// getter/matching-backing-member shape that would normally qualify
	// (reflection.md) - RootView isn't copy-constructible (its
	// std::unique_ptr<Overlay> overlay_ deletes the implicit copy ctor),
	// and reflectgen refuses .property() for *any* non-copy-constructible
	// return type, addressable-reference getters included - see its own
	// "MSVC is_copy_constructible_v reliability" comment (reflectgen.py)
	// for why. Bundle::loadFrame() (bundle.h) reaches this directly in
	// C++ instead - frame.rootView() is always live regardless, no
	// reflection needed to get here - then reads the file's own nested
	// "rootView" node into it via ObjectReader::readNested().
	RootView& rootView() {
		return *rootView_;
	}

	const RootView& rootView() const {
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
	std::string name_;
	Rect bounds_;




	RootView* rootView_ = nullptr;

	HWND frameHandle_ = nullptr;
	WNDPROC defaultWndProc_ = nullptr;
	WNDPROC wndProc_ = nullptr;

	void sizeChange(const Size& newSize);
	void updateViewBounds();
};

}

#pragma once

#include <newui/newui.h>
#include <newui/component.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/rootview.h>
#include <tuple>


namespace newui {

class Frame;

// Marks a Frame as handling one of its window messages (its own window's, or its root view's) for as long as it lives. Used by
// the frames' window procedures: free()ing a frame in that time is a mistake. Only the pointer is kept.
// @reflect ignore=true
class FrameDispatchScope {
public:
	explicit FrameDispatchScope(const Frame* frame);
	~FrameDispatchScope();

	FrameDispatchScope(const FrameDispatchScope&) = delete;
	FrameDispatchScope& operator=(const FrameDispatchScope&) = delete;

	// True while the calling thread is inside a scope for frame.
	static bool contains(const Frame* frame);

private:
	bool entered_;
};

// A top-level window and its RootView. A Frame is a Component, so it can own others and be owned (component.h):
//
//     auto* frame = Application::instance().createFrame<Frame>();      // the application owns it
//     frame->addComponent(new MyFrameController(*frame));              // the frame owns its controller
//     auto* tool = frame->createOwned<ToolFrame>();                    // and its tool window
//     tool->initialize(frame->frameHandle());                          // the native owner is a separate matter
//
// The frame owns its root view. free() takes the whole frame down, in this order: beforeDestruction(); what the frame owns,
// last added first (owned tool and popup frames go while this window still exists); the native window, which frees what the
// root view owns and then the views (WM_DESTROY); the frame object. From inside one of the frame's own event handlers use
// release(): free() asserts there. Closing is not freeing: WM_CLOSE keeps its meaning for each kind of frame. A window destroyed
// from outside free() (the user closing it, Windows destroying it with its owner window) tears its views down, and an owned
// frame then release()s itself, so it does not outlive its window (a Dialog does not: its views stay readable until it is
// freed). A Frame that nothing owns behaves as any other object, on the stack or deleted.
//
// @reflect proxy=FrameProxy
class Frame : public Component {
public:
    Frame();
    ~Frame() override;

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

	// name() / setName() (Component) identify this Frame's own saved-layout file - Bundle::loadFrame()/loadDialog()/
	// loadRootView() (bundle.h) resolve "<name>.newui" from it, not from getTitle() (which is the visible window caption,
	// changes at runtime, and has no filesystem-safe-character guarantee).
	//@reflect ignore=true
	std::string getName() const {
		return name_;
	}

	// Creates the native window. True without doing anything if it already exists.
	bool initialize() override;

	void setBounds(const Rect& bounds);

	const Rect& getBounds() const {
		return bounds_;
	}

	std::tuple<Frame*, RootView*> getTarget(HWND hwnd) ;

	TitleChangedDelegate onTitleChanged;
	
	CreatedDelegate onCreated;
	DestroyedDelegate onDestroyed;
	ClosedDelegate onClosed;

	// Fired on WM_CLOSE (titlebar X, Alt+F4, system menu Close) before anything is torn down. A subscriber
	// that sets canClose to false vetoes it: the window stays open and onClosed does not fire - the hook for
	// a "save changes?" prompt. Starts true. Code that calls DestroyWindow directly bypasses it.
	typedef Delegate<Frame, bool&> ClosingDelegate;
	ClosingDelegate onClosing;

	SizeChangedDelegate onSizeChanged;
	PosChangedDelegate onPosChanged;

	//@reflect ignore=true
	HWND frameHandle() const {
		return frameHandle_;
	}

	// Reflectgen-registered as an addressable-only "rootView" property
	// (.property<false>(...), reflection.h's TypedProperty AssumeCopyable
	// override) - RootView isn't copy-constructible (its
	// std::unique_ptr<Overlay> overlay_ deletes the implicit copy ctor), but
	// address()/write()/read() never need to copy a ValueT the way get()/
	// set()'s by-value paths do, so the property still works for
	// ObjectReader/ObjectWriter's normal per-property walk - see
	// reflectgen.py's collect_property_accessors() for how this getter
	// shape gets picked up. Bundle::loadFrame()/writeFrame() (bundle.cpp) no
	// longer need a separate manual bridge for this because of it.
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

	// release() hides the window until it is freed.
	void aboutToBeReleased() override;
	void markDestroyingTree() override;
	// Takes the native window down before the root view is freed.
	void freeOwnedComponent(Component& component) override;
	void tearDownForFree() override;
	// Forgets a root view that was freed some other way.
	void notification(Component& component, Operation operation) override;

	// Called once the window has been destroyed from WM_DESTROY: an owned frame that free() did not take it down for
	// release()s itself. Dialog turns this off.
	virtual bool releasesWithWindow() const { return true; }

    protected:
    std::string title_;
	Rect bounds_;




	RootView* rootView_ = nullptr;

	HWND frameHandle_ = nullptr;
	WNDPROC defaultWndProc_ = nullptr;
	WNDPROC wndProc_ = nullptr;

	void sizeChange(const Size& newSize);
	void updateViewBounds();

    private:
	// From WM_DESTROY on.
	bool windowGoing_ = false;
};

}

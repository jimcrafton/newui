#pragma once

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/geometry.h>
#include <newui/rootview.h>
#include <tuple>
#include <map>
#include <string>
#include <type_traits>
#include <vector>
#include <newui/binding.h>
#include <newui/models.h>


namespace newui {

// @reflect proxy=FrameProxy
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

	// ---- Data binding (binding.h) -------------------------------------------------------------
	// A Frame owns its bindings and converters; both lists are saved to and loaded from the .newui file
	// ("bindings": [...], "converters": [...]). Bundle::loadFrame() calls resolveBindings() once the whole
	// tree is built, so register any sources first.

	// @reflect collection add=addBinding remove=removeBinding
	const std::vector<Binding*>& bindings() const { return bindings_; }
	// Takes ownership. Not resolved until the next resolveBindings() (or Binding::resolve()).
	void addBinding(Binding* binding);
	// Unbinds and detaches without deleting - the caller owns binding again.
	void removeBinding(Binding* binding);

	// @reflect collection add=addConverter remove=removeConverter
	const std::vector<Converter*>& converters() const { return converters_; }
	void addConverter(Converter* converter);
	void removeConverter(Converter* converter);

	// Groups declared in the file ("groups": [...]) - only needed for group-level rules.
	// @reflect collection add=addGroup remove=removeGroup
	const std::vector<BindingGroup*>& groups() const { return groups_; }
	// Takes ownership.
	void addGroup(BindingGroup* group);
	// The caller owns group again.
	void removeGroup(BindingGroup* group);

	// The group named name (the bindings whose Binding::group() is name; "" is the default group). Made
	// on demand if the file didn't declare it. Owned by the Frame.
	//@reflect ignore=true
	BindingGroup& bindingGroup(const std::string& name = std::string());

	// The converter whose name() is name; nullptr if none.
	//@reflect ignore=true
	const Converter* findConverter(const std::string& name) const;

	// Makes object available to bindings as source name. object is not owned and must outlive the
	// bindings that use it (or the Frame). A Model is bound by key; anything else by reflected property.
	//@reflect ignore=true
	template<typename T>
	void setBindingSource(const std::string& name, T& object) {
		BindingSource source;
		source.object = const_cast<void*>(static_cast<const void*>(&object));
		source.type = std::type_index(typeid(T));
		if constexpr (std::is_polymorphic<T>::value) {
			source.object = dynamic_cast<void*>(&object);
			source.type = std::type_index(typeid(object));
			source.component = dynamic_cast<Component*>(&object);
			source.model = dynamic_cast<Model*>(&object);
		}
		bindingSources_[name] = source;
	}
	//@reflect ignore=true
	void removeBindingSource(const std::string& name) { bindingSources_.erase(name); }
	//@reflect ignore=true
	bool findBindingSource(const std::string& name, BindingSource& out) const;

	// Resolves every binding (Binding::resolve()), reporting the ones that fail on stderr and leaving
	// them unconnected. Safe to call again, e.g. after registering a late source. True if all resolved.
	//@reflect ignore=true
	bool resolveBindings();

	void setBounds(const Rect& bounds);

	const Rect& getBounds() const {
		return bounds_;
	}

	std::tuple<Frame*, RootView*> getTarget(NativeWindowHandle hwnd) ;

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
	NativeWindowHandle frameHandle() const {
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
	virtual bool handleMessage(NativeMessage message, NativeWParam wParam, NativeLParam lParam, NativeLResult& outLRESULT);

	// Reused as-is by PopupFrame::initialize() as its own wcex.lpfnWndProc -
	// this already does nothing Frame-specific beyond the GWLP_USERDATA/
	// CREATESTRUCT dance and dispatching to thisPtr->handleMessage(...),
	// which resolves virtually, so a PopupFrame registered with this same
	// function still gets PopupFrame::handleMessage() called.
	static NativeLResult WndProc(NativeWindowHandle hWnd, NativeMessage message, NativeWParam wParam, NativeLParam lParam);

	// Both just wrap rootView_->initialize()/destroy() - nothing about
	// either is specific to Frame's own WS_OVERLAPPEDWINDOW styling, so
	// PopupFrame's own WM_CREATE/WM_DESTROY handling reuses them directly.
	bool frameCreated();
	void destroy();

    protected:
    std::string title_;
	std::string name_;
	Rect bounds_;




	RootView* rootView_ = nullptr;

	std::vector<Binding*> bindings_;
	std::vector<BindingGroup*> groups_;
	std::vector<Converter*> converters_;
	std::map<std::string, BindingSource> bindingSources_;

	NativeWindowHandle frameHandle_ = nullptr;
	NativeWindowProc defaultWndProc_ = nullptr;
	NativeWindowProc wndProc_ = nullptr;

	void sizeChange(const Size& newSize);
	void updateViewBounds();
};

}

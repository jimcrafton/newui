#pragma once

#include <memory>
#include <vector>

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/view.h>
#include <newui/geometry.h>
#include <newui/overlay.h>

namespace newui {
    class Frame;

	class SubView;

    // Heap-only, like View - see View's class comment. Construct with
    // new RootView(...), not on the stack - see Frame::rootView_ for
    // the only place this is currently done.
    class RootView : public View {
    public:
        RootView(Frame* frame, const newui::Rect& bounds, const std::string& name);
        virtual ~RootView();

        void setBounds(const Rect& bounds);
        void setVisible(bool visible);

        typedef Delegate<RootView> RedrawNeededDelegate;

        SizeChangedDelegate onSizeChanged;
        VisibilityChangedDelegate onVisibilityChanged;
        CreatedDelegate onCreated;
        DestroyedDelegate onDestroyed;

        // Fired when getImageBuffer() was just (re)created - first
        // initialize(), a resize - or when markDirty() is called explicitly.
        // Not tied to WM_PAINT: WM_PAINT just blits whatever is currently in
        // the buffer whenever Windows wants it repainted. This is for driving
        // the actual drawing (e.g. from an animation timer) independently of
        // that.
        RedrawNeededDelegate onRedrawNeeded;

        void markDirty();
        void markDirty(const View* fromView, const newui::Rect& rect);

        // Drops every ThemedViewStyle's cached HTHEME across this
        // RootView's whole tree (itself plus every descendant SubView) -
        // see ThemedViewStyle::closeTheme()'s doc comment on why that's
        // needed after a system theme change, since a stale HTHEME keeps
        // drawing the *old* theme otherwise - then forces a full repaint
        // via markDirty(), whose next paintStyle() call reopens each one
        // lazily against whatever the theme now is. Called by Frame on
        // WM_THEMECHANGED/WM_DWMCOLORIZATIONCOLORCHANGED, and on
        // WM_SETTINGCHANGE specifically when its string is
        // "ImmersiveColorSet" (see Frame::handleMessage()) - not needed
        // for plain (non-Themed) ViewStyle subclasses, which cache
        // nothing theme-related.
        //
        // Known limitation: this correctly re-fetches whatever the
        // *current* theme data is, but classic common-control theme
        // classes (BUTTON/EDIT/TRACKBAR/PROGRESS/SCROLLBAR/... - see
        // ThemedViewStyle's subclasses in viewstyle.h) have no distinct
        // dark-mode visual for OpenThemeData()/DrawThemeBackground() to
        // return in the first place - confirmed live: toggling Settings >
        // Personalization > Colors' Light/Dark mode (which sends
        // WM_SETTINGCHANGE "ImmersiveColorSet", not WM_THEMECHANGED -
        // that one's for switching between whole .theme files, a
        // different, less common setting) correctly triggers this, but
        // produces no visible change, even across a full app restart
        // while already in Dark mode. Real dark-mode rendering for these
        // controls needs a separate opt-in this toolkit doesn't do -
        // SetWindowTheme(hwnd, L"DarkMode_Explorer", ...) plus
        // AllowDarkModeForWindow/SetPreferredAppMode (uxtheme.dll
        // ordinals 133/135) - all undocumented, unsupported by Microsoft
        // outside their own apps (Explorer, Windows Terminal, ...), and
        // only covering a subset of controls even when used (Trackbar/
        // Progress may have no dark variant regardless). Out of scope for
        // now; this function still does the right thing with whatever
        // theme data actually exists.
        void refreshThemes();



		virtual bool initialize();
        virtual void destroy();

        virtual void addChild(SubView* child);
        virtual void removeChild(SubView* child);
        //@reflect ignore=true
		Frame* getFrame() const {
			return parentFrame_;
		}

        // Backing buffer for this RootView's HWND, drawn to with blend2d
        // (e.g. BLContext ctx(view.getImageBuffer());) and blitted to the
        // window's HDC on WM_PAINT. Call invalidate() after drawing to it
        // to schedule that repaint.
        BLImage& getImageBuffer() {
            return imageBuffer_;
        }

        void invalidate();

        void invalidate(const newui::Rect* invalidArea);

        //convert the invalidArea, which is in coordinate system of fromView
        //into the coordinate system of root view
        void invalidate(View* fromView, const newui::Rect* invalidArea);

        std::tuple<RootView*, SubView*> getTarget(HWND hwnd);

        // Which SubView the mouse is currently over (nullptr if none) -
        // updated on every mouse move/leave; drives onMouseEntered()/
        // onMouseLeft() and View::setHighlighted() (see
        // updateHoveredSubView()).
        SubView* hoveredSubView() const {
            return hoveredSubView_;
        }

        // Which SubView is currently receiving mouse input regardless of
        // where the cursor actually is - set by mouseDown()/
        // mouseDblClick(), cleared by mouseUp(). Standard mouse-capture
        // semantics: a drag that started on a SubView keeps delivering
        // mouseMove()/mouseUp() to it even if the cursor leaves its
        // bounds (or the window entirely - see handleMessage()'s
        // SetCapture()/ReleaseCapture() calls).
        SubView* capturedSubView() const {
            return capturedSubView_;
        }

        // Which SubView keyboard events (keyEvent()) are routed to -
        // nullptr means "just the window itself", the pre-existing
        // behavior (onKeyDown()/onKeyPress()/onKeyUp() only ever fired on
        // this RootView). Set automatically on mouseDown()/
        // mouseDblClick() (clicking a SubView focuses it, clicking empty
        // space clears it), or call this directly for programmatic focus.
        void setFocusedSubView(SubView* target);

        SubView* focusedSubView() const {
            return focusedSubView_;
        }

        // Clears hoveredSubView_/capturedSubView_/focusedSubView_ if any
        // of them is removedSubtreeRoot itself or one of its descendants -
        // called by RootView::removeChild()/SubView::removeChild() before
        // detaching a subtree, so this RootView never holds onto a
        // dangling pointer into memory that's about to be (or already
        // was) deleted. No got/lostFocus or entered/left events fire for
        // this - the view is on its way out, nothing left to safely
        // notify.
        void notifySubViewRemoved(SubView* removedSubtreeRoot);

        // The live Win32 window handle backing this RootView, or nullptr
        // before initialize() has created it. Needed by anything that has
        // to talk to a real Win32 API against this window directly - e.g.
        // ThemedViewStyle (viewstyle.h) opening an HTHEME via
        // OpenThemeData(), which requires a real HWND.
        HWND windowHandle() const {
            return viewHwnd_;
        }

        // Sums getBounds().pos() up view's own parent() chain, translating
        // it into this RootView's own local/window-client space - the same
        // accumulated-offset math paintChildren()'s ctx.translate() calls
        // perform incrementally per level, done here in one shot for an
        // arbitrary SubView (regardless of nesting depth). Used internally
        // by mouseMove()/mouseUp() to keep targeting capturedSubView_
        // correctly once the cursor is no longer over its bounds; exposed
        // publicly so callers can position something (e.g. a popup menu -
        // see MenuBar/ContextMenu, menus.h) relative to an arbitrary
        // SubView without duplicating this walk - combine with
        // localToScreen() below for real screen coordinates.
        Point accumulatedOffset(const SubView* view) const;

        // Converts a point in this RootView's own local/window-client
        // space (e.g. accumulatedOffset(view) + view's own size) to real
        // screen coordinates via ::ClientToScreen() against windowHandle() -
        // needs a live window (returns rootLocalPt unchanged if
        // windowHandle() is null).
        Point localToScreen(const Point& rootLocalPt) const;

        // Painted last, on top of every child SubView - see Overlay's own
        // class comment (overlay.h). Null (the default) means nothing
        // extra is drawn. Takes ownership of overlay, replacing (and
        // freeing) whatever was set before; pass nullptr to remove it.
        // Immediately calls overlay->viewSized() with this RootView's
        // current bounds(), so a newly-attached overlay starts in sync
        // with the current size rather than stale until the next resize.
        void setOverlay(std::unique_ptr<Overlay> overlay);

        Overlay* overlay() const {
            return overlay_.get();
        }

    protected:
        // Win32-message-driven event entry points - protected (not
        // private) purely for testability, so a test-local subclass can
        // drive them directly without a real HWND/message pump (see
        // TestableRootView in unittests/test_rootview.cpp, same pattern
        // as TestableThemedButtonStyle in test_viewstyle.cpp exposing a
        // protected method via a using-declaration). handleMessage() is
        // the only real caller in production code.
        void mouseDown(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
		void mouseMove(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        void mouseEntered(const Point& pt);
        void mouseWheel(const Point& pt, float mouseDelta, std::uint32_t btnMask, std::uint32_t keyMask);
        void mouseLeft(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        void mouseUp(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);
        void mouseDblClick(const Point& pt, std::uint32_t btnMask, std::uint32_t keyMask);

        void gotFocus();
        void lostFocus();

        void keyEvent(int eventType, std::uint32_t keyMask, int keyCharVal, int repeatCount, std::uint32_t VKeyCode);

        // Which View's cursor() should be shown for a mouse position pt
        // (in this RootView's own local/window-client space).
        // capturedSubView_ takes priority over a fresh hit-test - same
        // reasoning as mouseMove()'s dispatchTarget: a drag that started
        // on a SubView (e.g. dragging a splitter) keeps showing that
        // SubView's cursor even once the pointer leaves its bounds. Falls
        // back to this RootView itself (its own cursor()) if neither
        // applies. Protected purely for testability, like the mouseXxx()
        // methods above - handleMessage()'s WM_SETCURSOR case is the only
        // real caller, feeding it the live cursor position via
        // GetCursorPos()/ScreenToClient().
        View* cursorTargetAt(const Point& pt);


        newui::Rect fromViewToLocal(const View* fromView, const newui::Rect& rect);

    private:
	    Frame* parentFrame_ = nullptr;
		HWND viewHwnd_ = nullptr;

        // imageBuffer_ wraps a CreateDIBSection()-allocated buffer directly
        // (via BLImage::create_from_data(), not BLImage::create() - blend2d
        // pads its own allocations to a 16-byte stride for SIMD, which
        // wouldn't match the stride a DIB section infers from biWidth) -
        // memDC_/dibSection_ below, not a plain heap buffer, so
        // paintImageBufferToWindow() can BitBlt() from an already-realized
        // GDI bitmap object instead of re-describing a raw pointer via
        // StretchDIBits() on every WM_PAINT. Owning the buffer this way
        // keeps the stride at exactly width * 4 so Blend2D and GDI agree on
        // layout (guaranteed DWORD-aligned for 32bpp regardless of width,
        // so no padding to account for).
        BLImage imageBuffer_;
        HDC memDC_ = nullptr;
        HBITMAP dibSection_ = nullptr;
        // Whatever memDC_ had selected before dibSection_ - re-selected
        // before deleting dibSection_ (see releaseImageBuffer()), since
        // deleting a bitmap while it's still selected into a DC is
        // undefined behavior.
        HBITMAP dibSectionOldBitmap_ = nullptr;

        newui::Rect dirtyRect_;

        // markDirty()/markDirty(fromView, rect) no longer call
        // notifyRedrawNeeded() (the actual, expensive Blend2D repaint())
        // directly - they union into dirtyRect_ as before, then call
        // scheduleRepaint(), which posts a single one-shot RunLoop idle
        // task (does nothing if one is already pending) instead. Idle
        // tasks only run once the message queue is fully drained (see
        // RunLoop::run()'s own idle loop), so a burst of markDirty()
        // calls within the same processing pass - e.g. several
        // WM_MOUSEMOVE events from one fast Slider drag - collapses into
        // exactly one real repaint() covering their unioned dirtyRect_,
        // instead of one full repaint per call. Still same-thread,
        // synchronous-from-the-UI-thread's-perspective - just deferred
        // to "the next idle opportunity" rather than "immediately inline" -
        // Blend2D's own rendering into imageBuffer_ isn't thread-safe, so
        // this is deliberately not a background-thread/async mechanism.
        bool repaintScheduled_ = false;
        // Set false in the destructor, checked by the queued idle task
        // before it touches this RootView - a task posted via
        // scheduleRepaint() can still be sitting in RunLoop's idle queue
        // after this RootView itself is destroyed (e.g. its window
        // closing while a repaint is still pending), and idleTasks_ has
        // no mechanism to cancel a specific already-queued task. The
        // lambda captures this shared_ptr by value (extending its own
        // lifetime independently of *this*), so checking *aliveFlag_ is
        // always safe even if the RootView behind the raw `this` capture
        // is long gone.
        std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);
        void scheduleRepaint();

        void resizeImageBuffer(int width, int height);
        // Frees memDC_/dibSection_ (and resets imageBuffer_, which points
        // into dibSection_'s memory) - called at the start of
        // resizeImageBuffer() before allocating the new size, and from
        // the destructor for final cleanup. Safe to call when already
        // released (both members already null).
        void releaseImageBuffer();
        void paintImageBufferToWindow(HDC hdc, const newui::Rect& paintRect );
        void notifyRedrawNeeded();
        void repaint();

        WNDPROC defaultWndProc_ = nullptr;
        WNDPROC wndProc_ = nullptr;

        bool mouseEnteredControl_ = false;

        // Mouse/keyboard routing state - see hoveredSubView()/
        // capturedSubView()/focusedSubView() above for what each means.
        SubView* hoveredSubView_ = nullptr;
        SubView* capturedSubView_ = nullptr;
        SubView* focusedSubView_ = nullptr;

        std::unique_ptr<Overlay> overlay_;

        // Updates hoveredSubView_ to target, firing onMouseLeft()/
        // onMouseEntered() (and toggling View::setHighlighted() +
        // style().markDirty(), so hover state actually repaints) on
        // whichever of the old/new hovered views actually changed. rootPt
        // is the mouse position in this RootView's own local space - a
        // no-op if target is already the current hoveredSubView_.
        void updateHoveredSubView(SubView* target, const Point& rootPt);

        bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT);

        static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


        void viewCreated();
    };

}

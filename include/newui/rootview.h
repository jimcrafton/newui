#pragma once

#include <memory>
#include <vector>

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/view.h>
#include <newui/geometry.h>
#include <newui/namemanager.h>
#include <newui/overlay.h>
#include <newui/presentsurface.h>

namespace newui {
    class Frame;

	class SubView;

    // How much of the window RootView::repaint() re-renders each time.
    //  - Full:  the whole tree, from a blank buffer, every time. Always right, and costs ~3 us per on-screen
    //           view (see the RootViewRepaintBenchmark test); a repaint driven by a tiny change still pays
    //           for every view.
    //  - Dirty: only the region that changed (dirtyRect_, whole-pixel snapped) - blanked, then the background
    //           and just the children that overlap it painted, everything clipped to it. A small hover
    //           repaint drops to roughly the cost of the views it touches. Correct only if every change to
    //           a view's appearance invalidates the region it affects; with Full, a missed invalidation is
    //           hidden by the next repaint anywhere, with Dirty it stays wrong on screen - which is what
    //           NEWUI_VERIFY_REPAINT exists to catch (see RootView::repaintVerifyMismatches()).
    // A RootView with onRedrawNeeded listeners always repaints in full (a listener draws straight into the
    // buffer, unclipped), whatever its mode.
    //
    // @reflect ignore=true
    enum class RepaintMode {
        Full,
        Dirty
    };

    // Case-insensitive "full" / "dirty". False (out untouched) for anything else.
    bool parseRepaintMode(const std::string& text, RepaintMode& out);

    // What a newly constructed RootView uses: whatever setDefaultRepaintMode() last set, otherwise the
    // NEWUI_REPAINT environment variable ("full" or "dirty", read once), otherwise Dirty.
    RepaintMode defaultRepaintMode();
    void setDefaultRepaintMode(RepaintMode mode);

    // Whether a newly constructed RootView verifies its pruned repaints (see RepaintMode::Dirty): true if
    // the NEWUI_VERIFY_REPAINT environment variable is set to anything but "0" (read once).
    bool defaultVerifyRepaint();

    // Heap-only, like View - see View's class comment. Construct with
    // new RootView(...), not on the stack - see Frame::rootView_ for
    // the only place this is currently done.
    //
    // @reflect proxy=RootViewProxy
    class RootView : public View {
    public:
        RootView(Frame* frame, const newui::Rect& bounds, const std::string& name);

        // Standalone construction - no Frame at all, for hosting this
        // RootView's HWND directly inside a parent window some other
        // process/toolkit owns (e.g. a Visual Studio-hosted editor
        // control), used together with a caller-owned RunLoop rather
        // than Application/Frame. externalParentHwnd becomes
        // initialize()'s CreateWindowExA parent; instanceHandle becomes
        // its window class/window hInstance - both used in place of
        // parentFrame_->frameHandle()/Application::instance()
        // .instanceHandle() (see initialize()). getFrame() correctly
        // keeps returning nullptr for a RootView built this way - already
        // the safe, tested case (MenuBar's own popup code, Bundle's
        // Frame-based loadRootView() overload) - both already null-check
        // it.
        RootView(HWND externalParentHwnd, HINSTANCE instanceHandle, const newui::Rect& bounds, const std::string& name);

        virtual ~RootView();

        void setBounds(const Rect& bounds) override;
        void setVisible(bool visible) override;

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
        // that. Fires at the start of every repaint(), right after the buffer
        // has been blanked and before the tree paints - so a handler must
        // draw what it wants to keep on *every* call; nothing survives from
        // the previous repaint.
        RedrawNeededDelegate onRedrawNeeded;

        void markDirty();
        void markDirty(const View* fromView, const newui::Rect& rect);

        // Repaints this RootView's entire tree into getImageBuffer() and
        // calls presentRepaintedBuffer(), synchronously, right now -
        // unlike markDirty() (whose actual repaint is deferred to the
        // next RunLoop idle pass via scheduleRepaint()). Ordinary WM_PAINT-
        // presented RootViews rarely need this - Windows itself eventually
        // asks for a repaint (WM_PAINT) regardless of how markDirty()'s
        // own deferral is timed. Two cases do:
        //  - A PopupTool (popuptool.h): its own present() only
        //    composites/pushes whatever's *already* in getImageBuffer(), and
        //    nothing else ever asks this RootView to actually repaint that
        //    buffer - addChild() alone doesn't (see PopupTool::addChild()'s
        //    own override, which calls this after the base call) - so a
        //    child added after the fact wouldn't show up until some
        //    unrelated event happened to call markDirty() on its own.
        //  - Feedback during a modal loop the RunLoop isn't pumping - most
        //    importantly Windows' own drag-and-drop loop (DoDragDrop, entered
        //    from a mouse handler): RunLoop idle tasks don't run until it
        //    ends, so markDirty() from a DropTarget::onTextDragOver handler
        //    would only repaint after the drag is over. Follow with
        //    ::UpdateWindow(windowHandle()) to also flush the WM_PAINT.
        void repaintNow();

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

        // wndClassFlags seeds WNDCLASSEXA::style, windowStyleFlags seeds
        // CreateWindowExA's dwStyle, windowExStyleFlags seeds its
        // dwExStyle (e.g. WS_EX_LAYERED | WS_EX_TOPMOST for a PopupTool -
        // popuptool.h) - all three are passed in already set to
        // RootView::initialize()'s own defaults (see rootview.cpp), so an
        // override only needs to touch whichever of the three it actually
        // wants to change.
        virtual void preCreateHints(size_t& wndClassFlags, size_t& windowStyleFlags, size_t& windowExStyleFlags);


		virtual bool initialize() override;

        virtual void postCreate();

        // Pixel format resizeImageBuffer() (rootview.cpp) allocates
        // getImageBuffer() at - BL_FORMAT_XRGB32 (opaque, alpha ignored)
        // by default, matching every ordinary WM_PAINT-presented RootView
        // (paintImageBufferToWindow()'s BitBlt() doesn't care about an
        // alpha channel that isn't there). Override to BL_FORMAT_PRGB32
        // for a RootView whose own SubView tree needs real per-pixel
        // alpha in its painted output - e.g. PopupTool (popuptool.h),
        // which composites getImageBuffer() over its own Underlay
        // background rather than presenting it directly.
        virtual BLFormat imageBufferFormat() const;

        // Called at the very end of repaint(), once this RootView's tree
        // has actually finished writing this frame into getImageBuffer() -
        // unlike onRedrawNeeded (fired at the *start* of repaint(), for
        // content that wants to draw into the buffer ahead of this
        // RootView's own tree paint - see its own doc comment above), this
        // is the right hook for something that needs the *final*, fully
        // composited buffer. dirtyRect_ still holds the region just
        // repainted while this runs (repaint() clears it right afterward,
        // so an override never has to). Base implementation hands it to
        // the PresentSurface (see presentsurface.h). PopupTool
        // (popuptool.h) overrides this instead of
        // subscribing onRedrawNeeded, precisely so its own present()
        // reads getImageBuffer() after this RootView's own children have
        // actually painted into it, not one frame stale.
        virtual void presentRepaintedBuffer();

        virtual void destroy() override;

        // Tears down only the live-window side - drag-drop registration and the backing
        // HWND (windowHandle() becomes nullptr) - leaving the child SubView tree fully intact
        // and readable. Idempotent. destroy() is the tree teardown plus this; Dialog calls
        // just this on WM_DESTROY so a caller can still read child state after showModal()
        // returns (see dialogs.h).
        void releaseWindow();

        virtual void addChild(SubView* child) override;
        virtual void removeChild(SubView* child) override;
        //@reflect ignore=true
		Frame* getFrame() const {
			return parentFrame_;
		}

        // Backing buffer for this RootView's HWND, drawn to with blend2d
        // (e.g. BLContext ctx(view.getImageBuffer());) and shown on screen by
        // this RootView's PresentSurface (WM_PAINT/BitBlt for GDI, an
        // upload + Present for DXGI). Note that anything drawn straight into
        // it is wiped by the next repaint() (the whole buffer is redrawn from
        // the View tree each time) - draw from onRedrawNeeded, or a View's
        // paint(), for anything that has to persist. Call invalidate() after
        // drawing to it to put the result on the screen now.
        BLImage& getImageBuffer() {
            return surface_->image();
        }

        // How this RootView is actually presenting right now - what
        // defaultPresentBackend() asked for (presentsurface.h), or Gdi if
        // that was Dxgi and this machine couldn't do it.
        //@reflect ignore=true
        PresentBackend presentBackend() const;

        // How much repaint() re-renders - see RepaintMode.
        //@reflect ignore=true
        RepaintMode repaintMode() const {
            return repaintMode_;
        }

        // Verification for RepaintMode::Dirty (on by default only if NEWUI_VERIFY_REPAINT is set): after each
        // pruned repaint, a full frame is rendered into a scratch buffer and compared with what the pruned
        // repaint left; any difference means some change wasn't invalidated properly, and is reported (stderr
        // and the debugger's output) with where. This counts those reports.
        //@reflect ignore=true
        std::size_t repaintVerifyMismatches() const {
            return verifyMismatches_;
        }

        // Shows a region of the buffer's *current* contents on the screen -
        // whole buffer for the no-argument/null forms - through whichever
        // PresentSurface this RootView uses, so it works the same under GDI
        // and DXGI. It does NOT re-run painting (use markDirty() to have the
        // View tree redrawn) and it does not touch the region a pending
        // markDirty() repaint still has to present.
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
        //
        // Not reflectgen-registered - transient runtime UI state, not
        // persistent structure, and (like View::rootView()/parent()) a
        // non-owning pointer into a SubView already reachable downward via
        // the normal childViews tree. Worse than a plain back-reference
        // here specifically: TypedProperty<RootView,SubView>::write()'s
        // nested-Class branch (reflection.h) resolves the written "type"
        // tag from ValueT (SubView, the getter's *static* return type),
        // never the pointee's real runtime type the way
        // TypedPropertyCollection::writeItem() does for an ordinary child -
        // so writing this out (once it became reachable at all - see
        // reflectgen.py's AssumeCopyable override) produced a *second*,
        // wrongly-`"type": "SubView"`-tagged copy of whatever real,
        // possibly-more-derived control (e.g. a Slider) currently has
        // hover/capture/focus, duplicating the same subtree's data with
        // the wrong type tag rather than the real one already written once
        // under its own parent in childViews - real, reported bad output,
        // not a hypothetical.
        //@reflect ignore=true
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
        //
        // Not reflectgen-registered - same reasoning as hoveredSubView()
        // just above.
        //@reflect ignore=true
        SubView* capturedSubView() const {
            return capturedSubView_;
        }

        // Which SubView keyboard events (keyEvent()) are routed to -
        // nullptr means "just the window itself", the pre-existing
        // behavior (onKeyDown()/onKeyPress()/onKeyUp() only ever fired on
        // this RootView). Set automatically on mouseDown()/
        // mouseDblClick() (clicking a SubView focuses it, clicking empty
        // space clears it), or call this directly for programmatic focus.
        // A no-op (focusedSubView_ left unchanged, no got/lostFocus
        // events fire) if the current focusedSubView_'s canResignFocus()
        // or target's canBecomeFocused() (View::, view.h) returns false.
        void setFocusedSubView(SubView* target);

        // Walks focusedSubView_'s own parent() chain - starting at
        // focusedSubView_ itself, ending at this RootView - looking for
        // the first View whose canPerformCommand(cmd)/performCommand(cmd)
        // (View::, view.h) answers it. This is the actual "responder
        // chain" entry point: a generic Edit menu Action can call these
        // without knowing which concrete control type is focused - see
        // command.h's CommandTable for how a control typically answers.
        // performCommand() stays void, same as View's own virtual it
        // overrides (and same as Action::perform()'s own convention) -
        // call canPerformCommand() first if the caller needs to know
        // whether anything will actually handle it.
        bool canPerformCommand(const CommandId& cmd) const override;
        void performCommand(const CommandId& cmd) override;

        // Not reflectgen-registered - same reasoning as hoveredSubView()
        // above; real, reported bad output this one specifically produced
        // (a second, wrongly-"type":"SubView"-tagged copy of whatever
        // control currently has focus - e.g. a Slider - alongside its one
        // correctly-tagged copy already written under its own parent in
        // childViews).
        //@reflect ignore=true
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

        // view's top-left in this RootView's own local/window-client space -
        // just view->localToRoot(Point(0,0)) (see View::localToRoot(), view.h).
        // Used internally by mouseMove()/mouseUp() to keep targeting
        // capturedSubView_ once the cursor is no longer over its bounds. Prefer
        // View::localToScreen()/mapTo() when positioning something relative to
        // an arbitrary view.
        Point accumulatedOffset(const SubView* view) const;

        // No RootView::localToScreen() of its own anymore - View::localToScreen()
        // (view.h) is inherited, and for a RootView root space *is* local space,
        // so root->localToScreen(pt) still means exactly what it always did.

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

        // Fresh, guaranteed-unique-within-this-tree default name for view -
        // lowercase-first-letter reflected class name plus the smallest
        // positive integer nameManager() hasn't already handed out to
        // another View in this tree, e.g. the first Button ever attached
        // anywhere in this tree becomes "button1". Called automatically by
        // View::propagateRootView() for any newly attached View whose
        // name() is still "" - not normally called directly.
        //@reflect ignore=true
        std::string generateDefaultName(const View& view);

        // Backs generateDefaultName() (and, via View::propagateRootView()/
        // setName(), reserves every hand-set name too, so a later
        // generateDefaultName() call never collides with one) - one
        // NameManager per RootView, so names only need to be unique
        // within a single tree, not across every RootView in the process.
        // Exposed publicly (rather than kept behind generateDefaultName()
        // alone) since View itself needs to reserve a hand-assigned name
        // directly - see View::setName()/propagateRootView() (view.cpp).
        //@reflect ignore=true
        NameManager& nameManager() {
            return nameManager_;
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

        // Renders whatever's in dirtyRect_ and presents it - what
        // scheduleRepaint()'s deferred idle task and repaintNow() both call.
        // Protected purely for testability: a test needs to repaint with a
        // *narrow* dirtyRect_ (markDirty(view, rect) then this), which
        // repaintNow() can't do since it always marks the whole client area.
        void repaint();

        // Protected for the same reason: a test flips these per instance.
        void setRepaintMode(RepaintMode mode);
        void setVerifyRepaint(bool verify);

        // Replaces this RootView's PresentSurface with a fresh one of the
        // given kind, overriding defaultPresentBackend() for this instance
        // (PopupTool forces Gdi - a layered window can't present through a
        // swap chain). Drops the current buffer, so call it before the first
        // sizing/initialize(), i.e. from a subclass constructor.
        void setPresentBackend(PresentBackend backend);


        newui::Rect fromViewToLocal(const View* fromView, const newui::Rect& rect);

        // Whatever's currently accumulated in dirtyRect_ (empty Rect if
        // nothing's pending) - protected purely for testability, same
        // pattern as every other method in this block, so a test can
        // confirm a given action actually invalidated something (e.g.
        // View::addChild()/removeChild()/reorderChild() each calling
        // redraw()) without needing a real HWND/message pump to observe a
        // real repaint.
        const newui::Rect& dirtyRect() const {
            return dirtyRect_;
        }

    private:
        // See its own definition comment (rootview.cpp) - called once
        // from each constructor.
        void initDefaultBackground();

	    Frame* parentFrame_ = nullptr;
		HWND viewHwnd_ = nullptr;

		// Only set when constructed via the standalone (Frame-less)
		// constructor above - parentFrame_ stays nullptr in that case.
		// See initialize()'s parent/hInstance resolution.
		HWND externalParentHwnd_ = nullptr;
		HINSTANCE externalInstanceHandle_ = nullptr;

        // Owns getImageBuffer()'s pixels and the final transfer of them to
        // the window - see PresentSurface (presentsurface.h). Always
        // non-null; GdiPresentSurface (the original BitBlt-on-WM_PAINT
        // path) today.
        std::unique_ptr<PresentSurface> surface_;

        newui::Rect dirtyRect_;

        RepaintMode repaintMode_ = defaultRepaintMode();
        bool verifyRepaint_ = defaultVerifyRepaint();
        std::size_t verifyMismatches_ = 0;

        // Paints this RootView's tree into image, on top of whatever's already there: the background, the
        // children (those overlapping `region` - View::paintChildren() culls the rest), then the overlay.
        // With `clip`, every draw is confined to `region`, so nothing outside it is modified.
        void paintTree(BLImage& image, const newui::Rect& region, bool clip);

        // Renders a full frame into a scratch buffer and compares it with what the pruned repaint of `region`
        // just left in the surface's buffer, reporting (and counting) any difference.
        void verifyPrunedRepaint(const newui::Rect& region);

        // markDirty()/markDirty(fromView, rect) don't call repaint() (the
        // actual, expensive Blend2D repaint) directly - they union into
        // dirtyRect_, then call
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

        WNDPROC defaultWndProc_ = nullptr;
        WNDPROC wndProc_ = nullptr;

        bool mouseEnteredControl_ = false;

        // Registered as this window's one real IDropTarget in
        // viewCreated() (RegisterDragDrop()) and revoked in destroy()
        // (RevokeDragDrop(), before DestroyWindow()) - see COMDropTarget's
        // own class comment (dragndrop.h) for why there's exactly one of
        // these per window regardless of how many (or how few) Views
        // actually register their own DropTarget. Every RootView gets one
        // automatically - pure listening plumbing with zero behavioral
        // effect until some View actually calls setDropTarget(), so there's
        // no reason to make this opt-in per window.
        Microsoft::WRL::ComPtr<COMDropTarget> comDropTarget_;

        // Mouse/keyboard routing state - see hoveredSubView()/
        // capturedSubView()/focusedSubView() above for what each means.
        SubView* hoveredSubView_ = nullptr;
        SubView* capturedSubView_ = nullptr;
        SubView* focusedSubView_ = nullptr;

        // Tracks a single potential outgoing drag from mouseDown() to
        // either a drag actually starting (activeDragView_'s own
        // View::dragSource() - if it has one - gets asked once the
        // movement threshold is crossed in mouseMove(); StartDragOperation()
        // only actually runs if that's handled) or the button coming back
        // up first (an ordinary click, never armed) - see mouseDown()/
        // mouseMove()/mouseUp() (rootview.cpp). activeDragLocalPt_ is in
        // activeDragView_'s own local space, same convention
        // capturedSubView_'s dispatch already uses.
        SubView* activeDragView_ = nullptr;
        Point activeDragLocalPt_;

        std::unique_ptr<Overlay> overlay_;

        // Backs nameManager()/generateDefaultName() - see their own doc
        // comments above.
        NameManager nameManager_;

        // Updates hoveredSubView_ to target, firing onMouseLeft()/
        // onMouseEntered() (and toggling View::setHighlighted() +
        // style().markDirty(), so hover state actually repaints) on
        // whichever of the old/new hovered views actually changed. rootPt
        // is the mouse position in this RootView's own local space - a
        // no-op if target is already the current hoveredSubView_.
        void updateHoveredSubView(SubView* target, const Point& rootPt);

        // The one choke point every real mouse-dispatch site (mouseDown/
        // mouseMove/mouseUp/mouseDblClick/mouseWheel) routes its hit-test
        // through, instead of calling hitTestChildren() directly - a
        // design-time SubView (isDesignTime(), e.g. content loaded into a
        // Designer's RootViewProxy) is never a valid real-interaction
        // target, so this returns nullptr for one exactly as if nothing
        // were hit at all. hitTestChildren() itself stays ungated - a
        // Designer's own selection code needs the *real* hit target,
        // design-time or not (that's the whole point of selecting it).
        SubView* resolveInteractiveHit(const Point& pt, Point& outLocalPt) const;

        bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT);

        static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


        void viewCreated();
    };

}

#pragma once

#include <memory>
#include <string>

#include <newui/newui.h>
#include <newui/delegate.h>
#include <newui/rootview.h>
#include <newui/shapes.h>
#include <newui/underlay.h>

namespace newui {

    // Custom-shaped, topmost popup window - see popup-tool-plan.md (repo
    // root) for the full design background.
    //
    // A PopupTool is a RootView whose real Win32 window is a
    // WS_POPUP + WS_EX_LAYERED top-level window (see preCreateHints()/
    // postCreate() below) instead of the WS_CHILD window every ordinary
    // RootView creates - avoiding the "duplicate RootView vs. customize
    // it" fork popup-tool-plan.md describes, now that RootView::
    // initialize() actually calls preCreateHints()/postCreate() (see
    // rootview.cpp). It never receives real WM_PAINT once
    // UpdateLayeredWindow() has been called on it even once - that's
    // documented Win32 behavior for a layered window driven this way, so
    // paintImageBufferToWindow()'s own BitBlt()-on-WM_PAINT path never
    // actually runs for a PopupTool; presentRepaintedBuffer() (overridden
    // below) is what gets this RootView's own freshly-repainted
    // getImageBuffer() onto the screen instead, via present()'s
    // UpdateLayeredWindow() call.
    //
    // Owns an Underlay - see its own class comment (underlay.h) for why
    // it needs a separate, real DIB-backed buffer of its own, used as the
    // background this RootView's own SubView tree (real Buttons/Labels/...,
    // added via addChild() same as any other RootView) gets composited
    // over in present() below.
    //
    // imageBufferFormat() overrides getImageBuffer() to BL_FORMAT_PRGB32
    // (real per-pixel alpha) instead of RootView's own default opaque
    // BL_FORMAT_XRGB32 - present() reads that buffer straight into
    // underlay_'s own canvas on top of the Underlay's background, so any
    // child SubView needs real alpha in its own painted output for that
    // composite to look right (an opaque child would otherwise paint a
    // solid rectangle over the Underlay's shape). This RootView's own
    // background (style().setBackgroundColor(), postCreate()) is left
    // fully transparent for the same reason - resizeImageBuffer() zero-
    // fills the fresh DIB section either way, so nothing needs to paint a
    // background at all for the buffer to start (and, wherever nothing
    // else paints, stay) transparent.
    class PopupTool : public RootView {
    public:
        // Same constructor shape as RootView's own standalone (Frame-
        // less) constructor (rootview.h) - a PopupTool never has a Frame.
        // ownerHwnd becomes CreateWindowExA's owner window (not a real
        // Win32 parent - WS_POPUP treats that argument as an owner) -
        // required non-null, same as RootView::initialize() already
        // requires for its own `parent` argument. Caller must still call
        // initialize() explicitly afterward, same as any other
        // standalone RootView - see RootView's own constructor comment.
        PopupTool(HWND ownerHwnd, HINSTANCE instanceHandle, const newui::Rect& bounds, const std::string& name);

        ~PopupTool() override;

        void preCreateHints(size_t& wndClassFlags, size_t& windowStyleFlags, size_t& windowExStyleFlags) override;
        void postCreate() override;

        // BL_FORMAT_PRGB32 instead of RootView's own default opaque
        // BL_FORMAT_XRGB32 - see this class's own comment for why.
        BLFormat imageBufferFormat() const override;

        // Fires once repaint() has actually finished writing this
        // RootView's own SubView tree into getImageBuffer() - see this
        // virtual's own doc comment (rootview.h). Calls present(), which
        // is what actually composites that buffer over underlay_ and
        // pushes the result via UpdateLayeredWindow - this is the right
        // place for that (not onRedrawNeeded, which fires *before*
        // repaint() runs, and not RootView::presentRepaintedBuffer()'s
        // own invalidate() call, meaningless for a layered window that
        // never receives WM_PAINT - see this class's own comment).
        void presentRepaintedBuffer() override;

        // RootView::setBounds() (rootview.h) already calls
        // presentRepaintedBuffer() (above) synchronously as part of its
        // own resizeImageBuffer() call, but that happens *before* its own
        // real ::SetWindowPos() call at the very end, so a present()
        // driven purely by that would push UpdateLayeredWindow a stale
        // GetWindowRect() snapshot taken before the HWND actually moved/
        // resized. A layered window doesn't auto-restretch its last
        // UpdateLayeredWindow bitmap the way an ordinary WM_PAINT-driven
        // window repaints itself after SetWindowPos - it needs an
        // explicit, fresh UpdateLayeredWindow call at the real final
        // rect. This override just re-presents once base's setBounds()
        // (and its SetWindowPos) has actually finished, so a resize/move
        // never briefly shows a mismatched position/size before the next
        // unrelated repaint happens to fix it. (The extra present() this
        // causes mid-call is harmless - just an immediately-superseded
        // UpdateLayeredWindow call.)
        void setBounds(const Rect& bounds) override;

        Underlay* underlay() const {
            return underlay_.get();
        }

        // The one safe way to close a PopupTool - fires onDismissed(*this)
        // (so a listener can drop its own tracking pointer to this
        // PopupTool first), then destroy()s the real window and deletes
        // this PopupTool itself. A deliberate departure from the usual
        // heap-only "caller frees explicitly" convention (View's own
        // class comment, view.h): a PopupTool can decide on its own to go
        // away (Escape, losing focus - see dismissOnFocusLost() below),
        // so there's no single outside owner who could safely
        // destroy()+delete it instead without racing that. Callers that
        // want to close a PopupTool programmatically should call this
        // too, never destroy()+delete directly.
        //
        // Safe to call from *anywhere* - ordinary outside code (a
        // caller's own button, Frame::onClosed, ...) just as much as a
        // child SubView added to this PopupTool's own tree (a "Close"
        // Button's onClick, say) or this class's own internal Escape/
        // focus-lost detection. While a RunLoop is current, the actual
        // teardown is deferred one tick via RunLoop::post() before it
        // runs - see dismissNow()'s own comment for the real, reproduced
        // crash that avoids (a child Button's onClick, like Escape/focus-
        // loss, still runs from inside this PopupTool's own WndProc/
        // handleMessage/... dispatch, which keeps touching *this after
        // the delegate that invoked the click returns). With no RunLoop
        // current (e.g. cleanup after Application::run() has already
        // returned), nothing could be concurrently dispatching against
        // this PopupTool either way, so this runs dismissNow()
        // immediately instead.
        void dismiss();

        // If true (the default), losing focus entirely - WM_KILLFOCUS,
        // e.g. the user clicking anywhere outside this popup - calls
        // dismiss() the same way Escape does (see postCreate()'s own
        // onLostFocus hook). Set false for a popup that should stay open
        // while the user interacts elsewhere.
        bool dismissOnFocusLost() const {
            return dismissOnFocusLost_;
        }
        void setDismissOnFocusLost(bool value) {
            dismissOnFocusLost_ = value;
        }

        // Fired by dismiss() - *before* it tears anything down, so a
        // listener can still safely read this PopupTool's final state (or
        // just drop its own tracking pointer to it). Never fire this
        // directly, and never destroy()/delete this PopupTool from inside
        // a listener - dismiss() already does both, right after this
        // delegate returns.
        typedef Delegate<PopupTool> DismissedDelegate;
        DismissedDelegate onDismissed;

        // Renders underlay_ fresh, composites this RootView's own
        // getImageBuffer() (its real SubView tree - Buttons, Labels, ... -
        // already repainted into it by the time presentRepaintedBuffer()
        // calls this) on top, and pushes the result to the screen via
        // UpdateLayeredWindow - the only way this popup's content
        // actually reaches the desktop (see this class's own comment).
        // Called automatically via presentRepaintedBuffer() once
        // postCreate() has run - exposed publicly only for a caller that
        // just changed underlay()'s content directly (e.g. after
        // underlay()->loadImageFile()) and wants it on screen
        // immediately rather than waiting for the next markDirty().
        void present();

    private:
        SyncReturn onRootSizeChanged(View& sender, const Size& newSize);
        SyncReturn onRootKeyDown(View& sender, std::uint32_t keyMask, int keyCharVal, int repeatCount,
            std::uint32_t VKeyCode);
        SyncReturn onRootLostFocus(View& sender);

        // Clears getImageBuffer() to fully transparent - onRedrawNeeded
        // fires here (rootview.h's own doc comment) *before* repaint()
        // draws this RootView's own paintStyle()/paint()/paintChildren()
        // into it, which is exactly the on-label use this delegate
        // documents: something that needs to touch the buffer ahead of
        // that tree paint. Needed because paintChildren() (view.h) redraws
        // every child unconditionally on every single repaint, regardless
        // of dirty rect - fine for an opaque window (each pass fully
        // overwrites the last), but this RootView's own background is
        // deliberately left transparent (see this class's own comment),
        // so without this, a child with no opaque background of its own
        // (a plain Label's anti-aliased text, say) would have its edges
        // re-blended onto themselves on every repaint instead of drawn
        // fresh - real, reproduced bug: repeatedly darkened/thickened
        // label text once postCreate()'s deferred repaintNow() (and any
        // later markDirty()-driven repaint - a hover, say) ran more than
        // once without this.
        SyncReturn onRootRedrawNeeded(RootView& sender);

        // The actual teardown - fires onDismissed(*this), then destroy()s
        // the real window and deletes this PopupTool. Only ever called by
        // dismiss() (immediately if no RunLoop is current, otherwise from
        // its posted task) - never called directly, and in particular
        // never synchronously from inside this PopupTool's own event
        // dispatch. Real, reproduced crash that guards against: RootView::
        // keyEvent()/lostFocus() (rootview.cpp) both keep touching *this -
        // reading focusedSubView_, firing a second delegate call - *after*
        // onKeyDown()/onLostFocus() (or, for a child SubView's own click,
        // whatever mouse-dispatch method fired it) already returns, so a
        // synchronous destroy()+delete from within any of those frees the
        // very object that call stack is still executing on (caught live
        // in a debugger: a poisoned/freed `this` inside Delegate::
        // syncCall(), one frame up from RootView::keyEvent()).
        void dismissNow();

        std::unique_ptr<Underlay> underlay_;
        bool dismissOnFocusLost_ = true;

        // Set false in the destructor, checked by dismiss()'s posted
        // task before it touches this PopupTool - same reasoning,
        // same pattern as RootView::aliveFlag_'s own doc comment
        // (rootview.h): that posted task can still be sitting in
        // RunLoop's queue after this PopupTool itself is destroyed by
        // some other path (e.g. its owner closing) in the meantime.
        std::shared_ptr<bool> aliveFlag_ = std::make_shared<bool>(true);
    };

    // A PopupTool whose Underlay is a single shapes::CalloutRoundRect
    // (shapes.h) - the rounded card-with-a-tail shape ShowShapePopup()
    // (examples/popuptool1.cpp) builds by hand, wrapped up as a reusable
    // class. postCreate() builds the shape once, sizes it from bounds(),
    // and adds it to underlay()->shapeLayer(); the setters below forward
    // to it. calloutShape() is exposed for anything they don't cover
    // (fill/stroke color and the rest of ShapeStyle, in particular).
    class CalloutTool : public PopupTool {
    public:
        // Same constructor shape as PopupTool's own (above).
        CalloutTool(HWND ownerHwnd, HINSTANCE instanceHandle, const newui::Rect& bounds, const std::string& name);

        ~CalloutTool() override;

        void postCreate() override;

        shapes::CalloutRoundRect* calloutShape() const {
            return calloutShape_;
        }

        shapes::TailSide tailSide() const;
        void setTailSide(shapes::TailSide value);

        float tailWidth() const;
        void setTailWidth(float value);

        float tailHeight() const;
        void setTailHeight(float value);

        float tailPosition() const;
        void setTailPosition(float value);

        float cornerRadius() const;
        void setCornerRadius(float value);

    private:
        // Sizes calloutShape_ from bounds() - a small fixed margin all
        // around, plus tailHeight() reserved on whichever side tailSide()
        // names, so the tail has room to poke out without clipping
        // against the underlay's own canvas edge. Called by postCreate()
        // and by setTailSide()/setTailHeight(), the two properties that
        // affect this margin.
        void updateShapeLayout();

        // Non-owning - underlay()->shapeLayer() (Underlay, underlay.h)
        // owns it, same as any other Shape added to a ShapeLayer (see
        // ShapeLayer::addShape()'s own doc comment, shapes.h).
        shapes::CalloutRoundRect* calloutShape_ = nullptr;
    };

}

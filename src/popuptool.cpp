#include "newui/popuptool.h"
#include "newui/color.h"
#include "newui/keyboard_constants.h"
#include "newui/runloop.h"
#include "newui/viewstyle.h"

namespace newui {

    PopupTool::PopupTool(HWND ownerHwnd, HINSTANCE instanceHandle, const newui::Rect& bounds, const std::string& name)
        : RootView(ownerHwnd, instanceHandle, bounds, name) {
    }

    PopupTool::~PopupTool() {
        *aliveFlag_ = false;
    }

    void PopupTool::preCreateHints(size_t& wndClassFlags, size_t& windowStyleFlags, size_t& windowExStyleFlags) {
        // A layered popup redraws itself explicitly via present()/
        // UpdateLayeredWindow rather than WM_PAINT - CS_HREDRAW/
        // CS_VREDRAW (which just force a full WM_ERASEBKGND+WM_PAINT on
        // resize, both meaningless here) are dropped; CS_DBLCLKS is kept
        // since ordinary mouse dispatch (RootView::mouseDblClick(),
        // rootview.cpp) still relies on it.
        wndClassFlags = CS_DBLCLKS;

        // WS_POPUP instead of RootView::initialize()'s own SIMPLE_VIEW
        // (WS_CHILD | ...) - this is the whole point of PopupTool: a
        // real top-level window, not embedded in a parent's client area.
        windowStyleFlags = WS_POPUP;

        // WS_EX_LAYERED is what makes UpdateLayeredWindow() (present(),
        // below) legal at all; WS_EX_TOPMOST matches "pops up the
        // topmost window" (popup-tool-plan.md).
        windowExStyleFlags = WS_EX_LAYERED | WS_EX_TOPMOST;
    }

    void PopupTool::postCreate() {
        RootView::postCreate();

        if (windowHandle() == nullptr) {
            return;
        }

        onSizeChanged.add(this, &PopupTool::onRootSizeChanged);
        onKeyDown.add(this, &PopupTool::onRootKeyDown);
        onLostFocus.add(this, &PopupTool::onRootLostFocus);
        onRedrawNeeded.add(this, &PopupTool::onRootRedrawNeeded);

        // Fully transparent - see this class's own comment (popuptool.h)
        // for why paintStyle()'s background fill has to stay a no-op here
        // (resizeImageBuffer()'s zero-fill is what actually keeps
        // getImageBuffer() transparent wherever nothing else paints, not
        // this - but a non-transparent color here would still wrongly
        // paint an opaque rectangle over the whole popup every repaint).
        style().setBackgroundColor(Color(0.0f, 0.0f, 0.0f, 0.0f));

        underlay_ = std::make_unique<Underlay>();
        underlay_->viewSized(bounds().size());

        // Immediate best-effort first frame - just the Underlay's own
        // background, since nothing has addChild()'d a real control yet
        // at this point (postCreate() runs *during* initialize(), before
        // it returns to whatever code constructed this PopupTool - see
        // this class's own constructor comment). Avoids ShowWindow()
        // (RootView::initialize(), right after postCreate() returns)
        // showing a layered surface with no content assigned at all.
        present();

        // The real first frame - posted, not called inline, so it runs
        // only after the current call stack fully unwinds back to
        // RunLoop::run()'s own message processing. By then, whoever
        // constructed this PopupTool has had a full turn to addChild()
        // real controls onto it (the normal pattern - initialize() first,
        // then build content, see this class's own constructor comment) -
        // RootView::addChild() (rootview.cpp) doesn't itself mark this
        // RootView dirty, so without this, those children wouldn't
        // actually appear until some unrelated event (a mouse hover, say)
        // happened to call markDirty() on its own. One single posted
        // repaintNow() naturally coalesces any number of addChild() calls
        // into exactly one repaint, rather than repainting once per call -
        // repainting more than once here would re-blend each already-
        // drawn child's anti-aliased edges onto themselves (see
        // onRootRedrawNeeded()'s own comment for why that's real, not
        // theoretical).
        if (RunLoop::current()) {
            std::shared_ptr<bool> alive = aliveFlag_;
            RunLoop::current().post([this, alive]() {
                if (*alive) {
                    repaintNow();
                }
            });
        }
    }

    BLFormat PopupTool::imageBufferFormat() const {
        return BL_FORMAT_PRGB32;
    }

    SyncReturn PopupTool::onRootRedrawNeeded(RootView& /*sender*/) {
        BLContext ctx(getImageBuffer());
        ctx.clear_all();
        ctx.end();
        return SyncReturn::Handled;
    }

    void PopupTool::presentRepaintedBuffer() {
        present();
    }

    void PopupTool::setBounds(const Rect& bounds) {
        RootView::setBounds(bounds);

        // Base's setBounds() already ran present() once (via
        // presentRepaintedBuffer(), mid-call - see this method's own
        // comment, popuptool.h) with a stale GetWindowRect() snapshot
        // taken before its ::SetWindowPos() call actually moved/resized
        // the real HWND. Re-present now that it has, so UpdateLayeredWindow
        // sees the real final rect.
        present();
    }

    SyncReturn PopupTool::onRootSizeChanged(View& /*sender*/, const Size& newSize) {
        if (underlay_) {
            underlay_->viewSized(newSize);
        }
        return SyncReturn::Handled;
    }

    SyncReturn PopupTool::onRootKeyDown(View& /*sender*/, std::uint32_t /*keyMask*/, int /*keyCharVal*/,
            int /*repeatCount*/, std::uint32_t VKeyCode) {
        if (VKeyCode != vkEscape) {
            return SyncReturn::Ignored;
        }
        dismiss();
        return SyncReturn::Handled;
    }

    SyncReturn PopupTool::onRootLostFocus(View& /*sender*/) {
        if (dismissOnFocusLost_) {
            dismiss();
        }
        return SyncReturn::Handled;
    }

    void PopupTool::dismiss() {
        // See this method's own doc comment (popuptool.h) for why the
        // actual teardown (dismissNow()) has to be deferred whenever a
        // RunLoop is current, rather than running directly here - this
        // may be running from inside this PopupTool's own event dispatch
        // (a child SubView's onClick, Escape, losing focus), which keeps
        // touching *this after the delegate that called this returns.
        if (!RunLoop::current()) {
            dismissNow();
            return;
        }
        std::shared_ptr<bool> alive = aliveFlag_;
        RunLoop::current().post([this, alive]() {
            if (*alive) {
                dismissNow();
            }
        });
    }

    void PopupTool::dismissNow() {
        onDismissed(*this);
        destroy();
        delete this;
    }

    void PopupTool::present() {
        HWND hwnd = windowHandle();
        if (hwnd == nullptr || !underlay_) {
            return;
        }

        underlay_->render();

        gfx::Image& img = underlay_->image();
        if (!img.isValid()) {
            return;
        }

        // Composite this RootView's own SubView tree - real Buttons/
        // Labels/... added via addChild(), already repainted into
        // getImageBuffer() with real per-pixel alpha (imageBufferFormat())
        // by the time presentRepaintedBuffer() calls this - on top of the
        // Underlay's own background. Plain blit_image(), not scaled -
        // both buffers are always the same size (onRootSizeChanged() keeps
        // underlay_ sized to bounds(), same as resizeImageBuffer() keeps
        // getImageBuffer()).
        {
            BLContext ctx(img.blImage());
            ctx.blit_image(BLPoint(0, 0), getImageBuffer());
            ctx.end();
        }

        RECT windowRect{};
        ::GetWindowRect(hwnd, &windowRect);

        POINT ptDst{ windowRect.left, windowRect.top };
        SIZE szWindow{ img.width(), img.height() };
        POINT ptSrc{ 0, 0 };

        BLENDFUNCTION blend{};
        blend.BlendOp = AC_SRC_OVER;
        blend.BlendFlags = 0;
        blend.SourceConstantAlpha = 255;
        blend.AlphaFormat = AC_SRC_ALPHA;

        ::UpdateLayeredWindow(hwnd, nullptr, &ptDst, &szWindow, img.memDC(), &ptSrc, 0, &blend, ULW_ALPHA);
    }

    CalloutTool::CalloutTool(HWND ownerHwnd, HINSTANCE instanceHandle, const newui::Rect& bounds, const std::string& name)
        : PopupTool(ownerHwnd, instanceHandle, bounds, name) {
    }

    CalloutTool::~CalloutTool() {
    }

    void CalloutTool::postCreate() {
        PopupTool::postCreate();

        if (windowHandle() == nullptr) {
            return;
        }

        calloutShape_ = new shapes::CalloutRoundRect();
        calloutShape_->style().fill().setColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
        calloutShape_->style().fill().setKind(gfx::PaintKind::Color);
        calloutShape_->style().stroke().setColor(Color(0.78f, 0.78f, 0.78f, 1.0f));
        calloutShape_->style().stroke().setKind(gfx::PaintKind::Color);
        calloutShape_->style().stroke().setWidth(1.5f);
        underlay()->shapeLayer().addShape(calloutShape_);

        updateShapeLayout();
        present();
    }

    shapes::TailSide CalloutTool::tailSide() const {
        return calloutShape_->tailSide();
    }

    void CalloutTool::setTailSide(shapes::TailSide value) {
        calloutShape_->setTailSide(value);
        updateShapeLayout();
        present();
    }

    float CalloutTool::tailWidth() const {
        return calloutShape_->tailWidth();
    }

    void CalloutTool::setTailWidth(float value) {
        calloutShape_->setTailWidth(value);
        present();
    }

    float CalloutTool::tailHeight() const {
        return calloutShape_->tailHeight();
    }

    void CalloutTool::setTailHeight(float value) {
        calloutShape_->setTailHeight(value);
        updateShapeLayout();
        present();
    }

    float CalloutTool::tailPosition() const {
        return calloutShape_->tailPosition();
    }

    void CalloutTool::setTailPosition(float value) {
        calloutShape_->setTailPosition(value);
        present();
    }

    float CalloutTool::cornerRadius() const {
        return calloutShape_->radius();
    }

    void CalloutTool::setCornerRadius(float value) {
        calloutShape_->setRadius(value);
        present();
    }

    void CalloutTool::updateShapeLayout() {
        constexpr float kMargin = 2.0f;
        const Size sz = bounds().size();
        float tail = calloutShape_->tailHeight();

        float x = kMargin;
        float y = kMargin;
        float w = sz.width - kMargin * 2.0f;
        float h = sz.height - kMargin * 2.0f;

        switch (calloutShape_->tailSide()) {
            case shapes::TailSide::Top:    y += tail; h -= tail; break;
            case shapes::TailSide::Bottom: h -= tail; break;
            case shapes::TailSide::Left:   x += tail; w -= tail; break;
            case shapes::TailSide::Right:  w -= tail; break;
        }

        calloutShape_->setX(x);
        calloutShape_->setY(y);
        calloutShape_->setWidth(w);
        calloutShape_->setHeight(h);
    }

}

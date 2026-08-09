#pragma once

#include <vector>

#include <blend2d/blend2d.h>

#include <newui/newui.h>
#include <newui/view.h>
#include <newui/geometry.h>

namespace newui {
    class Frame;

	class SubView;

    class RootView : public View {
    public:
        RootView( Frame* frame, const newui::Rect& bounds, const std::string& name );
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



		virtual bool initialize();
        virtual void destroy();

        virtual void addChild(SubView* child);
        virtual void removeChild(SubView* child);

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

        std::tuple<RootView*, SubView*> getTarget(HWND hwnd);
    private:
	    Frame* parentFrame_ = nullptr;
		HWND viewHwnd_ = nullptr;

        // imageBuffer_ wraps imagePixels_ directly (via BLImage::create_from_data)
        // rather than using BLImage::create(), because blend2d pads its own
        // allocations to a 16-byte stride for SIMD, which would not match the
        // stride StretchDIBits infers from biWidth. Owning the buffer keeps
        // the stride at exactly width * 4 so both sides agree on layout.
        BLImage imageBuffer_;
        std::vector<uint8_t> imagePixels_;

        void resizeImageBuffer(int width, int height);
        void paintImageBufferToWindow(HDC hdc);
        void notifyRedrawNeeded();
        void repaint();

        WNDPROC defaultWndProc_ = nullptr;
        WNDPROC wndProc_ = nullptr;

        bool mouseEnteredControl_ = false;

        bool handleMessage(UINT message, WPARAM wParam, LPARAM lParam, LRESULT& outLRESULT);

        static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);


        void viewCreated();

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
    };

}

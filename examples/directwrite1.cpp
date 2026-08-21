// Spike: how much does it cost to draw text via DirectWrite/Direct2D inside
// a normal newui SubView, whose paint(BLContext&) otherwise draws through
// Blend2D exactly like every other View in this toolkit?
//
// D2D/DWrite have no concept of a BLContext - there's no way to hand them
// the live BLContext& paint() already receives and have them draw into it
// directly. The bridge used here mirrors the one ThemedViewStyle::paint()
// (viewstyle.cpp) already uses for native theme parts (DrawThemeBackground
// via BeginBufferedPaint): render into a private, off-screen 32bpp top-down
// DIB section via GDI interop, then wrap that DIB's raw pixels straight into
// a BLImage (create_from_data(), no copy, no format conversion - a DC render
// target's DXGI_FORMAT_B8G8R8A8_UNORM/D2D1_ALPHA_MODE_PREMULTIPLIED pixel
// format is byte-for-byte the same premultiplied top-down BGRA layout as
// blend2d's own BL_FORMAT_PRGB32) and blit_image() it into ctx. Same
// "off-screen buffer now, blit once" shape, just with an ID2D1DCRenderTarget/
// IDWriteTextLayout doing the drawing instead of DrawThemeBackground().
//
// Findings so far: no changes needed anywhere else in the framework - this
// is entirely self-contained inside one SubView subclass. The real cost is
// per-frame: BindDC() + BeginDraw()/EndDraw() + GdiFlush() + a fresh
// BLImage::create_from_data() wrapper every paint(), on top of maintaining a
// second, independent font-loading pipeline (DWrite's IDWriteTextFormat)
// alongside the one FontManager/Font already provide via Blend2D.

#include "newui/newui.h"
#include "newui/application.h"
#include "newui/color.h"
#include "newui/frame.h"
#include "newui/layout.h"
#include "newui/rootview.h"
#include "newui/subview.h"
#include "newui/uicolormanager.h"
#include "newui/view.h"

#include <blend2d/blend2d.h>

#include <d2d1.h>
#include <dwrite.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")

namespace {

    template <typename T>
    void ReleaseCom(T*& ptr) {
        if (ptr != nullptr) {
            ptr->Release();
            ptr = nullptr;
        }
    }

}

// A single SubView that draws "Hello, DirectWrite!" via
// IDWriteTextLayout/ID2D1DCRenderTarget instead of Font/BLFont - see the
// file comment above for the actual bridging mechanism.
class DirectWriteHelloView : public newui::SubView {
public:
    ~DirectWriteHelloView() override {
        releaseDeviceResources();
        releaseDibBuffer();
    }

    void paint(BLContext& ctx) override {
        const int width = static_cast<int>(bounds().width());
        const int height = static_cast<int>(bounds().height());
        if (width <= 0 || height <= 0) {
            return;
        }

        if (!ensureDeviceResources() || !ensureDibBuffer(width, height)) {
            return;
        }

        RECT rc{0, 0, width, height};
        renderTarget_->BindDC(dibDC_, &rc);
        renderTarget_->BeginDraw();
        // Fully transparent clear - only the glyphs DrawTextLayout() paints
        // below should end up opaque; blit_image() below composites the
        // rest of this DIB's (untouched, still-zero) alpha as "nothing
        // here" over whatever paintStyle() already drew for this SubView's
        // own background.
        renderTarget_->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        ID2D1SolidColorBrush* brush = nullptr;
        renderTarget_->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);

        static const wchar_t kText[] = L"Hello, DirectWrite! (drawn via IDWriteTextLayout into a Blend2D SubView)";
        IDWriteTextLayout* textLayout = nullptr;
        dwriteFactory_->CreateTextLayout(
            kText, static_cast<UINT32>(wcslen(kText)), textFormat_,
            static_cast<float>(width), static_cast<float>(height), &textLayout);

        if (textLayout != nullptr && brush != nullptr) {
            renderTarget_->DrawTextLayout(D2D1::Point2F(12.0f, 12.0f), textLayout, brush);
        }
        if (textLayout != nullptr) {
            textLayout->Release();
        }

        ReleaseCom(brush);

        HRESULT hr = renderTarget_->EndDraw();
        if (hr == D2DERR_RECREATE_TARGET) {
            // Device loss (rare for a software DC render target, but a
            // real integration would still need to handle it) - drop the
            // render target so ensureDeviceResources() rebuilds it next
            // paint() instead of drawing through a now-invalid one.
            releaseDeviceResources();
            return;
        }

        // GDI (which is what BindDC()'s HDC ultimately writes through)
        // batches drawing calls - without this, reading dibBits_ directly
        // afterward can observe stale/incomplete pixels, the same reason
        // ThemedViewStyle::paint() calls GdiFlush() before its own
        // GetBufferedPaintBits() read (viewstyle.cpp).
        ::GdiFlush();

        // No copy: wraps the DIB section's own memory directly. Its
        // pixel layout (32bpp top-down premultiplied BGRA) is exactly
        // BL_FORMAT_PRGB32 - same fact ThemedViewStyle::paint() and
        // gfx::Image already rely on for their own DIB buffers.
        BLImage textImage;
        textImage.create_from_data(width, height, BL_FORMAT_PRGB32, dibBits_, intptr_t(width) * 4);

        ctx.save();
        ctx.blit_image(BLPoint(0, 0), textImage);
        ctx.restore();
    }

private:
    bool ensureDeviceResources() {
        if (d2dFactory_ == nullptr) {
            if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory_))) {
                return false;
            }
        }

        if (dwriteFactory_ == nullptr) {
            if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                    reinterpret_cast<IUnknown**>(&dwriteFactory_)))) {
                return false;
            }
        }

        if (textFormat_ == nullptr) {
            if (FAILED(dwriteFactory_->CreateTextFormat(
                    L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
                    DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us", &textFormat_))) {
                return false;
            }
        }

        if (renderTarget_ == nullptr) {
            // BindDC() below is what a DC render target actually draws
            // through - width/height here are placeholders, real ones are
            // supplied per-paint() via BindDC()'s own RECT.
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
            if (FAILED(d2dFactory_->CreateDCRenderTarget(&props, &renderTarget_))) {
                return false;
            }
        }

        return true;
    }

    void releaseDeviceResources() {
        ReleaseCom(renderTarget_);
        ReleaseCom(textFormat_);
        ReleaseCom(dwriteFactory_);
        ReleaseCom(d2dFactory_);
    }

    // Own private off-screen DIB, entirely separate from RootView's own
    // imageBuffer_ - same "hand it its own DIB rather than reach into
    // RootView's" shape gfx::Image already uses. Recreated only when this
    // SubView's size actually changes.
    bool ensureDibBuffer(int width, int height) {
        if (width == dibWidth_ && height == dibHeight_ && dib_ != nullptr) {
            return true;
        }

        releaseDibBuffer();

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dib_ = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
        if (dib_ == nullptr) {
            return false;
        }

        dibDC_ = ::CreateCompatibleDC(nullptr);
        if (dibDC_ == nullptr) {
            releaseDibBuffer();
            return false;
        }

        dibOldBitmap_ = static_cast<HBITMAP>(::SelectObject(dibDC_, dib_));
        dibWidth_ = width;
        dibHeight_ = height;
        return true;
    }

    void releaseDibBuffer() {
        if (dibDC_ != nullptr) {
            if (dibOldBitmap_ != nullptr) {
                ::SelectObject(dibDC_, dibOldBitmap_);
                dibOldBitmap_ = nullptr;
            }
            ::DeleteDC(dibDC_);
            dibDC_ = nullptr;
        }
        if (dib_ != nullptr) {
            ::DeleteObject(dib_);
            dib_ = nullptr;
        }
        dibBits_ = nullptr;
        dibWidth_ = 0;
        dibHeight_ = 0;
    }

    ID2D1Factory* d2dFactory_ = nullptr;
    IDWriteFactory* dwriteFactory_ = nullptr;
    IDWriteTextFormat* textFormat_ = nullptr;
    ID2D1DCRenderTarget* renderTarget_ = nullptr;

    HBITMAP dib_ = nullptr;
    HBITMAP dibOldBitmap_ = nullptr;
    HDC dibDC_ = nullptr;
    void* dibBits_ = nullptr;
    int dibWidth_ = 0;
    int dibHeight_ = 0;
};

newui::SyncReturn FrameClosed(newui::Frame& frame) {
    printf("Frame (%p, hwnd: %p) closed, exiting application.\n", &frame, frame.frameHandle());
    return newui::SyncReturn::Handled;
}

int main() {
    printf("newui %s - DirectWrite bridging spike\n", newui::version());

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("directwrite1");
    app.setFrame(&frame);

    frame.setTitle("DirectWrite Spike");
    frame.setBounds(newui::Rect(10, 10, 640, 240));
    frame.onClosed += FrameClosed;

    newui::RootView& root = frame.getView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(0.0f);
    rootLayout->setPadding(0.0f);
    root.setLayout(std::move(rootLayout));

    auto* textView = new DirectWriteHelloView();
    textView->setVisible(true);
    textView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    textView->style().setBackgroundColor(newui::Color(0xffffffu, false));
    root.addChild(textView);

    app.run();

    return 0;
}

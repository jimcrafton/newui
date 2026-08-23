
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
#include <wincodec.h>

#include <cstdio>
#include <string>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

#include <comdef.h>
#include <comip.h>


_COM_SMARTPTR_TYPEDEF(ID2D1Factory, __uuidof(ID2D1Factory));
_COM_SMARTPTR_TYPEDEF(ID2D1RenderTarget, __uuidof(ID2D1RenderTarget));
_COM_SMARTPTR_TYPEDEF(ID2D1SolidColorBrush, __uuidof(ID2D1SolidColorBrush));
_COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
_COM_SMARTPTR_TYPEDEF(IDWriteTextFormat, __uuidof(IDWriteTextFormat));
_COM_SMARTPTR_TYPEDEF(IDWriteTextLayout, __uuidof(IDWriteTextLayout));
_COM_SMARTPTR_TYPEDEF(IWICImagingFactory, __uuidof(IWICImagingFactory));
_COM_SMARTPTR_TYPEDEF(IWICBitmap, __uuidof(IWICBitmap));
_COM_SMARTPTR_TYPEDEF(IWICBitmapLock, __uuidof(IWICBitmapLock));





class Blend2DTextRenderer : public IDWriteTextRenderer {
    size_t refCount = 0;
public:
    Blend2DTextRenderer(BLContext& ctx) :m_ctx(ctx) {}
    BLContext& m_ctx;
    BLRgba32 m_textColor = BLRgba32(0, 0, 0, 255);

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override {
        if (__uuidof(IDWriteTextRenderer) == riid || __uuidof(IDWriteLocalizedStrings) == riid || __uuidof(IUnknown) == riid) {
            *ppvObject = this;
            AddRef();
            return S_OK;
        }
        *ppvObject = nullptr;
        return E_NOINTERFACE;
    }


    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&refCount); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG res = InterlockedDecrement(&refCount);
        if (res == 0) delete this;
        return res;
    }


    HRESULT STDMETHODCALLTYPE DrawGlyphRun(
        void* clientDrawingContext,
        FLOAT baselineOriginX,
        FLOAT baselineOriginY,
        DWRITE_MEASURING_MODE measuringMode,
        DWRITE_GLYPH_RUN const* glyphRun,
        DWRITE_GLYPH_RUN_DESCRIPTION const* glyphRunDescription,
        IUnknown* clientDrawingEffect) override
    {
        IDWriteFontFace* fontFace = glyphRun->fontFace;
        UINT32 glyphCount = glyphRun->glyphCount;
        //std::unique_lock<...> lock; // if thread-safe context needed
        auto indices = glyphRun->glyphIndices;
        auto advances = glyphRun->glyphAdvances;
        auto offsets = glyphRun->glyphOffsets;
        BLPath blPath;

        m_ctx.fill_path(blPath, m_textColor);
        return S_OK;

    }

    HRESULT STDMETHODCALLTYPE DrawUnderline(void*, FLOAT, FLOAT, DWRITE_UNDERLINE const*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*, FLOAT, FLOAT, DWRITE_STRIKETHROUGH const*, IUnknown*) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }
    
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*, BOOL* isDisabled) override { 
        *isDisabled = FALSE; return S_OK; 
    }

    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*, DWRITE_MATRIX* transform) override {
        transform->m11 = 1.0f; transform->m22 = 1.0f;
        transform->m12 = 0.0f; transform->m21 = 0.0f;
        transform->dx = 0.0f; transform->dy = 0.0f;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*, FLOAT* pixelsPerDip) override { 
        *pixelsPerDip = 1.0f; 
        return S_OK; 
    }
};



class DWView : public newui::SubView {
public:
    DWView() {
        onSizeChanged.add(this, &DWView::onSize);
    }

    ~DWView() override {
        
    }

    newui::SyncReturn onSize(newui::View&, const newui::Size& sz) {
        wicBitmap = nullptr;
        renderTarget = nullptr;

        if (nullptr == d2dFactory) {
            ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2dFactory);
        }

        if (nullptr == dwriteFactory) {
            ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwriteFactory));
        }
        if (nullptr == wicFactory) {
            ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&wicFactory));
        }


        wicFactory->CreateBitmap(
            static_cast<UINT>(sz.width), static_cast<UINT>(sz.height), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnDemand, &wicBitmap);

        if (nullptr == renderTarget) {
            D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
                D2D1_RENDER_TARGET_TYPE_DEFAULT,
                D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

            d2dFactory->CreateWicBitmapRenderTarget(wicBitmap, props, &renderTarget);
        }
        
        if (nullptr == format) {
            DWRITE_FONT_WEIGHT weight = font.bold() ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
            DWRITE_FONT_STYLE style = font.italic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;

            dwriteFactory->CreateTextFormat(
                L"Segoe UI", nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
                16, L"en-us", &format);
        }
        


        return newui::SyncReturn::Handled;
    }

    void paint(BLContext& ctx) override {
        const int width = static_cast<int>(bounds().width());
        const int height = static_cast<int>(bounds().height());
        if (width <= 0 || height <= 0) {
            return;
        }

        
        
        renderTarget->BeginDraw();
        renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        ID2D1SolidColorBrushPtr brush;
        HRESULT brushHr = renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(textColor.r, textColor.g, textColor.b, textColor.a), &brush);

        IDWriteTextLayoutPtr textLayout;
        

        HRESULT layoutHr = dwriteFactory->CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), format,
            static_cast<float>(width), static_cast<float>(height), &textLayout);

        renderTarget->DrawTextLayout( D2D1::Point2F(0.0f, 0.0f), textLayout, brush);
    
        renderTarget->EndDraw();


        WICRect lockRect{ 0, 0, width, height };
        IWICBitmapLockPtr lock;
        HRESULT lockHr = wicBitmap->Lock(&lockRect, WICBitmapLockRead, &lock);

        UINT stride = 0;
        HRESULT strideHr = lock->GetStride(&stride);
        UINT lockedBufferSize = 0;
        WICInProcPointer lockedData = nullptr;
        HRESULT dataHr = lock->GetDataPointer(&lockedBufferSize, &lockedData);
        BLImage textImage;
        textImage.create_from_data(width, height, BL_FORMAT_PRGB32, lockedData, static_cast<intptr_t>(stride));

        ctx.save();
        ctx.blit_image(BLPoint(0, 0), textImage);
        ctx.restore();

    }


    std::wstring text = L"Hello World";

    newui::Font font;
    newui::Color textColor;
    IWICBitmapPtr wicBitmap;
    IDWriteTextFormatPtr format;
    ID2D1FactoryPtr d2dFactory;
    IDWriteFactoryPtr dwriteFactory;
    IWICImagingFactoryPtr wicFactory;
    ID2D1RenderTargetPtr renderTarget;
    
};


int main() {    

    newui::Frame frame;

    newui::Application& app = newui::Application::instance();
    app.setName("directwrite1");
    app.setFrame(&frame);

    frame.setTitle("DirectWrite Custom Renderer");
    frame.setBounds(newui::Rect(10, 10, 640, 240));
    

    newui::RootView& root = frame.rootView();
    root.style().setBackgroundColor(newui::UIColorManager::colorFor(newui::UIColorRole::WindowBackground));

    auto rootLayout = std::make_unique<newui::FlexLayout>(newui::Orientation::Vertical);
    rootLayout->setSpacing(0.0f);
    rootLayout->setPadding(0.0f);
    root.setLayout(std::move(rootLayout));

    auto* textView = new DWView();
    textView->setVisible(true);
    textView->setLayoutParams(std::make_unique<newui::FlexLayoutParams>(1.0f));
    textView->style().setBackgroundColor(newui::Color(0xffffffu, false));
    root.addChild(textView);

    app.run();

    return 0;
}
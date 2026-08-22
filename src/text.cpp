#include "newui/text.h"

#include "newui/uicolormanager.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <comdef.h>
#include <comip.h>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace newui::text {

    namespace {

        // MSVC's own COM smart pointer (_com_ptr_t, <comip.h>) rather
        // than hand-written Release() calls - AddRef/Release are handled
        // entirely by these typedefs' own copy/assignment/destructor.
        // <comdef.h> (needed for _COM_SMARTPTR_TYPEDEF's _com_error
        // support) declares a *global* `Font` that collides with
        // newui::Font - see text.h's own top-of-file comment on why that
        // means these typedefs, and every other D2D/DWrite/WIC/COM type,
        // stay confined to this .cpp (via DirectWriteResources::Impl/
        // TextRenderer::Impl below) and are never named in the header.
        _COM_SMARTPTR_TYPEDEF(ID2D1Factory, __uuidof(ID2D1Factory));
        _COM_SMARTPTR_TYPEDEF(ID2D1RenderTarget, __uuidof(ID2D1RenderTarget));
        _COM_SMARTPTR_TYPEDEF(ID2D1SolidColorBrush, __uuidof(ID2D1SolidColorBrush));
        _COM_SMARTPTR_TYPEDEF(IDWriteFactory, __uuidof(IDWriteFactory));
        _COM_SMARTPTR_TYPEDEF(IDWriteTextFormat, __uuidof(IDWriteTextFormat));
        _COM_SMARTPTR_TYPEDEF(IDWriteTextLayout, __uuidof(IDWriteTextLayout));
        _COM_SMARTPTR_TYPEDEF(IWICImagingFactory, __uuidof(IWICImagingFactory));
        _COM_SMARTPTR_TYPEDEF(IWICBitmap, __uuidof(IWICBitmap));
        _COM_SMARTPTR_TYPEDEF(IWICBitmapLock, __uuidof(IWICBitmapLock));

        // Same MultiByteToWideChar()-based conversion dialogs.cpp's own
        // (file-local) Utf8ToWide() already uses - IDWriteFactory::
        // CreateTextFormat() needs a wide font name, Font::name() is a
        // plain (UTF-8) std::string.
        std::wstring Utf8ToWide(const std::string& text) {
            if (text.empty()) {
                return std::wstring();
            }
            int required = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
            if (required <= 0) {
                return std::wstring();
            }
            std::wstring result(static_cast<size_t>(required), L'\0');
            ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), required);
            return result;
        }

        // Shared by TextRenderer::Impl and TextLayoutEngine::Impl - both
        // need "given a Font, get/cache a matching IDWriteTextFormat",
        // resolved lazily and re-resolved only when the font actually
        // changes, the same shape Font::blFont() itself already uses for
        // BLFont. Factored out here rather than each hand-rolling its
        // own copy of the same ~20 lines.
        class TextFormatCache {
        public:
            // Returns the cached format for font, (re)creating it first
            // if font's own name/size/bold/italic differ from what's
            // cached (or nothing is cached yet) - nullptr if creation
            // fails. The returned pointer's identity itself signals
            // whether a rebuild actually happened: it's the same pointer
            // as the previous resolve() call unless font's inputs
            // changed just now.
            IDWriteTextFormat* resolve(const Font& font) {
                if (format_ != nullptr && lastName_ == font.name() && lastSize_ == font.size()
                    && lastBold_ == font.bold() && lastItalic_ == font.italic()) {
                    return format_.GetInterfacePtr();
                }

                format_ = nullptr;

                DWRITE_FONT_WEIGHT weight = font.bold() ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL;
                DWRITE_FONT_STYLE style = font.italic() ? DWRITE_FONT_STYLE_ITALIC : DWRITE_FONT_STYLE_NORMAL;
                std::wstring fontName = Utf8ToWide(font.name());
                if (fontName.empty()) {
                    fontName = L"Segoe UI";
                }

                HRESULT hr = DirectWriteResources::dwriteFactory().CreateTextFormat(
                    fontName.c_str(), nullptr, weight, style, DWRITE_FONT_STRETCH_NORMAL,
                    font.size(), L"en-us", &format_);
                if (FAILED(hr)) {
                    return nullptr;
                }

                lastName_ = font.name();
                lastSize_ = font.size();
                lastBold_ = font.bold();
                lastItalic_ = font.italic();
                return format_.GetInterfacePtr();
            }

        private:
            IDWriteTextFormatPtr format_;
            std::string lastName_;
            float lastSize_ = 0.0f;
            bool lastBold_ = false;
            bool lastItalic_ = false;
        };

    }  // namespace

    Caret::~Caret() {
        stop();
    }

    void Caret::setPosition(const TextPosition& position) {
        position_ = position;
        visible_ = true;
    }

    void Caret::start(RunLoop& runLoop) {
        UINT blinkMs = ::GetCaretBlinkTime();
        if (blinkMs == 0 || blinkMs == INFINITE) {
            // Accessibility setting disables blinking entirely.
            blinkMs = 0;
        }
        start(runLoop, std::chrono::milliseconds(blinkMs));
    }

    void Caret::start(RunLoop& runLoop, std::chrono::milliseconds interval) {
        if (active_) {
            return;
        }
        active_ = true;
        visible_ = true;
        runLoop_ = &runLoop;

        if (interval.count() <= 0) {
            // Stay solidly visible, no timer needed at all.
            return;
        }

        timerHandle_ = runLoop.postDelayed(interval, [this]() {
            return onBlinkTick();
            });
    }

    void Caret::stop() {
        if (!active_) {
            return;
        }
        if (runLoop_ != nullptr && timerHandle_ != RunLoop::kInvalidTimerHandle) {
            runLoop_->cancelDelayed(timerHandle_);
        }
        timerHandle_ = RunLoop::kInvalidTimerHandle;
        runLoop_ = nullptr;
        active_ = false;
        visible_ = true;
    }

    bool Caret::onBlinkTick() {
        visible_ = !visible_;
        onVisibilityChanged(*this);
        return false;  // keep blinking until stop()/the destructor cancels this
    }

    void Caret::draw(BLContext& ctx, const Point& topLeft, float height) const {
        if (!isVisible()) {
            return;
        }
        float width = static_cast<float>(systemCaretWidth());
        ctx.save();
        ctx.set_fill_style(color_.toBLRgba32());
        ctx.fill_rect(BLRect(topLeft.x, topLeft.y, width, height));
        ctx.restore();
    }

    UINT Caret::systemCaretWidth() {
        UINT width = 1;
        ::SystemParametersInfo(SPI_GETCARETWIDTH, 0, &width, 0);
        return width;
    }

    void TextSelection::setRange(const TextRange& range) {
        bool canChange = true;
        onBeforeSelectionChanged(*this, range, canChange);
        if (!canChange) {
            return;
        }
        ranges_.clear();
        ranges_.push_back(range);
        onAfterSelectionChanged(*this, range);
    }

    void TextSelection::addRange(const TextRange& range) {
        bool canChange = true;
        onBeforeSelectionChanged(*this, range, canChange);
        if (!canChange) {
            return;
        }
        ranges_.push_back(range);
        onAfterSelectionChanged(*this, range);
    }

    void TextSelection::clear() {
        TextRange none;
        bool canChange = true;
        onBeforeSelectionChanged(*this, none, canChange);
        if (!canChange) {
            return;
        }
        ranges_.clear();
        onAfterSelectionChanged(*this, none);
    }

    bool TextSelection::contains(size_t offset) const {
        for (const TextRange& range : ranges_) {
            if (range.contains(offset)) {
                return true;
            }
        }
        return false;
    }

    Color TextSelection::color() const {
        if (hasColorOverride_) {
            return colorOverride_;
        }
        return UIColorManager::colorFor(UIColorRole::HighlightBackground);
    }

    void TextSelection::draw(BLContext& ctx, const std::vector<Rect>& rects) const {
        // Deliberately NOT paired 1:1 against ranges_ by index (an
        // earlier version was, via min(ranges_.size(), rects.size())) -
        // a single range wrapped across several visual lines legitimately
        // produces several rects from one TextLayoutEngine::hitTestRange()
        // call (see its own doc comment), so index-pairing against
        // ranges_ silently dropped every rect past the first for a
        // multi-line selection. rects is already exactly "every highlight
        // rect to paint" by the time a caller builds it (TextField/
        // TextController flatten every range's own hitTestRange() output
        // into one vector first) - just draw all of them.
        if (rects.empty()) {
            return;
        }

        ctx.save();
        ctx.set_fill_style(color().toBLRgba32());
        for (const Rect& r : rects) {
            ctx.fill_rect(BLRect(r.left(), r.top(), r.width(), r.height()));
        }
        ctx.restore();
    }

    wchar_t TextStorage::at(size_t offset) const {
        if (offset >= text_.size()) {
            return L'\0';
        }
        return text_[offset];
    }

    std::wstring TextStorage::substring(const TextRange& range) const {
        size_t clampedStart = range.start() < text_.size() ? range.start() : text_.size();
        size_t clampedEnd = range.end() < text_.size() ? range.end() : text_.size();
        if (clampedStart >= clampedEnd) {
            return std::wstring();
        }
        return text_.substr(clampedStart, clampedEnd - clampedStart);
    }

    void TextStorage::insert(size_t offset, const std::wstring& text) {
        replace(TextRange(offset, 0), text);
    }

    void TextStorage::remove(const TextRange& range) {
        replace(range, std::wstring());
    }

    void TextStorage::replace(const TextRange& range, const std::wstring& replacement) {
        size_t clampedStart = range.start() < text_.size() ? range.start() : text_.size();
        size_t clampedEnd = range.end() < text_.size() ? range.end() : text_.size();
        if (clampedEnd < clampedStart) {
            clampedEnd = clampedStart;
        }
        text_.replace(clampedStart, clampedEnd - clampedStart, replacement);
    }

    bool TextModel::fireBeforeChar(size_t offset, wchar_t ch, CharChangeKind kind) {
        bool canChange = true;
        onBeforeChar(*this, offset, ch, kind, canChange);
        return canChange;
    }

    bool TextModel::fireBeforeRangeChanged(const TextRange& range, const std::wstring& replacement) {
        bool canChange = true;
        onBeforeRangeChanged(*this, range, replacement, canChange);
        return canChange;
    }

    void TextModel::setText(const std::wstring& text) {
        TextRange fullRange(0, storage_.length());
        if (!fireBeforeRangeChanged(fullRange, text)) {
            return;
        }
        storage_.setText(text);
        onAfterRangeChanged(*this, fullRange, text);
        notifyChanged();
    }

    void TextModel::insert(size_t offset, const std::wstring& text) {
        size_t clampedOffset = offset < storage_.length() ? offset : storage_.length();

        if (text.size() == 1) {
            if (!fireBeforeChar(clampedOffset, text[0], CharChangeKind::Inserted)) {
                return;
            }
            storage_.insert(offset, text);
            onAfterChar(*this, clampedOffset, text[0], CharChangeKind::Inserted);
        } else {
            TextRange range(clampedOffset, 0);
            if (!fireBeforeRangeChanged(range, text)) {
                return;
            }
            storage_.insert(offset, text);
            onAfterRangeChanged(*this, range, text);
        }
        notifyChanged();
    }

    void TextModel::remove(const TextRange& range) {
        size_t clampedStart = range.start() < storage_.length() ? range.start() : storage_.length();
        size_t clampedEnd = range.end() < storage_.length() ? range.end() : storage_.length();
        if (clampedEnd < clampedStart) {
            clampedEnd = clampedStart;
        }
        size_t clampedLength = clampedEnd - clampedStart;

        if (clampedLength == 1) {
            wchar_t removedChar = storage_.at(clampedStart);
            if (!fireBeforeChar(clampedStart, removedChar, CharChangeKind::Removed)) {
                return;
            }
            storage_.remove(range);
            onAfterChar(*this, clampedStart, removedChar, CharChangeKind::Removed);
        } else {
            TextRange clampedRange(clampedStart, clampedLength);
            if (!fireBeforeRangeChanged(clampedRange, std::wstring())) {
                return;
            }
            storage_.remove(range);
            onAfterRangeChanged(*this, clampedRange, std::wstring());
        }
        notifyChanged();
    }

    void TextModel::replace(const TextRange& range, const std::wstring& replacement) {
        size_t clampedStart = range.start() < storage_.length() ? range.start() : storage_.length();
        size_t clampedEnd = range.end() < storage_.length() ? range.end() : storage_.length();
        if (clampedEnd < clampedStart) {
            clampedEnd = clampedStart;
        }
        TextRange clampedRange(clampedStart, clampedEnd - clampedStart);

        if (!fireBeforeRangeChanged(clampedRange, replacement)) {
            return;
        }
        storage_.replace(range, replacement);
        onAfterRangeChanged(*this, clampedRange, replacement);
        notifyChanged();
    }

    std::any TextModel::value(const std::any& key) {
        return std::any(storage_.text());
    }

    void TextModel::setValue(const std::any& newValue, const std::any& key) {
        if (const std::wstring* text = std::any_cast<std::wstring>(&newValue)) {
            setText(*text);
        }
    }

    void TextModel::clear() {
        TextRange fullRange(0, storage_.length());
        if (!fireBeforeRangeChanged(fullRange, std::wstring())) {
            return;
        }
        storage_.setText(std::wstring());
        Model::clear();  // fires onCleared()
        onAfterRangeChanged(*this, fullRange, std::wstring());
        notifyChanged();
    }

    struct DirectWriteResources::Impl {
        ID2D1FactoryPtr d2dFactory;
        IDWriteFactoryPtr dwriteFactory;
        IWICImagingFactoryPtr wicFactory;
    };

    DirectWriteResources::DirectWriteResources() : impl_(std::make_unique<Impl>()) {
        ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &impl_->d2dFactory);
        ::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(&impl_->dwriteFactory));

        // IWICImagingFactory only ever comes from CoCreateInstance, which
        // needs COM initialized on this thread - the real app already
        // gets this for free (RunLoop::run() calls OleInitialize()), but
        // e.g. a unit test driving TextRenderer directly without ever
        // running a RunLoop doesn't. CoInitializeEx tolerates being
        // called more than once per thread (refcounted) as long as the
        // apartment model agrees each time - S_FALSE ("already
        // initialized") is expected and fine here, not just S_OK.
        // Deliberately never balanced with CoUninitialize - same
        // "process-lifetime singleton, no clean per-thread teardown
        // hook exists" reasoning already accepted for not releasing
        // d2dFactory/dwriteFactory any earlier than process exit either.
        HRESULT comHr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(comHr) || comHr == S_FALSE) {
            HRESULT wicHr = ::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                __uuidof(IWICImagingFactory), reinterpret_cast<void**>(&impl_->wicFactory));
            (void)wicHr;
        }
    }

    // Defined here (not = default in text.h) - Impl is only a complete
    // type in this .cpp, and std::unique_ptr<Impl>'s destructor needs
    // that to delete it.
    DirectWriteResources::~DirectWriteResources() = default;

    DirectWriteResources& DirectWriteResources::instance() {
        static DirectWriteResources instance;
        return instance;
    }

    ID2D1Factory& DirectWriteResources::d2dFactory() {
        ID2D1Factory* factory = instance().impl_->d2dFactory.GetInterfacePtr();
        if (factory == nullptr) {
            // D2D1CreateFactory() failing at all is not a mundane,
            // recoverable condition the way a single CreateTextFormat()/
            // CreateBitmap() call failing is (those are checked and
            // degrade to a no-op render() - see TextRenderer) - it means
            // this process's whole Direct2D stack is unavailable, which
            // shouldn't happen on any supported Windows version. Throwing
            // matches Button::paint()'s own precedent for a comparably
            // "should never happen" condition (its own BLFont-resolution
            // check, controls.cpp) rather than letting every caller of
            // this accessor null-dereference instead.
            throw std::runtime_error("newui::text::DirectWriteResources::d2dFactory: D2D1CreateFactory failed");
        }
        return *factory;
    }

    IDWriteFactory& DirectWriteResources::dwriteFactory() {
        IDWriteFactory* factory = instance().impl_->dwriteFactory.GetInterfacePtr();
        if (factory == nullptr) {
            throw std::runtime_error("newui::text::DirectWriteResources::dwriteFactory: DWriteCreateFactory failed");
        }
        return *factory;
    }

    IWICImagingFactory& DirectWriteResources::wicFactory() {
        IWICImagingFactory* factory = instance().impl_->wicFactory.GetInterfacePtr();
        if (factory == nullptr) {
            throw std::runtime_error("newui::text::DirectWriteResources::wicFactory: "
                "CoInitializeEx/CoCreateInstance(CLSID_WICImagingFactory) failed");
        }
        return *factory;
    }

    struct TextRenderer::Impl {
        ID2D1RenderTargetPtr renderTarget;
        IWICBitmapPtr wicBitmap;
        TextFormatCache textFormat;
    };

    TextRenderer::TextRenderer() : impl_(std::make_unique<Impl>()) {}

    // impl_'s own _com_ptr_t members, and pixelBuffer_ (a plain
    // std::vector), all release themselves - nothing left to do here.
    TextRenderer::~TextRenderer() = default;

    bool TextRenderer::ensureRenderTarget(int width, int height) {
        if (impl_->renderTarget != nullptr && width == bufferWidth_ && height == bufferHeight_) {
            return true;
        }

        // A WIC bitmap render target is permanently bound to the bitmap
        // it was created with (unlike the DC-render-target/BindDC()
        // approach this replaced - see this class's own doc comment), so
        // a size change means rebuilding the bitmap/render target
        // together, not just re-binding.
        //
        // CreateBitmap(), not CreateBitmapFromMemory() - an earlier
        // version passed our own buffer to CreateBitmapFromMemory()
        // expecting D2D to draw straight into it, but that call *copies*
        // the seed buffer into the WIC bitmap's own internal storage
        // (confirmed live: every HRESULT in this whole call chain came
        // back S_OK, yet the buffer we kept reading from afterward
        // stayed all-zero) - it is not a live-shared backing store. The
        // real "read pixels back out" mechanism WIC offers is
        // IWICBitmap::Lock(), used in render() below after EndDraw() -
        // so there's no seed buffer to allocate here at all.
        IWICBitmapPtr wicBitmap;
        HRESULT hr = DirectWriteResources::wicFactory().CreateBitmap(
            static_cast<UINT>(width), static_cast<UINT>(height), GUID_WICPixelFormat32bppPBGRA,
            WICBitmapCacheOnDemand, &wicBitmap);
        if (FAILED(hr)) {
            return false;
        }

        D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

        ID2D1RenderTargetPtr renderTarget;
        hr = DirectWriteResources::d2dFactory().CreateWicBitmapRenderTarget(
            wicBitmap.GetInterfacePtr(), props, &renderTarget);
        if (FAILED(hr)) {
            return false;
        }

        impl_->wicBitmap = wicBitmap;
        impl_->renderTarget = renderTarget;
        bufferWidth_ = width;
        bufferHeight_ = height;
        return true;
    }

    void TextRenderer::render(BLContext& ctx, int width, int height, const std::wstring& text,
            const Font& font, const Color& textColor, float scrollOffsetY, bool wordWrap) {
        if (width <= 0 || height <= 0) {
            return;
        }
        IDWriteTextFormat* format = impl_->textFormat.resolve(font);
        if (!ensureRenderTarget(width, height) || format == nullptr) {
            return;
        }

        impl_->renderTarget->BeginDraw();
        // Fully transparent clear - only the glyphs DrawTextLayout()
        // below should end up opaque; blit_image() below composites the
        // rest of this bitmap's (untouched, still-zero) alpha as
        // "nothing here" over whatever the caller already painted.
        impl_->renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));

        ID2D1SolidColorBrushPtr brush;
        HRESULT brushHr = impl_->renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(textColor.r, textColor.g, textColor.b, textColor.a), &brush);

        IDWriteTextLayoutPtr textLayout;
        HRESULT layoutHr = DirectWriteResources::dwriteFactory().CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), format,
            static_cast<float>(width), static_cast<float>(height), &textLayout);

        if (SUCCEEDED(layoutHr)) {
            textLayout->SetWordWrapping(wordWrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        }
        if (SUCCEEDED(brushHr) && SUCCEEDED(layoutHr)) {
            // scrollOffsetY shifts the whole layout up before rasterizing -
            // see this method's own doc comment (text.h) on why this is
            // safe/correct regardless of how tall the real content is,
            // even though textLayout itself was only ever built against
            // this render target's own small height.
            impl_->renderTarget->DrawTextLayout(
                D2D1::Point2F(0.0f, -scrollOffsetY), textLayout.GetInterfacePtr(), brush.GetInterfacePtr());
        }
        // brush/textLayout (_com_ptr_t) release themselves at scope exit.

        // EndDraw() has to be called to close out BeginDraw() regardless
        // of whether the brush/layout above succeeded - abandoning a
        // draw session without it leaves the render target in an
        // inconsistent state for the next render() call.
        HRESULT endHr = impl_->renderTarget->EndDraw();
        if (endHr == D2DERR_RECREATE_TARGET) {
            // Device loss (rare for a software render target, but a real
            // integration still has to handle it) - drop the render
            // target/bitmap and force bufferWidth_/bufferHeight_ back to
            // 0 so ensureRenderTarget() rebuilds everything fresh next
            // render() instead of drawing through a now-invalid target.
            impl_->renderTarget = nullptr;
            impl_->wicBitmap = nullptr;
            bufferWidth_ = 0;
            bufferHeight_ = 0;
            return;
        }
        if (FAILED(brushHr) || FAILED(layoutHr) || FAILED(endHr)) {
            return;
        }

        // Read the drawn pixels back out via IWICBitmap::Lock() - see
        // ensureRenderTarget()'s own comment on why this, not a caller-
        // supplied buffer, is WIC's real "get pixels out" mechanism.
        WICRect lockRect{0, 0, width, height};
        IWICBitmapLockPtr lock;
        HRESULT lockHr = impl_->wicBitmap->Lock(&lockRect, WICBitmapLockRead, &lock);
        if (FAILED(lockHr)) {
            return;
        }

        UINT stride = 0;
        HRESULT strideHr = lock->GetStride(&stride);
        UINT lockedBufferSize = 0;
        WICInProcPointer lockedData = nullptr;
        HRESULT dataHr = lock->GetDataPointer(&lockedBufferSize, &lockedData);
        if (FAILED(strideHr) || FAILED(dataHr) || lockedData == nullptr) {
            return;
        }

        // No copy: wraps the locked bitmap memory directly for the
        // duration of this call - lock stays alive (keeping the bitmap
        // locked) until it goes out of scope below. Its pixel layout
        // (32bpp top-down premultiplied BGRA) is exactly BL_FORMAT_PRGB32 -
        // same fact ThemedViewStyle::paint() and gfx::Image already rely
        // on for their own (GDI-backed) buffers.
        BLImage textImage;
        textImage.create_from_data(width, height, BL_FORMAT_PRGB32, lockedData, static_cast<intptr_t>(stride));

        ctx.save();
        ctx.blit_image(BLPoint(0, 0), textImage);
        ctx.restore();
        // lock unlocks the bitmap automatically at scope exit.
    }

    struct TextLayoutEngine::Impl {
        IDWriteTextLayoutPtr layout;
        TextFormatCache textFormat;

        // Non-owning - purely to detect "the font actually changed"
        // (compared by identity against whatever TextFormatCache::
        // resolve() just returned) without update() needing its own
        // separate copy of TextFormatCache's own name/size/bold/italic
        // tracking. A stale/dangling value here is harmless: it's never
        // dereferenced, only ever compared against a fresh resolve()
        // result.
        IDWriteTextFormat* lastFormat = nullptr;
    };

    TextLayoutEngine::TextLayoutEngine() : impl_(std::make_unique<Impl>()) {}

    // impl_'s own _com_ptr_t/TextFormatCache members release themselves -
    // nothing left to do here.
    TextLayoutEngine::~TextLayoutEngine() = default;

    bool TextLayoutEngine::update(const TextStorage& storage, const Font& font, float maxWidth, float maxHeight, bool wordWrap) {
        IDWriteTextFormat* format = impl_->textFormat.resolve(font);
        if (format == nullptr) {
            return false;
        }

        const std::wstring& text = storage.text();
        bool needsRebuild = impl_->layout == nullptr
            || format != impl_->lastFormat
            || lastText_ != text
            || lastMaxWidth_ != maxWidth
            || lastMaxHeight_ != maxHeight
            || lastWordWrap_ != wordWrap;
        if (!needsRebuild) {
            return true;
        }

        IDWriteTextLayoutPtr layout;
        HRESULT hr = DirectWriteResources::dwriteFactory().CreateTextLayout(
            text.c_str(), static_cast<UINT32>(text.size()), format, maxWidth, maxHeight, &layout);
        if (FAILED(hr)) {
            impl_->layout = nullptr;
            return false;
        }
        layout->SetWordWrapping(wordWrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);

        impl_->layout = layout;
        impl_->lastFormat = format;
        lastText_ = text;
        lastMaxWidth_ = maxWidth;
        lastMaxHeight_ = maxHeight;
        lastWordWrap_ = wordWrap;
        return true;
    }

    std::vector<Rect> TextLayoutEngine::hitTestRange(const TextRange& range) const {
        std::vector<Rect> result;
        if (impl_->layout == nullptr) {
            return result;
        }

        // First call (null buffer) exists purely to learn how many
        // DWRITE_HIT_TEST_METRICS a real call will need (a multi-line
        // range spans more than one contiguous run) - its own HRESULT is
        // expected to report "not enough buffer" and is deliberately not
        // treated as a real failure; actualCount == 0 (an empty/invalid
        // range) is the only thing checked here.
        UINT32 actualCount = 0;
        impl_->layout->HitTestTextRange(static_cast<UINT32>(range.start()), static_cast<UINT32>(range.length()),
            0.0f, 0.0f, nullptr, 0, &actualCount);
        if (actualCount == 0) {
            return result;
        }

        std::vector<DWRITE_HIT_TEST_METRICS> metrics(actualCount);
        HRESULT hr = impl_->layout->HitTestTextRange(static_cast<UINT32>(range.start()),
            static_cast<UINT32>(range.length()), 0.0f, 0.0f, metrics.data(), actualCount, &actualCount);
        if (FAILED(hr)) {
            return result;
        }

        result.reserve(actualCount);
        for (UINT32 i = 0; i < actualCount; ++i) {
            const DWRITE_HIT_TEST_METRICS& m = metrics[i];
            result.emplace_back(m.left, m.top, m.width, m.height);
        }
        return result;
    }

    void TextLayoutEngine::hitTestPosition(const TextPosition& position, Point& outTopLeft, float& outHeight) const {
        outTopLeft = Point();
        outHeight = 0.0f;
        if (impl_->layout == nullptr || !position.isValid()) {
            return;
        }

        FLOAT pointX = 0.0f;
        FLOAT pointY = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        HRESULT hr = impl_->layout->HitTestTextPosition(
            static_cast<UINT32>(position.offset()), FALSE, &pointX, &pointY, &metrics);
        if (FAILED(hr)) {
            return;
        }

        outTopLeft = Point(pointX, pointY);
        outHeight = metrics.height;
    }

    TextPosition TextLayoutEngine::hitTestPoint(const Point& localPoint) const {
        if (impl_->layout == nullptr) {
            return TextPosition();
        }

        BOOL isTrailingHit = FALSE;
        BOOL isInside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        HRESULT hr = impl_->layout->HitTestPoint(localPoint.x, localPoint.y, &isTrailingHit, &isInside, &metrics);
        if (FAILED(hr)) {
            return TextPosition();
        }

        size_t offset = metrics.textPosition;
        if (isTrailingHit) {
            offset += metrics.length;
        }
        return TextPosition(offset);
    }

    TextRange TextLayoutEngine::lineRange(const TextPosition& position) const {
        if (impl_->layout == nullptr || !position.isValid()) {
            return TextRange();
        }

        // Same two-pass pattern hitTestRange() already uses - the first
        // call (null buffer) exists purely to learn the line count.
        UINT32 lineCount = 0;
        impl_->layout->GetLineMetrics(nullptr, 0, &lineCount);
        if (lineCount == 0) {
            return TextRange();
        }

        std::vector<DWRITE_LINE_METRICS> metrics(lineCount);
        HRESULT hr = impl_->layout->GetLineMetrics(metrics.data(), lineCount, &lineCount);
        if (FAILED(hr)) {
            return TextRange();
        }

        size_t offset = position.offset();
        size_t lineStart = 0;
        for (UINT32 i = 0; i < lineCount; ++i) {
            size_t lineLength = metrics[i].length;
            size_t lineEnd = lineStart + lineLength;
            bool isLastLine = (i + 1 == lineCount);
            // A boundary offset (exactly lineEnd) belongs to the *next*
            // line's own range - except on the last line, where there is
            // no next line to fall through to.
            if (offset < lineEnd || (isLastLine && offset <= lineEnd)) {
                size_t contentLength = lineLength - metrics[i].newlineLength;
                return TextRange(lineStart, contentLength);
            }
            lineStart = lineEnd;
        }

        // Not normally reachable (offset is always <= the total text
        // length a valid TextPosition can hold) - fall back to the last
        // line found rather than an invalid range.
        const DWRITE_LINE_METRICS& last = metrics[lineCount - 1];
        size_t lastContentLength = last.length - last.newlineLength;
        return TextRange(lineStart - last.length, lastContentLength);
    }

    float TextLayoutEngine::contentHeight() const {
        if (impl_->layout == nullptr) {
            return 0.0f;
        }
        DWRITE_TEXT_METRICS metrics{};
        HRESULT hr = impl_->layout->GetMetrics(&metrics);
        if (FAILED(hr)) {
            return 0.0f;
        }
        return metrics.height;
    }

}

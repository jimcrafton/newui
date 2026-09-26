#include "newui/text.h"

#include "newui/uicolormanager.h"
#include "newui/utils.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <comdef.h>
#include <comip.h>

#include <algorithm>
#include <map>

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
                std::wstring fontName = utf8ToWide(font.name());
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

    void drawTextDecoration(BLContext& ctx, const TextDecoration& decoration, const std::vector<Rect>& rects) {
        const BLRgba32 color = decoration.color.toBLRgba32();
        const double thickness = decoration.thickness > 0.0f ? decoration.thickness : 1.0f;
        ctx.save();
        ctx.set_stroke_width(thickness);
        ctx.set_stroke_style(color);
        ctx.set_fill_style(color);
        for (const Rect& r : rects) {
            if (r.width() <= 0.0f || r.height() <= 0.0f) {
                continue;
            }
            switch (decoration.kind) {
            case TextDecorationKind::Background:
                ctx.fill_rect(r.left(), r.top(), r.width(), r.height());
                break;
            case TextDecorationKind::Underline: {
                const double y = r.bottom() - thickness * 0.5;
                ctx.stroke_line(r.left(), y, r.right(), y);
                break;
            }
            case TextDecorationKind::Squiggle: {
                // A zigzag along the bottom of the line: 2px up and down every 2px.
                const double amplitude = 1.5;
                const double step = 2.0;
                const double baseY = r.bottom() - amplitude - thickness * 0.5;
                BLPath wave;
                wave.move_to(r.left(), baseY + amplitude);
                bool up = true;
                for (double x = r.left() + step; x <= r.right(); x += step) {
                    wave.line_to(x, up ? baseY - amplitude : baseY + amplitude);
                    up = !up;
                }
                ctx.stroke_path(wave);
                break;
            }
            case TextDecorationKind::Box:
                ctx.stroke_rect(r.left() + thickness * 0.5, r.top() + thickness * 0.5,
                    r.width() - thickness, r.height() - thickness);
                break;
            case TextDecorationKind::RoundBox:
                ctx.stroke_round_rect(r.left() + thickness * 0.5, r.top() + thickness * 0.5,
                    r.width() - thickness, r.height() - thickness, decoration.radius);
                break;
            case TextDecorationKind::None:
                break;
            }
        }
        ctx.restore();
    }

    void TextRenderer::render(BLContext& ctx, int width, int height, const std::wstring& text,
            const Font& font, const Color& textColor, float scrollOffsetY, bool wordWrap,
            const std::vector<TextColorRun>& colorRuns) {
        if (width <= 0 || height <= 0) {
            return;
        }
        IDWriteTextFormat* format = impl_->textFormat.resolve(font);
        if (format == nullptr) {
            return;
        }
        IDWriteTextLayoutPtr textLayout;
        if (FAILED(DirectWriteResources::dwriteFactory().CreateTextLayout(
                text.c_str(), static_cast<UINT32>(text.size()), format,
                static_cast<float>(width), static_cast<float>(height), &textLayout))) {
            return;
        }
        textLayout->SetWordWrapping(wordWrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
        drawLayouts(ctx, width, height,
            { LayoutPiece{ textLayout.GetInterfacePtr(), 0.0f, text.size(), { LayoutSegment{ 0, 0, text.size() } }, {} } },
            textColor, scrollOffsetY, colorRuns, textColor);
    }

    void TextRenderer::drawLayouts(BLContext& ctx, int width, int height, const std::vector<LayoutPiece>& pieces,
            const Color& textColor, float scrollOffsetY, const std::vector<TextColorRun>& colorRuns, const Color& foldColor) {
        if (width <= 0 || height <= 0 || pieces.empty() || !ensureRenderTarget(width, height)) {
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

        // Each run's brush is its drawing effect - DrawTextLayout() draws a range whose effect is an
        // ID2D1Brush with that brush, so no custom IDWriteTextRenderer is needed. One brush per
        // distinct color (a document has thousands of runs but a handful of colors). A layout that
        // outlives this call (TextLayoutEngine's) may still carry an earlier paint's effects - each
        // piece is cleared first, and again once drawn.
        std::map<std::uint32_t, ID2D1SolidColorBrushPtr> runBrushes;
        ID2D1SolidColorBrushPtr foldBrush;
        impl_->renderTarget->CreateSolidColorBrush(D2D1::ColorF(foldColor.r, foldColor.g, foldColor.b, foldColor.a), &foldBrush);
        for (const LayoutPiece& piece : pieces) {
            piece.layout->SetDrawingEffect(nullptr, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(piece.layoutLength) });
            for (const LayoutSegment& segment : piece.segments) {
                const std::size_t segmentEnd = segment.textStart + segment.length;
                for (const TextColorRun& run : colorRuns) {
                    const std::size_t runEnd = run.start + run.length;
                    const std::size_t from = run.start > segment.textStart ? run.start : segment.textStart;
                    const std::size_t to = runEnd < segmentEnd ? runEnd : segmentEnd;
                    if (run.length == 0 || to <= from) {
                        continue;
                    }
                    const std::uint32_t key = run.color.toBLRgba32().value;
                    ID2D1SolidColorBrushPtr& runBrush = runBrushes[key];
                    if (!runBrush && FAILED(impl_->renderTarget->CreateSolidColorBrush(
                            D2D1::ColorF(run.color.r, run.color.g, run.color.b, run.color.a), &runBrush))) {
                        continue;
                    }
                    piece.layout->SetDrawingEffect(runBrush.GetInterfacePtr(), DWRITE_TEXT_RANGE{
                        static_cast<UINT32>(segment.layoutStart + (from - segment.textStart)), static_cast<UINT32>(to - from) });
                }
            }
            if (foldBrush) {
                for (const LayoutSegment& placeholder : piece.placeholders) {
                    piece.layout->SetDrawingEffect(foldBrush.GetInterfacePtr(), DWRITE_TEXT_RANGE{
                        static_cast<UINT32>(placeholder.layoutStart), static_cast<UINT32>(placeholder.length) });
                }
            }
            if (SUCCEEDED(brushHr)) {
                // scrollOffsetY shifts everything up before rasterizing - see
                // render()'s own doc comment (text.h).
                impl_->renderTarget->DrawTextLayout(
                    D2D1::Point2F(0.0f, piece.top - scrollOffsetY), piece.layout, brush.GetInterfacePtr());
            }
        }

        // EndDraw() has to be called to close out BeginDraw() regardless
        // of whether the brush above succeeded - abandoning a draw
        // session without it leaves the render target in an inconsistent
        // state for the next render() call.
        HRESULT endHr = impl_->renderTarget->EndDraw();
        // The brushes belong to this render target - don't leave them attached to layouts that
        // outlive this call.
        for (const LayoutPiece& piece : pieces) {
            piece.layout->SetDrawingEffect(nullptr, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(piece.layoutLength) });
        }
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
        if (FAILED(brushHr) || FAILED(endHr)) {
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

    // One DirectWrite layout per visual line. A visual line is a line of the text (split at '\n';
    // a "\r\n" pair is one break) - or, where a collapsed TextFold hides text, the lines it joins:
    // the text before the fold, its placeholder, then the text after it. Segments map the
    // layout's characters back to the text. An edit re-lays out only the lines it touched, and
    // drawing touches only the visible ones.
    struct TextLayoutEngine::Impl {
        struct Placeholder {
            std::size_t layoutStart = 0;
            std::size_t length = 0;
            std::size_t foldStart = 0;
            std::size_t foldEnd = 0;
        };

        struct Line {
            std::size_t start = 0;       // the text offset the line starts at
            std::size_t length = 0;      // its span of the text (hidden text included), not counting the line break
            std::size_t terminator = 0;  // the break's length: 0 (the last line), 1 or 2
            std::size_t textLine = 0;    // the line of the text it starts on
            float baseline = 0.0f;       // its first row's, from top
            float top = 0.0f;
            float height = 0.0f;
            std::wstring text;           // what's laid out: the visible text with placeholders
            std::vector<LayoutSegment> segments;
            std::vector<Placeholder> placeholders;
            std::vector<TextFontRun> runs;   // in layout positions
            IDWriteTextLayoutPtr layout;

            std::size_t end() const { return start + length; }

            // A text offset's layout position; hidden text maps to the start of its placeholder.
            std::size_t toLayout(std::size_t offset) const {
                for (const LayoutSegment& segment : segments) {
                    if (offset >= segment.textStart && offset <= segment.textStart + segment.length) {
                        return segment.layoutStart + (offset - segment.textStart);
                    }
                }
                for (const Placeholder& placeholder : placeholders) {
                    if (offset > placeholder.foldStart && offset < placeholder.foldEnd) {
                        return placeholder.layoutStart;
                    }
                }
                return text.size();
            }

            // A layout position's text offset; inside a placeholder, the nearer edge of its fold.
            std::size_t toText(std::size_t layoutOffset) const {
                for (const Placeholder& placeholder : placeholders) {
                    if (layoutOffset > placeholder.layoutStart && layoutOffset < placeholder.layoutStart + placeholder.length) {
                        return (layoutOffset - placeholder.layoutStart) * 2 <= placeholder.length
                            ? placeholder.foldStart : placeholder.foldEnd;
                    }
                }
                for (const LayoutSegment& segment : segments) {
                    if (layoutOffset >= segment.layoutStart && layoutOffset <= segment.layoutStart + segment.length) {
                        return segment.textStart + (layoutOffset - segment.layoutStart);
                    }
                }
                return end();
            }
        };

        std::vector<Line> lines;
        TextFormatCache textFormat;
        IDWriteTextFormat* lastFormat = nullptr;
        std::size_t lastBuilt = 0;

        // The line holding offset (the last one starting at or before it).
        std::size_t lineForOffset(std::size_t offset) const {
            auto it = std::upper_bound(lines.begin(), lines.end(), offset,
                [](std::size_t value, const Line& line) { return value < line.start; });
            return it == lines.begin() ? 0 : static_cast<std::size_t>(it - lines.begin()) - 1;
        }

        // The line at y (clamped to the first / last).
        std::size_t lineForY(float y) const {
            auto it = std::upper_bound(lines.begin(), lines.end(), y,
                [](float value, const Line& line) { return value < line.top + line.height; });
            return it == lines.end() ? lines.size() - 1 : static_cast<std::size_t>(it - lines.begin());
        }

        static void appendRangeRects(const Line& line, std::size_t layoutStart, std::size_t length, std::vector<Rect>& out) {
            UINT32 count = 0;
            line.layout->HitTestTextRange(static_cast<UINT32>(layoutStart), static_cast<UINT32>(length),
                0.0f, line.top, nullptr, 0, &count);
            if (count == 0) {
                return;
            }
            std::vector<DWRITE_HIT_TEST_METRICS> metrics(count);
            if (SUCCEEDED(line.layout->HitTestTextRange(static_cast<UINT32>(layoutStart), static_cast<UINT32>(length),
                    0.0f, line.top, metrics.data(), count, &count))) {
                for (UINT32 m = 0; m < count; ++m) {
                    out.emplace_back(metrics[m].left, metrics[m].top, metrics[m].width, metrics[m].height);
                }
            }
        }
    };

    TextLayoutEngine::TextLayoutEngine() : impl_(std::make_unique<Impl>()) {}

    TextLayoutEngine::~TextLayoutEngine() = default;

    // Defined after TextLayoutEngine::Impl, which it reaches into (TextRenderer is a friend).
    void TextRenderer::render(BLContext& ctx, int width, int height, const TextLayoutEngine& layout,
            const Color& textColor, float scrollOffsetY, const std::vector<TextColorRun>& colorRuns, const Color& foldColor) {
        const auto& lines = layout.impl_->lines;
        if (lines.empty()) {
            return;
        }
        std::vector<LayoutPiece> pieces;
        for (std::size_t i = layout.impl_->lineForY(scrollOffsetY);
                i < lines.size() && lines[i].top < scrollOffsetY + static_cast<float>(height); ++i) {
            LayoutPiece piece{ lines[i].layout.GetInterfacePtr(), lines[i].top, lines[i].text.size(), lines[i].segments, {} };
            for (const TextLayoutEngine::Impl::Placeholder& placeholder : lines[i].placeholders) {
                piece.placeholders.push_back({ placeholder.foldStart, placeholder.layoutStart, placeholder.length });
            }
            pieces.push_back(std::move(piece));
        }
        drawLayouts(ctx, width, height, pieces, textColor, scrollOffsetY, colorRuns, foldColor);
    }

    bool TextLayoutEngine::update(const TextStorage& storage, const Font& font, float maxWidth, float maxHeight, bool wordWrap,
            const std::vector<TextFontRun>& fontRuns, const std::vector<TextFold>& folds) {
        IDWriteTextFormat* format = impl_->textFormat.resolve(font);
        if (format == nullptr) {
            return false;
        }

        const std::wstring& text = storage.text();

        // Only collapsed folds matter here, outermost first where they start together.
        std::vector<TextFold> collapsed;
        for (const TextFold& fold : folds) {
            if (fold.collapsed && fold.length > 0 && fold.start < text.size()) {
                collapsed.push_back(fold);
            }
        }
        std::sort(collapsed.begin(), collapsed.end(), [](const TextFold& a, const TextFold& b) {
            return a.start != b.start ? a.start < b.start : a.length > b.length;
        });

        const bool sameShape = format == impl_->lastFormat
            && lastMaxWidth_ == maxWidth
            && lastMaxHeight_ == maxHeight
            && lastWordWrap_ == wordWrap;
        if (sameShape && !impl_->lines.empty() && lastText_ == text && lastFontRuns_ == fontRuns && lastFolds_ == collapsed) {
            return true;
        }

        // Font runs by start, swept alongside the segments (which only move forward) - so each
        // segment looks at the few runs that can reach it, not all of them.
        std::vector<const TextFontRun*> sortedRuns;
        for (const TextFontRun& run : fontRuns) {
            if (run.length > 0) {
                sortedRuns.push_back(&run);
            }
        }
        std::sort(sortedRuns.begin(), sortedRuns.end(),
            [](const TextFontRun* a, const TextFontRun* b) { return a->start < b->start; });
        std::size_t nextRun = 0;
        std::vector<const TextFontRun*> activeRuns;

        std::vector<Impl::Line> fresh;
        std::size_t nextFold = 0;
        std::size_t pos = 0;
        std::size_t textLine = 0;
        bool done = false;
        while (!done) {
            Impl::Line line;
            line.start = pos;
            line.textLine = textLine;
            while (true) {
                const std::size_t newline = text.find(L'\n', pos);
                const std::size_t end = newline == std::wstring::npos ? text.size() : newline;
                while (nextFold < collapsed.size() && collapsed[nextFold].start < pos) {
                    ++nextFold;   // inside text an earlier fold already hides
                }
                if (nextFold < collapsed.size() && collapsed[nextFold].start <= end) {
                    const TextFold& fold = collapsed[nextFold++];
                    const std::size_t foldEnd = fold.start + fold.length < text.size() ? fold.start + fold.length : text.size();
                    line.segments.push_back({ pos, line.text.size(), fold.start - pos });
                    line.text.append(text, pos, fold.start - pos);
                    line.placeholders.push_back({ line.text.size(), fold.placeholder.size(), fold.start, foldEnd });
                    line.text += fold.placeholder;
                    textLine += static_cast<std::size_t>(std::count(text.begin() + fold.start, text.begin() + foldEnd, L'\n'));
                    pos = foldEnd;
                    continue;
                }
                std::size_t contentEnd = end;
                if (newline != std::wstring::npos && contentEnd > pos && text[contentEnd - 1] == L'\r') {
                    --contentEnd;
                }
                line.segments.push_back({ pos, line.text.size(), contentEnd - pos });
                line.text.append(text, pos, contentEnd - pos);
                line.length = contentEnd - line.start;
                line.terminator = newline == std::wstring::npos ? 0 : newline + 1 - contentEnd;
                done = newline == std::wstring::npos;
                pos = newline + 1;
                ++textLine;
                break;
            }

            for (const LayoutSegment& segment : line.segments) {
                const std::size_t segmentEnd = segment.textStart + segment.length;
                while (nextRun < sortedRuns.size() && sortedRuns[nextRun]->start < segmentEnd) {
                    activeRuns.push_back(sortedRuns[nextRun++]);
                }
                activeRuns.erase(std::remove_if(activeRuns.begin(), activeRuns.end(), [&](const TextFontRun* run) {
                    return run->start + run->length <= segment.textStart;
                }), activeRuns.end());
                for (const TextFontRun* run : activeRuns) {
                    const std::size_t runEnd = run->start + run->length;
                    const std::size_t from = run->start > segment.textStart ? run->start : segment.textStart;
                    const std::size_t to = runEnd < segmentEnd ? runEnd : segmentEnd;
                    if (to > from) {
                        TextFontRun local = *run;
                        local.start = segment.layoutStart + (from - segment.textStart);
                        local.length = to - from;
                        line.runs.push_back(local);
                    }
                }
            }
            fresh.push_back(std::move(line));
        }

        // Keep the layouts of unchanged lines (same laid-out text and styling), matched from the
        // front and the back - so one edit rebuilds only the lines it actually touched.
        std::vector<Impl::Line>& old = impl_->lines;
        auto unchanged = [](const Impl::Line& a, const Impl::Line& b) { return a.text == b.text && a.runs == b.runs; };
        auto reuse = [](Impl::Line& into, const Impl::Line& from) {
            into.layout = from.layout;
            into.height = from.height;
            into.baseline = from.baseline;
        };
        if (sameShape) {
            std::size_t prefix = 0;
            while (prefix < old.size() && prefix < fresh.size() && unchanged(old[prefix], fresh[prefix])) {
                reuse(fresh[prefix], old[prefix]);
                ++prefix;
            }
            std::size_t suffix = 0;
            while (suffix < old.size() - prefix && suffix < fresh.size() - prefix
                    && unchanged(old[old.size() - 1 - suffix], fresh[fresh.size() - 1 - suffix])) {
                reuse(fresh[fresh.size() - 1 - suffix], old[old.size() - 1 - suffix]);
                ++suffix;
            }
        }

        impl_->lastBuilt = 0;
        float emptyLineHeight = 0.0f;   // an empty layout can report 0 - use one space's line height
        float top = 0.0f;
        for (Impl::Line& line : fresh) {
            if (line.layout == nullptr) {
                IDWriteTextLayoutPtr layout;
                if (FAILED(DirectWriteResources::dwriteFactory().CreateTextLayout(
                        line.text.c_str(), static_cast<UINT32>(line.text.size()), format, maxWidth, maxHeight, &layout))) {
                    impl_->lines.clear();
                    return false;
                }
                layout->SetWordWrapping(wordWrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
                for (const TextFontRun& run : line.runs) {
                    const DWRITE_TEXT_RANGE range{ static_cast<UINT32>(run.start), static_cast<UINT32>(run.length) };
                    if (run.bold) {
                        layout->SetFontWeight(DWRITE_FONT_WEIGHT_BOLD, range);
                    }
                    if (run.italic) {
                        layout->SetFontStyle(DWRITE_FONT_STYLE_ITALIC, range);
                    }
                    if (run.underline) {
                        layout->SetUnderline(TRUE, range);
                    }
                    if (run.strikethrough) {
                        layout->SetStrikethrough(TRUE, range);
                    }
                    if (!run.fontName.empty()) {
                        layout->SetFontFamilyName(utf8ToWide(run.fontName).c_str(), range);
                    }
                    if (run.fontSize > 0.0f) {
                        layout->SetFontSize(run.fontSize, range);
                    }
                }
                DWRITE_TEXT_METRICS metrics{};
                layout->GetMetrics(&metrics);
                line.height = metrics.height;
                if (line.height <= 0.0f) {
                    if (emptyLineHeight <= 0.0f) {
                        IDWriteTextLayoutPtr space;
                        if (SUCCEEDED(DirectWriteResources::dwriteFactory().CreateTextLayout(
                                L" ", 1, format, maxWidth, maxHeight, &space))) {
                            DWRITE_TEXT_METRICS spaceMetrics{};
                            space->GetMetrics(&spaceMetrics);
                            emptyLineHeight = spaceMetrics.height;
                        }
                    }
                    line.height = emptyLineHeight;
                }
                DWRITE_LINE_METRICS firstRow{};
                UINT32 rowCount = 0;
                layout->GetLineMetrics(&firstRow, 1, &rowCount);
                line.baseline = rowCount > 0 && firstRow.baseline > 0.0f ? firstRow.baseline : line.height * 0.8f;
                line.layout = layout;
                ++impl_->lastBuilt;
            }
            line.top = top;
            top += line.height;
        }

        impl_->lines = std::move(fresh);
        impl_->lastFormat = format;
        lastText_ = text;
        lastMaxWidth_ = maxWidth;
        lastMaxHeight_ = maxHeight;
        lastWordWrap_ = wordWrap;
        lastFontRuns_ = fontRuns;
        lastFolds_ = std::move(collapsed);
        return true;
    }

    std::vector<Rect> TextLayoutEngine::hitTestRange(const TextRange& range) const {
        std::vector<Rect> result;
        const auto& lines = impl_->lines;
        if (lines.empty() || range.length() == 0) {
            return result;
        }
        for (std::size_t i = impl_->lineForOffset(range.start()); i < lines.size() && lines[i].start < range.end(); ++i) {
            const Impl::Line& line = lines[i];
            for (const LayoutSegment& segment : line.segments) {
                const std::size_t segmentEnd = segment.textStart + segment.length;
                const std::size_t from = range.start() > segment.textStart ? range.start() : segment.textStart;
                const std::size_t to = range.end() < segmentEnd ? range.end() : segmentEnd;
                if (to > from) {
                    Impl::appendRangeRects(line, segment.layoutStart + (from - segment.textStart), to - from, result);
                }
            }
            // A range reaching into hidden text covers its placeholder.
            for (const Impl::Placeholder& placeholder : line.placeholders) {
                if (range.start() < placeholder.foldEnd && range.end() > placeholder.foldStart && placeholder.length > 0) {
                    Impl::appendRangeRects(line, placeholder.layoutStart, placeholder.length, result);
                }
            }
            // The line break itself, when the range runs through it - a sliver at the line's end,
            // as a single whole-document layout would show it.
            if (line.terminator > 0 && range.start() <= line.end() && range.end() > line.end()) {
                FLOAT x = 0.0f;
                FLOAT y = 0.0f;
                DWRITE_HIT_TEST_METRICS metrics{};
                if (SUCCEEDED(line.layout->HitTestTextPosition(static_cast<UINT32>(line.text.size()), FALSE, &x, &y, &metrics))) {
                    result.emplace_back(x, line.top + y, metrics.height * 0.3f, metrics.height);
                }
            }
        }
        return result;
    }

    void TextLayoutEngine::hitTestPosition(const TextPosition& position, Point& outTopLeft, float& outHeight) const {
        outTopLeft = Point();
        outHeight = 0.0f;
        if (impl_->lines.empty() || !position.isValid()) {
            return;
        }
        const Impl::Line& line = impl_->lines[impl_->lineForOffset(position.offset())];
        FLOAT pointX = 0.0f;
        FLOAT pointY = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (FAILED(line.layout->HitTestTextPosition(static_cast<UINT32>(line.toLayout(position.offset())),
                FALSE, &pointX, &pointY, &metrics))) {
            return;
        }
        outTopLeft = Point(pointX, line.top + pointY);
        outHeight = metrics.height;
    }

    TextPosition TextLayoutEngine::hitTestPoint(const Point& localPoint) const {
        if (impl_->lines.empty()) {
            return TextPosition();
        }
        const Impl::Line& line = impl_->lines[impl_->lineForY(localPoint.y)];
        BOOL isTrailingHit = FALSE;
        BOOL isInside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (FAILED(line.layout->HitTestPoint(localPoint.x, localPoint.y - line.top, &isTrailingHit, &isInside, &metrics))) {
            return TextPosition();
        }
        std::size_t layoutOffset = metrics.textPosition;
        if (isTrailingHit) {
            layoutOffset += metrics.length;
        }
        return TextPosition(line.toText(layoutOffset < line.text.size() ? layoutOffset : line.text.size()));
    }

    TextRange TextLayoutEngine::lineRange(const TextPosition& position) const {
        if (impl_->lines.empty() || !position.isValid()) {
            return TextRange();
        }
        const Impl::Line& line = impl_->lines[impl_->lineForOffset(position.offset())];
        const std::size_t local = line.toLayout(position.offset());

        // The wrapped row within the line - what Home/End move across.
        UINT32 rowCount = 0;
        line.layout->GetLineMetrics(nullptr, 0, &rowCount);
        std::vector<DWRITE_LINE_METRICS> rows(rowCount);
        if (rowCount == 0 || FAILED(line.layout->GetLineMetrics(rows.data(), rowCount, &rowCount))) {
            return TextRange(line.start, line.length);
        }
        std::size_t rowStart = 0;
        for (UINT32 r = 0; r < rowCount; ++r) {
            const std::size_t rowEnd = rowStart + rows[r].length;
            if (local < rowEnd || r + 1 == rowCount) {
                const std::size_t from = line.toText(rowStart);
                const std::size_t to = line.toText(rowEnd - rows[r].newlineLength);
                return TextRange(from, to > from ? to - from : 0);
            }
            rowStart = rowEnd;
        }
        return TextRange(line.start, line.length);
    }

    float TextLayoutEngine::contentHeight() const {
        return impl_->lines.empty() ? 0.0f : impl_->lines.back().top + impl_->lines.back().height;
    }

    std::vector<Rect> TextLayoutEngine::placeholderRects(float top, float bottom) const {
        std::vector<Rect> result;
        const auto& lines = impl_->lines;
        if (lines.empty()) {
            return result;
        }
        for (std::size_t i = impl_->lineForY(top); i < lines.size() && lines[i].top < bottom; ++i) {
            for (const Impl::Placeholder& placeholder : lines[i].placeholders) {
                if (placeholder.length > 0) {
                    Impl::appendRangeRects(lines[i], placeholder.layoutStart, placeholder.length, result);
                }
            }
        }
        return result;
    }

    bool TextLayoutEngine::foldAtPoint(const Point& localPoint, std::size_t& outFoldStart) const {
        if (impl_->lines.empty()) {
            return false;
        }
        const Impl::Line& line = impl_->lines[impl_->lineForY(localPoint.y)];
        if (line.placeholders.empty() || localPoint.y < line.top || localPoint.y >= line.top + line.height) {
            return false;
        }
        BOOL isTrailingHit = FALSE;
        BOOL isInside = FALSE;
        DWRITE_HIT_TEST_METRICS metrics{};
        if (FAILED(line.layout->HitTestPoint(localPoint.x, localPoint.y - line.top, &isTrailingHit, &isInside, &metrics))
                || !isInside) {
            return false;
        }
        for (const Impl::Placeholder& placeholder : line.placeholders) {
            if (metrics.textPosition >= placeholder.layoutStart && metrics.textPosition < placeholder.layoutStart + placeholder.length) {
                outFoldStart = placeholder.foldStart;
                return true;
            }
        }
        return false;
    }

    std::size_t TextLayoutEngine::lineCount() const { return impl_->lines.size(); }
    std::size_t TextLayoutEngine::lineStart(std::size_t line) const { return line < impl_->lines.size() ? impl_->lines[line].start : 0; }
    std::size_t TextLayoutEngine::lineLength(std::size_t line) const { return line < impl_->lines.size() ? impl_->lines[line].length : 0; }
    float TextLayoutEngine::lineTop(std::size_t line) const { return line < impl_->lines.size() ? impl_->lines[line].top : 0.0f; }
    float TextLayoutEngine::lineHeight(std::size_t line) const { return line < impl_->lines.size() ? impl_->lines[line].height : 0.0f; }
    std::size_t TextLayoutEngine::lineAt(std::size_t offset) const { return impl_->lines.empty() ? 0 : impl_->lineForOffset(offset); }
    std::size_t TextLayoutEngine::lineAtY(float y) const { return impl_->lines.empty() ? 0 : impl_->lineForY(y); }
    std::size_t TextLayoutEngine::lineNumber(std::size_t line) const { return line < impl_->lines.size() ? impl_->lines[line].textLine : 0; }
    float TextLayoutEngine::lineBaseline(std::size_t line) const { return line < impl_->lines.size() ? impl_->lines[line].baseline : 0.0f; }
    std::size_t TextLayoutEngine::layoutsBuiltLastUpdate() const { return impl_->lastBuilt; }

}

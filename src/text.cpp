#include "newui/text.h"

#include "newui/uicolormanager.h"
#include "newui/utils.h"

#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>

#include <comdef.h>
#include <comip.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <tuple>

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
        const bool moved = position_ != position;
        position_ = position;
        visible_ = true;
        if (moved) {
            onPositionChanged(*this);
        }
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

    void Caret::draw(BLContext& ctx, const Point& topLeft, float height, float width) const {
        if (!isVisible()) {
            return;
        }
        ctx.save();
        ctx.set_fill_style(color_.toBLRgba32());
        if (width > 0.0f) {
            ctx.set_fill_alpha(0.5);
        } else {
            width = static_cast<float>(systemCaretWidth());
        }
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

    void TextStorage::appendTo(const TextRange& range, std::wstring& out) const {
        const size_t start = range.start() < text_.size() ? range.start() : text_.size();
        const size_t end = range.end() < text_.size() ? range.end() : text_.size();
        if (start < end) {
            out.append(text_, start, end - start);
        }
    }

    size_t TextStorage::find(wchar_t ch, size_t from) const {
        const size_t found = text_.find(ch, from);
        return found == std::wstring::npos ? npos : found;
    }

    size_t TextStorage::count(wchar_t ch, const TextRange& range) const {
        const size_t start = range.start() < text_.size() ? range.start() : text_.size();
        const size_t end = range.end() < text_.size() ? range.end() : text_.size();
        if (start >= end) {
            return 0;
        }
        return static_cast<size_t>(std::count(text_.begin() + static_cast<std::ptrdiff_t>(start),
            text_.begin() + static_cast<std::ptrdiff_t>(end), ch));
    }

    void TextStorage::forEachChunk(const TextRange& range, const std::function<void(const wchar_t*, size_t)>& visit) const {
        const size_t start = range.start() < text_.size() ? range.start() : text_.size();
        const size_t end = range.end() < text_.size() ? range.end() : text_.size();
        if (start < end) {
            visit(text_.data() + start, end - start);
        }
    }

    bool TextStorage::equals(const std::wstring& other) const {
        return text_ == other;
    }

    size_t TextStorage::commonPrefixLength(const std::wstring& other) const {
        const size_t shorter = text_.size() < other.size() ? text_.size() : other.size();
        size_t n = 0;
        while (n < shorter && text_[n] == other[n]) {
            ++n;
        }
        return n;
    }

    size_t TextStorage::commonSuffixLength(const std::wstring& other, size_t limit) const {
        const size_t shorter = text_.size() < other.size() ? text_.size() : other.size();
        const size_t max = limit < shorter ? limit : shorter;
        size_t n = 0;
        while (n < max && text_[text_.size() - 1 - n] == other[other.size() - 1 - n]) {
            ++n;
        }
        return n;
    }

    void TextStorage::buildLineStarts() const {
        lineStarts_.clear();
        lineStarts_.push_back(0);
        for (size_t i = 0; i < text_.size(); ++i) {
            if (text_[i] == L'\r' && i + 1 < text_.size() && text_[i + 1] == L'\n') {
                ++i;   // "\r\n" is one break
            }
            if (text_[i] == L'\n' || text_[i] == L'\r') {
                lineStarts_.push_back(i + 1);
            }
        }
        lineStartsValid_ = true;
    }

    size_t TextStorage::findLineBreak(size_t from, size_t* terminatorLength) const {
        for (size_t i = from; i < text_.size(); ++i) {
            if (text_[i] == L'\n') {
                if (terminatorLength != nullptr) {
                    *terminatorLength = 1;
                }
                return i;
            }
            if (text_[i] == L'\r') {
                if (terminatorLength != nullptr) {
                    *terminatorLength = i + 1 < text_.size() && text_[i + 1] == L'\n' ? 2 : 1;
                }
                return i;
            }
        }
        return npos;
    }

    size_t TextStorage::countLineBreaks(const TextRange& range) const {
        const size_t start = range.start() < text_.size() ? range.start() : text_.size();
        const size_t end = range.end() < text_.size() ? range.end() : text_.size();
        size_t breaks = 0;
        for (size_t i = start; i < end; ++i) {
            if (text_[i] == L'\r' || (text_[i] == L'\n' && (i == 0 || text_[i - 1] != L'\r'))) {
                ++breaks;
            }
        }
        return breaks;
    }

    size_t TextStorage::lineCount() const {
        if (!lineStartsValid_) {
            buildLineStarts();
        }
        return lineStarts_.size();
    }

    size_t TextStorage::lineStart(size_t line) const {
        if (!lineStartsValid_) {
            buildLineStarts();
        }
        return line < lineStarts_.size() ? lineStarts_[line] : text_.size();
    }

    size_t TextStorage::lineOfOffset(size_t offset) const {
        if (!lineStartsValid_) {
            buildLineStarts();
        }
        const size_t clamped = offset < text_.size() ? offset : text_.size();
        // The last line starting at or before clamped.
        return static_cast<size_t>(std::upper_bound(lineStarts_.begin(), lineStarts_.end(), clamped) - lineStarts_.begin()) - 1;
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
        lineStartsValid_ = false;
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
        ID2D1RenderTargetPtr opaqueRenderTarget;   // see ensureOpaqueRenderTarget()
        IWICBitmapPtr opaqueBitmap;
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
        if (width <= 0 || height <= 0 || pieces.empty()) {
            return;
        }

        // Opaque when the pixels already under the text can be read back from ctx's target (a
        // whole-pixel translation, the area inside the target): the text is then drawn onto a
        // copy of them with ClearType, as Windows draws text. Otherwise onto a transparent bitmap,
        // where Direct2D can only antialias in grayscale - which, blended over the background,
        // looks heavier and softer.
        BLImage* target = ctx.target_image();
        const BLMatrix2D& transform = ctx.final_transform();
        const double originX = transform.m20;
        const double originY = transform.m21;
        bool opaque = target != nullptr && !target->is_empty()
            && transform.type() <= BL_TRANSFORM_TYPE_TRANSLATE
            && originX == std::floor(originX) && originY == std::floor(originY)
            && originX >= 0.0 && originY >= 0.0
            && originX + width <= target->width() && originY + height <= target->height()
            && (target->format() == BL_FORMAT_PRGB32 || target->format() == BL_FORMAT_XRGB32);
        // ...and only over a backdrop that's fully painted - over anything see-through, the opaque
        // bitmap would put black where there was nothing.
        BLImageData source{};
        const std::uint8_t* from = nullptr;
        if (opaque) {
            ctx.flush(BL_CONTEXT_FLUSH_SYNC);
            opaque = target->get_data(&source) == BL_SUCCESS;
            from = opaque ? static_cast<const std::uint8_t*>(source.pixel_data)
                + static_cast<std::intptr_t>(originY) * source.stride + static_cast<std::intptr_t>(originX) * 4 : nullptr;
            for (int y = 0; opaque && source.format == BL_FORMAT_PRGB32 && y < height; ++y) {
                const auto* row = reinterpret_cast<const std::uint32_t*>(from + static_cast<std::intptr_t>(y) * source.stride);
                for (int x = 0; x < width; ++x) {
                    if ((row[x] >> 24) != 0xFF) {
                        opaque = false;
                        break;
                    }
                }
            }
        }
        if (!(opaque ? ensureOpaqueRenderTarget(width, height) : ensureRenderTarget(width, height))) {
            return;
        }
        ID2D1RenderTarget* renderTarget = opaque ? impl_->opaqueRenderTarget.GetInterfacePtr() : impl_->renderTarget.GetInterfacePtr();
        IWICBitmap* bitmap = opaque ? impl_->opaqueBitmap.GetInterfacePtr() : impl_->wicBitmap.GetInterfacePtr();

        if (opaque) {
            // What's been painted so far (background, selection, highlights) becomes the backdrop.
            WICRect lockRect{ 0, 0, width, height };
            IWICBitmapLockPtr lock;
            UINT stride = 0;
            UINT size = 0;
            WICInProcPointer pixels = nullptr;
            if (FAILED(bitmap->Lock(&lockRect, WICBitmapLockWrite, &lock))
                    || FAILED(lock->GetStride(&stride)) || FAILED(lock->GetDataPointer(&size, &pixels)) || pixels == nullptr) {
                return;
            }
            for (int y = 0; y < height; ++y) {
                std::memcpy(pixels + static_cast<std::size_t>(y) * stride, from + static_cast<std::intptr_t>(y) * source.stride,
                    static_cast<std::size_t>(width) * 4);
            }
        }

        renderTarget->BeginDraw();
        if (!opaque) {
            // Fully transparent clear - only the glyphs DrawTextLayout()
            // below should end up opaque; blit_image() below composites the
            // rest of this bitmap's (untouched, still-zero) alpha as
            // "nothing here" over whatever the caller already painted.
            renderTarget->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
        }

        ID2D1SolidColorBrushPtr brush;
        HRESULT brushHr = renderTarget->CreateSolidColorBrush(
            D2D1::ColorF(textColor.r, textColor.g, textColor.b, textColor.a), &brush);

        // Each run's brush is its drawing effect - DrawTextLayout() draws a range whose effect is an
        // ID2D1Brush with that brush, so no custom IDWriteTextRenderer is needed. One brush per
        // distinct color (a document has thousands of runs but a handful of colors). A layout that
        // outlives this call (TextLayoutEngine's) may still carry an earlier paint's effects - each
        // piece is cleared first, and again once drawn.
        std::map<std::uint32_t, ID2D1SolidColorBrushPtr> runBrushes;
        ID2D1SolidColorBrushPtr foldBrush;
        renderTarget->CreateSolidColorBrush(D2D1::ColorF(foldColor.r, foldColor.g, foldColor.b, foldColor.a), &foldBrush);
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
                    if (!runBrush && FAILED(renderTarget->CreateSolidColorBrush(
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
                renderTarget->DrawTextLayout(
                    D2D1::Point2F(0.0f, piece.top - scrollOffsetY), piece.layout, brush.GetInterfacePtr());
            }
        }

        // EndDraw() has to be called to close out BeginDraw() regardless
        // of whether the brush above succeeded - abandoning a draw
        // session without it leaves the render target in an inconsistent
        // state for the next render() call.
        HRESULT endHr = renderTarget->EndDraw();
        // The brushes belong to this render target - don't leave them attached to layouts that
        // outlive this call.
        for (const LayoutPiece& piece : pieces) {
            piece.layout->SetDrawingEffect(nullptr, DWRITE_TEXT_RANGE{ 0, static_cast<UINT32>(piece.layoutLength) });
        }
        if (endHr == D2DERR_RECREATE_TARGET) {
            // Device loss (rare for a software render target, but a real
            // integration still has to handle it) - drop the render
            // targets/bitmaps so the next render() rebuilds them fresh
            // instead of drawing through a now-invalid target.
            impl_->renderTarget = nullptr;
            impl_->wicBitmap = nullptr;
            impl_->opaqueRenderTarget = nullptr;
            impl_->opaqueBitmap = nullptr;
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
        HRESULT lockHr = bitmap->Lock(&lockRect, WICBitmapLockRead, &lock);
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
        // locked) until it goes out of scope below. The transparent
        // bitmap is 32bpp top-down premultiplied BGRA - exactly
        // BL_FORMAT_PRGB32; the opaque one's alpha byte is undefined, so
        // it's XRGB32 (every pixel opaque, replacing what was there).
        BLImage textImage;
        textImage.create_from_data(width, height, opaque ? BL_FORMAT_XRGB32 : BL_FORMAT_PRGB32,
            lockedData, static_cast<intptr_t>(stride));

        ctx.save();
        ctx.blit_image(BLPoint(0, 0), textImage);
        ctx.restore();
        // lock unlocks the bitmap automatically at scope exit.
    }

    bool TextRenderer::ensureOpaqueRenderTarget(int width, int height) {
        if (impl_->opaqueRenderTarget != nullptr && width == opaqueWidth_ && height == opaqueHeight_) {
            return true;
        }
        // 32bpp BGR with the alpha ignored: an opaque target, the only kind Direct2D draws
        // ClearType text on.
        IWICBitmapPtr bitmap;
        if (FAILED(DirectWriteResources::wicFactory().CreateBitmap(static_cast<UINT>(width), static_cast<UINT>(height),
                GUID_WICPixelFormat32bppBGR, WICBitmapCacheOnDemand, &bitmap))) {
            return false;
        }
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));
        ID2D1RenderTargetPtr renderTarget;
        if (FAILED(DirectWriteResources::d2dFactory().CreateWicBitmapRenderTarget(bitmap.GetInterfacePtr(), props, &renderTarget))) {
            return false;
        }
        // ClearType only if the user has it on (Control Panel's ClearType Tuner), grayscale if not.
        UINT smoothing = 0;
        ::SystemParametersInfo(SPI_GETFONTSMOOTHINGTYPE, 0, &smoothing, 0);
        renderTarget->SetTextAntialiasMode(smoothing == FE_FONTSMOOTHINGCLEARTYPE
            ? D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE : D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
        impl_->opaqueBitmap = bitmap;
        impl_->opaqueRenderTarget = renderTarget;
        opaqueWidth_ = width;
        opaqueHeight_ = height;
        return true;
    }

    // One DirectWrite layout per visual line. A visual line is a line of the text (split at "\n", "\r\n" or a lone '\r';
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
            bool terminatorIsCr = false; // a lone '\r' (else '\n' - or "\r\n", terminator 2)
            std::size_t textLine = 0;    // the line of the text it starts on
            std::size_t textLines = 1;   // how many lines of the text it shows (a collapsed fold joins several)
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
        float digitWidth = 0.0f;   // lastFormat's - tab stops are counted in it

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

    namespace {
        // runs clipped to [from, to), relative to from, in a fixed order - for comparing the styling
        // of two stretches of text.
        std::vector<TextFontRun> runsWithin(const std::vector<TextFontRun>& runs, std::size_t from, std::size_t to) {
            std::vector<TextFontRun> out;
            for (const TextFontRun& run : runs) {
                const std::size_t start = run.start > from ? run.start : from;
                const std::size_t end = run.start + run.length < to ? run.start + run.length : to;
                if (end > start) {
                    TextFontRun clipped = run;
                    clipped.start = start - from;
                    clipped.length = end - start;
                    out.push_back(clipped);
                }
            }
            std::sort(out.begin(), out.end(), [](const TextFontRun& a, const TextFontRun& b) {
                return std::tie(a.start, a.length, a.bold, a.italic, a.underline, a.strikethrough, a.fontSize, a.fontName)
                    < std::tie(b.start, b.length, b.bold, b.italic, b.underline, b.strikethrough, b.fontSize, b.fontName);
            });
            return out;
        }

        // Collapsed folds starting in [from, to), relative to from.
        std::vector<TextFold> foldsWithin(const std::vector<TextFold>& folds, std::size_t from, std::size_t to) {
            std::vector<TextFold> out;
            for (const TextFold& fold : folds) {
                if (fold.start >= from && fold.start < to) {
                    TextFold relative = fold;
                    relative.start -= from;
                    out.push_back(relative);
                }
            }
            return out;
        }
    }

    bool TextLayoutEngine::update(const TextStorage& storage, const Font& font, float maxWidth, float maxHeight, bool wordWrap,
            const std::vector<TextFontRun>& fontRuns, const std::vector<TextFold>& folds, std::size_t tabWidth) {
        IDWriteTextFormat* format = impl_->textFormat.resolve(font);
        if (format == nullptr) {
            return false;
        }

        const std::size_t textSize = storage.length();

        // Only collapsed folds matter here, outermost first where they start together.
        std::vector<TextFold> collapsed;
        for (const TextFold& fold : folds) {
            if (fold.collapsed && fold.length > 0 && fold.start < textSize) {
                collapsed.push_back(fold);
            }
        }
        std::sort(collapsed.begin(), collapsed.end(), [](const TextFold& a, const TextFold& b) {
            return a.start != b.start ? a.start < b.start : a.length > b.length;
        });

        const bool sameShape = format == impl_->lastFormat
            && lastMaxWidth_ == maxWidth
            && lastMaxHeight_ == maxHeight
            && lastWordWrap_ == wordWrap
            && lastTabWidth_ == tabWidth;
        if (sameShape && !impl_->lines.empty() && storage.equals(lastText_) && lastFontRuns_ == fontRuns && lastFolds_ == collapsed) {
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

        // Lays out lines (no DirectWrite yet - see below) from text offset from, a line's start,
        // until one would start at stopAt or past it, or the text ends. Returns where it stopped.
        auto build = [&](std::size_t from, std::size_t stopAt, std::vector<Impl::Line>& out) {
            std::size_t nextFold = static_cast<std::size_t>(std::lower_bound(collapsed.begin(), collapsed.end(), from,
                [](const TextFold& fold, std::size_t offset) { return fold.start < offset; }) - collapsed.begin());
            std::size_t nextRun = static_cast<std::size_t>(std::lower_bound(sortedRuns.begin(), sortedRuns.end(), from,
                [](const TextFontRun* run, std::size_t offset) { return run->start < offset; }) - sortedRuns.begin());
            std::vector<const TextFontRun*> activeRuns;
            for (std::size_t i = 0; i < nextRun; ++i) {
                if (sortedRuns[i]->start + sortedRuns[i]->length > from) {
                    activeRuns.push_back(sortedRuns[i]);   // began earlier, still running
                }
            }
            std::size_t pos = from;
            bool done = false;
            while (!done && pos < stopAt) {
                Impl::Line line;
                line.start = pos;
                while (true) {
                    std::size_t breakLength = 0;
                    const std::size_t lineBreak = storage.findLineBreak(pos, &breakLength);
                    const std::size_t end = lineBreak == TextStorage::npos ? textSize : lineBreak;
                    while (nextFold < collapsed.size() && collapsed[nextFold].start < pos) {
                        ++nextFold;   // inside text an earlier fold already hides
                    }
                    if (nextFold < collapsed.size() && collapsed[nextFold].start <= end) {
                        const TextFold& fold = collapsed[nextFold++];
                        const std::size_t foldEnd = fold.start + fold.length < textSize ? fold.start + fold.length : textSize;
                        line.segments.push_back({ pos, line.text.size(), fold.start - pos });
                        storage.appendTo(TextRange(pos, fold.start - pos), line.text);
                        line.placeholders.push_back({ line.text.size(), fold.placeholder.size(), fold.start, foldEnd });
                        line.text += fold.placeholder;
                        line.textLines += storage.countLineBreaks(TextRange(fold.start, foldEnd - fold.start));
                        pos = foldEnd;
                        continue;
                    }
                    line.segments.push_back({ pos, line.text.size(), end - pos });
                    storage.appendTo(TextRange(pos, end - pos), line.text);
                    line.length = end - line.start;
                    line.terminator = lineBreak == TextStorage::npos ? 0 : breakLength;
                    line.terminatorIsCr = lineBreak != TextStorage::npos && storage.at(lineBreak) == L'\r';
                    done = lineBreak == TextStorage::npos;
                    pos = done ? std::wstring::npos : lineBreak + breakLength;
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
                        const std::size_t runFrom = run->start > segment.textStart ? run->start : segment.textStart;
                        const std::size_t runTo = runEnd < segmentEnd ? runEnd : segmentEnd;
                        if (runTo > runFrom) {
                            TextFontRun local = *run;
                            local.start = segment.layoutStart + (runFrom - segment.textStart);
                            local.length = runTo - runFrom;
                            line.runs.push_back(local);
                        }
                    }
                }
                out.push_back(std::move(line));
            }
            return pos;
        };

        std::vector<Impl::Line>& lines = impl_->lines;
        bool edited = false;
        if (sameShape && !lines.empty() && !storage.equals(lastText_)) {
            // An edit: rebuild only the lines it touched. The text before it (prefix) and after it
            // (suffix) is unchanged, so are their lines - moved along by the edit's length -
            // provided their styling and folds are too. Otherwise, the full path below.
            const std::wstring& old = lastText_;
            const std::size_t shorter = old.size() < textSize ? old.size() : textSize;
            const std::size_t prefix = storage.commonPrefixLength(old);
            const std::size_t suffix = storage.commonSuffixLength(old, shorter - prefix);
            // An edit right after a '\r' can turn it into half of a "\r\n" - the line it ends is
            // touched too.
            const std::size_t first = impl_->lineForOffset(prefix > 0 && storage.at(prefix - 1) == L'\r' ? prefix - 1 : prefix);
            // The first line after the edit whose preceding break is unchanged too.
            const std::size_t oldSuffixStart = old.size() - suffix;
            std::size_t keep = static_cast<std::size_t>(std::upper_bound(lines.begin(), lines.end(), oldSuffixStart,
                [](std::size_t offset, const Impl::Line& line) { return offset < line.start; }) - lines.begin());
            keep = keep > first + 1 ? keep : first + 1;
            const std::size_t from = lines[first].start;
            const std::size_t oldStop = keep < lines.size() ? lines[keep].start : old.size();
            const std::size_t newStop = keep < lines.size() ? oldStop - old.size() + textSize : std::wstring::npos;
            const std::size_t newSuffixStart = keep < lines.size() ? newStop : textSize;

            const bool sameAround = runsWithin(lastFontRuns_, 0, from) == runsWithin(fontRuns, 0, from)
                && runsWithin(lastFontRuns_, oldStop, old.size()) == runsWithin(fontRuns, newSuffixStart, textSize)
                && foldsWithin(lastFolds_, 0, from) == foldsWithin(collapsed, 0, from)
                && foldsWithin(lastFolds_, oldStop, old.size()) == foldsWithin(collapsed, newSuffixStart, textSize);
            if (sameAround) {
                std::vector<Impl::Line> rebuilt;
                const std::size_t reached = build(from, newStop, rebuilt);
                if (keep >= lines.size() || reached == newStop) {
                    for (std::size_t i = keep; i < lines.size(); ++i) {
                        Impl::Line& line = lines[i];
                        line.start = line.start - old.size() + textSize;
                        for (LayoutSegment& segment : line.segments) {
                            segment.textStart = segment.textStart - old.size() + textSize;
                        }
                        for (Impl::Placeholder& placeholder : line.placeholders) {
                            placeholder.foldStart = placeholder.foldStart - old.size() + textSize;
                            placeholder.foldEnd = placeholder.foldEnd - old.size() + textSize;
                        }
                    }
                    lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(first), lines.begin() + static_cast<std::ptrdiff_t>(keep));
                    lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(first),
                        std::make_move_iterator(rebuilt.begin()), std::make_move_iterator(rebuilt.end()));
                    edited = true;
                }
            }
        }

        if (!edited) {
            std::vector<Impl::Line> fresh;
            build(0, std::wstring::npos, fresh);

            // Keep the layouts of unchanged lines (same laid-out text and styling), matched from
            // the front and the back.
            std::vector<Impl::Line>& old = lines;
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
            lines = std::move(fresh);
        }

        if (format != impl_->lastFormat || impl_->digitWidth <= 0.0f) {
            impl_->digitWidth = 0.0f;
            IDWriteTextLayoutPtr digits;
            if (SUCCEEDED(DirectWriteResources::dwriteFactory().CreateTextLayout(L"0000", 4, format, 10000.0f, 1000.0f, &digits))) {
                DWRITE_TEXT_METRICS metrics{};
                if (SUCCEEDED(digits->GetMetrics(&metrics))) {
                    impl_->digitWidth = metrics.widthIncludingTrailingWhitespace / 4.0f;
                }
            }
        }
        const float tabStop = tabWidth > 0 ? static_cast<float>(tabWidth) * impl_->digitWidth : 0.0f;

        // DirectWrite layouts for the lines that need one, then everyone's position.
        impl_->lastBuilt = 0;
        float emptyLineHeight = 0.0f;   // an empty layout can report 0 - use one space's line height
        float top = 0.0f;
        std::size_t textLine = 0;
        for (Impl::Line& line : lines) {
            if (line.layout == nullptr) {
                IDWriteTextLayoutPtr layout;
                if (FAILED(DirectWriteResources::dwriteFactory().CreateTextLayout(
                        line.text.c_str(), static_cast<UINT32>(line.text.size()), format, maxWidth, maxHeight, &layout))) {
                    lines.clear();
                    lastText_.clear();
                    return false;
                }
                layout->SetWordWrapping(wordWrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
                if (tabStop > 0.0f) {
                    layout->SetIncrementalTabStop(tabStop);
                }
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
            line.textLine = textLine;
            textLine += line.textLines;
        }

        impl_->lastFormat = format;
        lastText_.clear();
        storage.appendTo(TextRange(0, textSize), lastText_);
        lastMaxWidth_ = maxWidth;
        lastMaxHeight_ = maxHeight;
        lastWordWrap_ = wordWrap;
        lastFontRuns_ = fontRuns;
        lastFolds_ = std::move(collapsed);
        lastTabWidth_ = tabWidth;
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

    std::vector<WhitespaceMark> TextLayoutEngine::whitespaceMarks(float top, float bottom) const {
        std::vector<WhitespaceMark> marks;
        const auto& lines = impl_->lines;
        if (lines.empty()) {
            return marks;
        }
        auto mark = [&marks](const Impl::Line& line, std::size_t layoutOffset, WhitespaceKind kind, bool zeroWidth) {
            FLOAT x = 0.0f;
            FLOAT y = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics{};
            if (SUCCEEDED(line.layout->HitTestTextPosition(static_cast<UINT32>(layoutOffset), FALSE, &x, &y, &metrics))) {
                marks.push_back({ kind, Rect(x, line.top + y, zeroWidth ? 0.0f : metrics.width, metrics.height) });
            }
        };
        for (std::size_t i = impl_->lineForY(top); i < lines.size() && lines[i].top < bottom; ++i) {
            const Impl::Line& line = lines[i];
            for (const LayoutSegment& segment : line.segments) {   // placeholders aren't text
                for (std::size_t k = 0; k < segment.length; ++k) {
                    const std::size_t at = segment.layoutStart + k;
                    const wchar_t ch = line.text[at];
                    if (ch == L' ') {
                        mark(line, at, WhitespaceKind::Space, false);
                    } else if (ch == L'\t') {
                        mark(line, at, WhitespaceKind::Tab, false);
                    }
                }
            }
            if (line.terminator > 0) {
                mark(line, line.text.size(),
                    line.terminator == 2 ? WhitespaceKind::CarriageReturnLineFeed
                        : line.terminatorIsCr ? WhitespaceKind::CarriageReturn : WhitespaceKind::LineFeed, true);
            }
        }
        return marks;
    }

}

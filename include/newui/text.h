#pragma once

#include "newui/color.h"
#include "newui/delegate.h"
#include "newui/font.h"
#include "newui/geometry.h"
#include "newui/models.h"
#include "newui/runloop.h"

#include <blend2d/blend2d.h>

#include <any>
#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

// Forward declarations only - the full <d2d1.h>/<dwrite.h>/<comdef.h>/
// <comip.h> definitions (and every actual D2D/DWrite/COM call) live
// entirely inside text.cpp, so this header - pulled in by controls.h,
// and therefore practically everywhere - doesn't drag those in. This
// isn't just a header-weight nicety: <comdef.h> (needed for
// _COM_SMARTPTR_TYPEDEF/_com_ptr_t, see DirectWriteResources/
// TextRenderer's Impl in text.cpp) declares a *global* `Font` type (the
// classic OLE "StdFont" automation object) that collides with
// newui::Font the moment both are visible in the same translation unit -
// confirmed live: reflectgen's generated code (which does `using
// namespace newui;` and references `Font` unqualified) failed to compile
// with an ambiguous-symbol error the one time this header pulled
// <comdef.h> in directly. DirectWriteResources/TextRenderer both use the
// Pimpl idiom below specifically to keep that boundary intact - their
// own private _com_ptr_t members live in a text.cpp-only Impl struct,
// never named here. A COM interface like ID2D1Factory is declared as
// `interface ID2D1Factory : public IUnknown` in the real headers, and
// `interface` is just `#define`d to `struct` - so a bare `struct`
// forward declaration here is exactly what the real definition also is,
// safe to use as a reference/pointer return type below.
struct ID2D1Factory;
struct IDWriteFactory;
struct IWICImagingFactory;

namespace newui::text {

    // A single position within a TextStorage's content, expressed as a
    // plain character offset - TextStorage's own backing is a std::wstring
    // today, addressed by UTF-16 code-unit offset (see its own class
    // comment on how a future, more sophisticated backing stays valid
    // without this type changing). Deliberately not an opaque handle the
    // way UIKit's UITextPosition is - there's exactly one TextStorage
    // implementation right now, so there's nothing an opaque type would
    // usefully hide yet; this can grow into one later if a second, index-
    // incompatible backing ever needs it.
    class TextPosition {
    public:
        static const size_t Invalid = (size_t)-1;

        TextPosition() = default;
        explicit TextPosition(size_t offset) : offset_(offset) {}

        size_t offset() const { return offset_; }
        void setOffset(size_t offset) { offset_ = offset; }

        bool isValid() const { return offset_ != Invalid; }

        bool operator==(const TextPosition& other) const { return offset_ == other.offset_; }
        bool operator!=(const TextPosition& other) const { return !(*this == other); }
        bool operator<(const TextPosition& other) const { return offset_ < other.offset_; }
        bool operator<=(const TextPosition& other) const { return offset_ <= other.offset_; }
        bool operator>(const TextPosition& other) const { return other < *this; }
        bool operator>=(const TextPosition& other) const { return other <= *this; }

    private:
        size_t offset_ = Invalid;
    };

    // A contiguous run of text within a TextStorage - [start, start +
    // length). Deliberately start/length rather than UITextRange's own
    // start/end TextPosition pair: a zero-length range at a given offset
    // (an empty selection - i.e. just a caret) is a completely ordinary
    // value here (length() == 0), not a special case a caller has to
    // detect via start == end.
    class TextRange {
    public:
        static const size_t Invalid = (size_t)-1;

        TextRange() = default;
        TextRange(size_t start, size_t length) : start_(start), length_(length) {}

        size_t start() const { return start_; }
        void setStart(size_t start) { start_ = start; }

        size_t length() const { return length_; }
        void setLength(size_t length) { length_ = length; }

        // One past the last character this range covers - start() + length().
        size_t end() const { return start_ + length_; }

        bool isValid() const { return start_ != Invalid; }
        bool isEmpty() const { return length_ == 0; }

        bool contains(size_t offset) const { return offset >= start_ && offset < end(); }

        bool operator==(const TextRange& other) const {
            return start_ == other.start_ && length_ == other.length_;
        }
        bool operator!=(const TextRange& other) const { return !(*this == other); }

    private:
        size_t start_ = Invalid;
        size_t length_ = 0;
    };

    // Configuration knobs for how a TextField/TextControl accepts input -
    // loosely modeled on UIKit's UITextInputTraits, trimmed to what's
    // actually meaningful on a desktop keyboard: there's no software
    // keyboard here, so UITextInputTraits' autocapitalizationType/
    // keyboardType/returnKeyType and similar have no counterpart and are
    // deliberately left out.
    class TextInputTraits {
    public:
        bool isSecureTextEntry() const { return secureTextEntry_; }
        void setSecureTextEntry(bool value) { secureTextEntry_ = value; }

        bool isReadOnly() const { return readOnly_; }
        void setReadOnly(bool value) { readOnly_ = value; }

        // 0 (the default) means unlimited.
        size_t maxLength() const { return maxLength_; }
        void setMaxLength(size_t value) { maxLength_ = value; }

    private:
        bool secureTextEntry_ = false;
        bool readOnly_ = false;
        size_t maxLength_ = 0;
    };

    // A blinking text-insertion caret - a thin bar drawn at whatever
    // screen-space rect the owning control's layout engine (not yet built -
    // see TextLayoutEngine, Phase 3) computes for its current position().
    // Caret only owns *when* to be visible (blinking) and *what* position
    // it tracks, not where that position maps to on screen - draw() takes
    // that rect as a parameter rather than computing it, since this class
    // has no line-wrapping/hit-testing knowledge of its own to do so.
    //
    // Non-copyable: an active Caret owns a live RunLoop::postDelayed()
    // timer whose callback captures its own `this` - a copy would either
    // share that timer (double-cancel on the second destructor) or
    // silently not blink at all, neither of which is a sensible meaning
    // for "copy a caret".
    class Caret {
    public:
        // Fired whenever the blink timer flips isVisible() - NOT fired
        // from setPosition()/start()/stop() themselves, since those are
        // synchronous calls the caller already knows the outcome of at
        // the call site. Toggling a private bool has no effect on screen
        // by itself (this toolkit only repaints on an explicit
        // style().markDirty()/invalidate(), never just because time
        // passed) - the owning control's own job is to subscribe to this
        // and repaint (e.g. style().markDirty()) whenever it fires.
        // Confirmed live as a real, easy-to-miss gap: without this, a
        // caret blinks internally but visibly "waits for the mouse to
        // move" before the new state ever reaches the screen, since
        // mouse movement is what was incidentally triggering the only
        // repaints happening at all.
        typedef Delegate<Caret> VisibilityChangedDelegate;
        VisibilityChangedDelegate onVisibilityChanged;

        Caret() = default;
        ~Caret();

        Caret(const Caret&) = delete;
        Caret& operator=(const Caret&) = delete;

        const TextPosition& position() const { return position_; }

        // Also resets the blink phase to solid/visible - matches every
        // real text editor's own behavior (typing, or clicking to
        // reposition, always shows a solid caret first rather than
        // picking up wherever the previous blink cycle left off).
        void setPosition(const TextPosition& position);

        const Color& color() const { return color_; }
        void setColor(const Color& color) { color_ = color; }

        // Whether this caret should currently be drawn, factoring in both
        // isActive() and the current blink phase - draw() itself checks
        // this, but exposed separately too in case a caller wants it
        // without actually painting (e.g. to decide whether a repaint is
        // even needed at all).
        bool isVisible() const { return active_ && visible_; }

        bool isActive() const { return active_; }

        // Starts blinking on runLoop - "active" meaning "this control
        // currently has keyboard focus and should show a caret at all".
        // A no-op if already active. Blink rate comes from
        // ::GetCaretBlinkTime() (the same source a real Win32 EDIT
        // control's caret uses) - if the user has disabled caret blinking
        // entirely via that accessibility setting, this stays solidly
        // visible instead, no timer needed.
        void start(RunLoop& runLoop);

        // Same as start(RunLoop&) but with an explicit blink interval
        // instead of ::GetCaretBlinkTime() - mainly so tests don't have
        // to wait on the real system blink rate, though a caller wanting
        // a non-default rate can use this directly too. interval <= 0
        // means "never blink, stay solidly visible" (same behavior
        // start(RunLoop&) falls back to for ::GetCaretBlinkTime()'s own
        // INFINITE/0).
        void start(RunLoop& runLoop, std::chrono::milliseconds interval);

        // Stops blinking and cancels the underlying timer - a no-op if
        // not currently active. Leaves isVisible() false (via
        // active_ == false) regardless of the blink phase at the moment
        // this is called - a caret with no focus draws nothing, not a
        // frozen solid/hidden bar depending on blink timing luck.
        void stop();

        // Paints a solid bar systemCaretWidth() pixels wide and height
        // pixels tall, top-left corner at topLeft, if isVisible() - a
        // no-op otherwise. Drawn in whatever local space ctx is already
        // translated/clipped to, matching every other paint()-style
        // method in this toolkit (see View::paint()). topLeft/height
        // describe where the layout engine (not yet built - Phase 3) has
        // positioned this caret's current position() on screen; the
        // bar's width is this class's own concern, not the caller's -
        // see systemCaretWidth().
        void draw(BLContext& ctx, const Point& topLeft, float height) const;

        // The system's current caret width in pixels (SPI_GETCARETWIDTH) -
        // the same accessibility-configurable value a real Win32 EDIT
        // control's own caret uses (default 1, but a user may have
        // widened it for visibility). Queried fresh on every call rather
        // than cached, matching UIColorManager::isDarkMode()'s own "ask
        // Windows live" convention - cheap, and can change at runtime via
        // WM_SETTINGCHANGE.
        static UINT systemCaretWidth();

    private:
        // The postDelayed() task - toggles visible_ and always returns
        // false (keep blinking) since only stop()/the destructor ever
        // end this early, never the task completing on its own.
        bool onBlinkTick();

        TextPosition position_;
        Color color_{0.0f, 0.0f, 0.0f, 1.0f};
        bool active_ = false;
        bool visible_ = true;

        // Both only meaningful while active_ - null/kInvalidTimerHandle
        // otherwise. runLoop_ is a bare, non-owning pointer: the RunLoop
        // itself is expected to outlive every Caret it's given to (same
        // "the loop outlives what's posted to it" assumption post()/
        // postIdle() callers already make throughout this toolkit).
        RunLoop* runLoop_ = nullptr;
        RunLoop::TimerHandle timerHandle_ = RunLoop::kInvalidTimerHandle;
    };

    // One or more selected ranges within a TextStorage - a vector rather
    // than a single TextRange to leave room for multiple, discontiguous
    // selections (e.g. a future column/block selection, or multi-cursor
    // editing) without a later, wider API change. setRange() below is
    // what every current caller actually uses (the ordinary "click and
    // drag" single-selection case) - addRange() is what a future multi-
    // selection caller would reach for instead.
    class TextSelection {
    public:
        // Fired around every mutation below (setRange()/addRange()/
        // clear()) - Before is vetoable (canChange starts true; a
        // subscriber sets it false to reject the change, the same
        // "starts true, a subscriber flips it to veto" convention
        // RunLoop::onEnding/onModalEnding already use), After is purely
        // informational, matching RunLoop's own onEnding/onEnd split.
        // range describes whatever this specific call is doing - the
        // range being set/added, or an invalid (default-constructed)
        // TextRange for clear() - not this selection's entire ranges()
        // state, which a listener can already read directly.
        //
        // Selection lives here, not on TextModel, deliberately - the
        // same split UIKit keeps: NSTextStorage (content) has no
        // selection concept at all; selectedTextRange and its change
        // notifications belong to UITextView/UITextField (the
        // interactive control), matching where TextSelection itself is
        // meant to be owned (a future TextField, not TextModel).
        typedef Delegate<TextSelection, const TextRange&, bool&> BeforeSelectionChangedDelegate;
        typedef Delegate<TextSelection, const TextRange&> AfterSelectionChangedDelegate;

        BeforeSelectionChangedDelegate onBeforeSelectionChanged;
        AfterSelectionChangedDelegate onAfterSelectionChanged;

        const std::vector<TextRange>& ranges() const { return ranges_; }

        // Replaces every existing range with just this one. A no-op if
        // onBeforeSelectionChanged vetoes.
        void setRange(const TextRange& range);

        // Adds another, independent selected range alongside whatever's
        // already selected, rather than replacing it - see this class's
        // own doc comment. A no-op if onBeforeSelectionChanged vetoes.
        void addRange(const TextRange& range);

        // A no-op if onBeforeSelectionChanged vetoes.
        void clear();

        bool isEmpty() const { return ranges_.empty(); }

        // Whether offset falls inside any one of this selection's ranges.
        bool contains(size_t offset) const;

        // The highlight color this selection paints with - if never
        // overridden via setColor(), this is UIColorManager::colorFor(
        // UIColorRole::HighlightBackground), resolved fresh (not cached)
        // on every call, the same "ask Windows live" convention
        // UIColorManager::colorFor() itself already documents - so this
        // always reflects the user's live Windows accent color and
        // Light/Dark mode setting rather than a fixed guess. Returned by
        // value (not const&) for exactly that reason: there's no single
        // long-lived Color object to hand back a reference to once the
        // live-lookup path can be taken.
        Color color() const;

        // Overrides the default system highlight color above with a
        // fixed one.
        void setColor(const Color& color) {
            colorOverride_ = color;
            hasColorOverride_ = true;
        }

        // Reverts to the live system default after a prior setColor().
        void resetColor() { hasColorOverride_ = false; }

        // Paints one highlight rect per entry in rects, in the same order
        // as ranges() - normally sourced from the layout engine's own
        // HitTestTextRange() (Phase 3), since this class has no layout
        // knowledge of its own to turn a TextRange into screen geometry,
        // the same split Caret::draw() draws along with its own
        // topLeft/height parameters. Only min(ranges().size(),
        // rects.size()) rects are actually drawn - a caller passing a
        // mismatched count gets a partial paint rather than an out-of-
        // bounds access.
        void draw(BLContext& ctx, const std::vector<Rect>& rects) const;

    private:
        std::vector<TextRange> ranges_;
        Color colorOverride_;
        bool hasColorOverride_ = false;
    };

    // Owns a text buffer's actual content - insertion, deletion, and
    // character extraction, addressed throughout by plain offset/length
    // (TextPosition/TextRange above), never an iterator. That's
    // deliberate: a std::wstring backing this directly is today's
    // simplest correct choice, but every public member here is expressed
    // in terms plain offsets can survive a swap to a more sophisticated
    // backing (a piece table, a rope, a gap buffer) without this class's
    // own public API changing at all - only replace()'s own
    // implementation would need to.
    //
    // Pure text storage - no change notification of its own (no
    // onChanged-style delegate). A caller that needs to know when this
    // storage's content changes wraps it in a Model instead (see
    // TextModel), the same "storage vs. observable" split Document draws
    // between raw file content and Model::onChanged().
    class TextStorage {
    public:
        TextStorage() = default;
        explicit TextStorage(const std::wstring& text) : text_(text) {}

        size_t length() const { return text_.size(); }
        bool empty() const { return text_.empty(); }

        const std::wstring& text() const { return text_; }
        void setText(const std::wstring& text) { text_ = text; }

        // The character at offset, or L'\0' if offset is out of range -
        // never throws (a caret one past the last character, or a stale
        // offset after a concurrent edit elsewhere, are both ordinary
        // situations for a caller to hit, not exceptional ones).
        wchar_t at(size_t offset) const;

        // The characters range covers, clamped to this storage's current
        // length() - a range that runs past the end returns however much
        // of it actually exists rather than throwing.
        std::wstring substring(const TextRange& range) const;

        // Inserts text at offset (clamped to [0, length()]) - equivalent
        // to replace(TextRange(offset, 0), text).
        void insert(size_t offset, const std::wstring& text);

        // Removes range's own characters (clamped to this storage's
        // current length()) - equivalent to replace(range, L"").
        void remove(const TextRange& range);

        // Replaces range's own characters (clamped to this storage's
        // current length()) with replacement in one step - insert()/
        // remove() above are both expressed in terms of this.
        void replace(const TextRange& range, const std::wstring& replacement);

    private:
        std::wstring text_;
    };

    // Wraps TextStorage as a Model (models.h) - the "storage vs.
    // observable" split TextStorage's own class comment already promises:
    // TextStorage itself fires no change notification, TextModel is what
    // a caller actually wires up to (via Model::onChanged and
    // Model::addView()) when it wants to know when this text has changed
    // and have every registered View marked dirty for its next repaint.
    // Composition, not inheritance - the same "a Model wraps real domain
    // data, doesn't reimplement its own storage" shape Document draws
    // between raw file content and Model::onChanged(), except TextModel
    // has no file-path/dirty-flag concept of its own: a text field's
    // content isn't inherently tied to a file - a caller wanting that
    // pairs a TextModel with its own Document instead, the same way a
    // caller wanting a captioned Slider already pairs it with its own
    // Label (see Slider's own class comment).
    //
    // storage() is read-only - every mutation goes through this class's
    // own setText()/insert()/remove()/replace() instead of a mutable
    // accessor, so onChanged()/updateAllViews() can never be silently
    // skipped by a caller reaching straight into the underlying
    // TextStorage.
    // Whether a BeforeChar/AfterChar event describes a single character
    // being inserted or removed - see TextModel::onBeforeChar.
    enum class CharChangeKind {
        Inserted,
        Removed
    };

    class TextModel : public Model {
    public:
        // Fired around every content-mutating call below
        // (setText()/insert()/remove()/replace()/clear()) - Before is
        // vetoable (canChange starts true; a subscriber sets it false to
        // reject the edit, the same convention RunLoop::onEnding/
        // onModalEnding already use for a vetoable event, and the same
        // real shape UIKit's own textView(_:shouldChangeTextIn:
        // replacementText:) has - reject or allow, not rewrite in place),
        // After is purely informational, matching RunLoop's own
        // onEnding/onEnd split. This is content's own change history -
        // see TextSelection::onBeforeSelectionChanged/
        // onAfterSelectionChanged for the separate, selection-only
        // events (this class has no relationship to TextSelection at
        // all - see this class's own doc comment on why).
        //
        // Char vs. RangeChanged is mutually exclusive per call, not
        // Char-as-a-specialization-of-RangeChanged: inserting or
        // removing exactly one character fires only the Char pair (the
        // common case for ordinary typing); everything else - a multi-
        // character insert/remove, replace() at any length, setText(),
        // clear() - fires only the RangeChanged pair. A listener that
        // wants to know about every content change regardless of size
        // has to subscribe to both.
        typedef Delegate<TextModel, size_t /*offset*/, wchar_t, CharChangeKind, bool& /*canChange*/> BeforeCharDelegate;
        typedef Delegate<TextModel, size_t /*offset*/, wchar_t, CharChangeKind> AfterCharDelegate;

        // range/replacement describe this specific edit (the span being
        // replaced and what it's being replaced with) in both Before and
        // After - not a before/after-shaped pair of different ranges;
        // After simply confirms the same edit Before already described
        // has now been applied. A caller wanting the range the
        // replacement now actually occupies can compute
        // TextRange(range.start(), replacement.size()) from either.
        typedef Delegate<TextModel, const TextRange&, const std::wstring& /*replacement*/, bool& /*canChange*/> BeforeRangeChangedDelegate;
        typedef Delegate<TextModel, const TextRange&, const std::wstring& /*replacement*/> AfterRangeChangedDelegate;

        BeforeCharDelegate onBeforeChar;
        AfterCharDelegate onAfterChar;
        BeforeRangeChangedDelegate onBeforeRangeChanged;
        AfterRangeChangedDelegate onAfterRangeChanged;

        TextModel() = default;
        explicit TextModel(const std::wstring& text) : storage_(text) {}

        const TextStorage& storage() const { return storage_; }

        size_t length() const { return storage_.length(); }
        bool empty() const override { return storage_.empty(); }

        const std::wstring& text() const { return storage_.text(); }

        // A no-op (storage() left exactly as it was) if onBeforeRangeChanged
        // vetoes.
        void setText(const std::wstring& text);

        // A no-op if onBeforeChar (text.size() == 1) or
        // onBeforeRangeChanged (every other size, including 0) vetoes.
        void insert(size_t offset, const std::wstring& text);

        // A no-op if onBeforeChar (range clamps to length 1) or
        // onBeforeRangeChanged (every other length) vetoes.
        void remove(const TextRange& range);

        // Always goes through onBeforeRangeChanged/onAfterRangeChanged,
        // regardless of range's or replacement's length - see this
        // class's own doc comment on why a 1-for-1 replace() isn't
        // treated as a Char event. A no-op if vetoed.
        void replace(const TextRange& range, const std::wstring& replacement);

        // Model: value()/setValue() bridge the generic std::any API to
        // this class's own typed text() above - value() returns text()
        // boxed as std::any(std::wstring); setValue() calls setText() if
        // newValue actually holds a std::wstring (a no-op otherwise,
        // rather than throwing std::bad_any_cast - matching TextStorage's
        // own "clamp/ignore, don't throw" convention for out-of-range
        // input). Neither calls the Model base version of itself - unlike
        // Document's own setValue() override, setText() below already
        // fires onChanged()/updateAllViews() on its own, so chaining to
        // Model::setValue() too would just fire onChanged() twice.
        std::any value(const std::any& key = std::any()) override;
        void setValue(const std::any& newValue, const std::any& key = std::any()) override;

        // Goes through onBeforeRangeChanged/onAfterRangeChanged the same
        // as any other whole-content replace - a no-op (storage() left
        // untouched, onCleared() never fires) if vetoed.
        void clear() override;

    private:
        // Fires onBeforeChar and returns whether the caller should
        // proceed (canChange's final value).
        bool fireBeforeChar(size_t offset, wchar_t ch, CharChangeKind kind);

        // Fires onBeforeRangeChanged and returns whether the caller
        // should proceed (canChange's final value).
        bool fireBeforeRangeChanged(const TextRange& range, const std::wstring& replacement);

        // Every mutator above ends with this once its own After event
        // has already fired - fires onChanged() (so an owning Controller/
        // TextField can react generically) and updateAllViews() (so every
        // View registered via Model::addView() repaints), the same
        // pairing Model::updateAllViews()'s own doc comment describes a
        // subclass typically wanting alongside onChanged().
        void notifyChanged() {
            onChanged(*this);
            updateAllViews();
        }

        TextStorage storage_;
    };

    // Process-wide Direct2D/DirectWrite factories - Microsoft's own
    // guidance is to create these exactly once and keep them alive for
    // the whole process ("create a single Direct2D factory at the
    // beginning of the application and maintain a reference to it for
    // the life of the application" - ID2D1Factory's own docs), not per-
    // control - same "resolve once, cache for the process" shape
    // FontManager already uses for BLFont, just for a different
    // rendering stack that TextRenderer (below) bridges into this
    // toolkit's own Blend2D-based one.
    //
    // Meyer's singleton, like FontManager/UIColorManager - lazily
    // created on first use, no per-instance state, so callers reach it
    // via the static accessors below rather than constructing one
    // themselves.
    class DirectWriteResources {
    public:
        // All three below throw std::runtime_error if the resource they
        // return never got created successfully - not a mundane,
        // recoverable failure the way a single CreateTextFormat()/
        // CreateBitmap() call failing is (those are checked and degrade
        // to a no-op render() instead - see TextRenderer), but a sign
        // this process's whole D2D/DWrite/WIC stack is unavailable,
        // which shouldn't happen on any supported Windows version.
        // Throwing matches Button::paint()'s own precedent for a
        // comparably "should never happen" condition (its own BLFont-
        // resolution check, controls.cpp) rather than every caller
        // null-dereferencing instead.
        static ID2D1Factory& d2dFactory();
        static IDWriteFactory& dwriteFactory();

        // WIC (Windows Imaging Component) - what TextRenderer uses to
        // create an IWICBitmap a D2D render target can draw directly
        // into, then read pixels back out of via IWICBitmapLock (no
        // GDI/DIB/HDC involved anywhere - see TextRenderer's own class
        // comment). Unlike d2dFactory()/dwriteFactory() above, creating
        // this needs COM initialized on the calling thread
        // (IWICImagingFactory only comes from CoCreateInstance) -
        // handled inside this class's own constructor, tolerating
        // "already initialized" (S_FALSE) as success, since the real app
        // already has COM initialized by the time this runs
        // (RunLoop::run() calls OleInitialize()).
        static IWICImagingFactory& wicFactory();

        DirectWriteResources(const DirectWriteResources&) = delete;
        DirectWriteResources& operator=(const DirectWriteResources&) = delete;

    private:
        DirectWriteResources();
        // Declared (not = default) and defined in text.cpp, where Impl
        // is a complete type - see this file's own top-of-file comment
        // on why Impl (holding the real _com_ptr_t members) can't be
        // named here.
        ~DirectWriteResources();

        static DirectWriteResources& instance();

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };

    // Bridges DirectWrite/Direct2D text painting into this toolkit's own
    // Blend2D-based rendering - packages the spike proven out in
    // examples/directwrite1.cpp into reusable machinery instead of every
    // control hand-rolling its own copy. Owns a private pixel buffer
    // (entirely separate from whatever BLContext a caller's own paint()
    // eventually blits into - same "hand it its own buffer rather than
    // reach into anything shared" shape gfx::Image already uses),
    // wrapped as an IWICBitmap (DirectWriteResources::wicFactory()) that
    // a D2D render target draws directly into - no GDI, no DIB, no HDC
    // anywhere in this path (an earlier version used an
    // ID2D1DCRenderTarget bound to a DIB-backed HDC per render() call;
    // WIC lets D2D write straight into a plain buffer instead). render()
    // draws text via a fresh IDWriteTextLayout, then wraps that same
    // buffer directly into a BLImage (zero copy, zero conversion -
    // DXGI_FORMAT_B8G8R8A8_UNORM/D2D1_ALPHA_MODE_PREMULTIPLIED is byte-
    // for-byte the same premultiplied top-down BGRA layout as
    // BL_FORMAT_PRGB32) and blits that into ctx.
    //
    // One instance per control that needs to draw DirectWrite text (e.g.
    // owned by TextField) - the buffer/render target are sized to that
    // control's own bounds and recreated together whenever that size
    // changes (unlike a DC render target, a WIC bitmap render target is
    // permanently bound to the bitmap it was created with, so a size
    // change means building both fresh, not just re-binding). Instances
    // aren't interchangeable/shareable the way DirectWriteResources' own
    // factories are. Also caches the last IDWriteTextFormat it resolved
    // (keyed by font name/size/bold/italic), the same "resolve lazily,
    // re-resolve only when it actually changes" shape Font::blFont()
    // itself already uses for BLFont.
    class TextRenderer {
    public:
        // Declared (not = default) and defined in text.cpp, where Impl
        // is a complete type - see this file's own top-of-file comment.
        TextRenderer();
        ~TextRenderer();

        TextRenderer(const TextRenderer&) = delete;
        TextRenderer& operator=(const TextRenderer&) = delete;

        // Draws text into ctx at (0,0), sized/clipped to width x height
        // and colored with textColor - a no-op if width/height aren't
        // positive, or if the underlying D2D/DWrite/WIC resources fail
        // to (re)create. width/height normally come straight from the
        // owning control's own bounds() (e.g. View::bounds()).
        //
        // scrollOffsetY (default 0, TextField's own single-line case)
        // shifts the underlying IDWriteTextLayout's own draw origin up
        // by that many pixels before rasterizing - what TextController
        // (controls.h/.cpp) uses for a scrolled multi-line TextControl,
        // so only the currently-visible slice of a much taller document
        // ever gets drawn into a render target that's still just
        // width x height pixels (never the full content height) - see
        // TextController::paint()'s own doc comment for why that matters.
        // Safe regardless of how large scrollOffsetY is: DirectWrite
        // doesn't truncate line generation based on the layout's own
        // maxHeight (same "maxHeight is soft" behavior
        // TextLayoutEngine::lineRange()'s own doc comment documents), and
        // D2D simply never rasterizes glyphs that land outside the
        // render target's own pixel bounds after the origin shift - no
        // separate "how tall is the real content" parameter needed here.
        void render(BLContext& ctx, int width, int height, const std::wstring& text,
            const Font& font, const Color& textColor, float scrollOffsetY = 0.0f);

    private:
        // (Re)builds the WIC bitmap + render target together for the
        // given size if either doesn't already exist at that size - see
        // this class's own doc comment on why the two can't be resized
        // independently the way the old DC-render-target version could.
        bool ensureRenderTarget(int width, int height);

        // Holds the real ID2D1RenderTarget/IWICBitmap (_com_ptr_t)
        // members, plus a cached IDWriteTextFormat (via the same
        // text.cpp-local TextFormatCache helper TextLayoutEngine also
        // uses, rather than each hand-rolling its own copy of the same
        // "resolve lazily, re-resolve only when the Font actually
        // changes" logic) - see this file's own top-of-file comment on
        // why none of this can be named directly here.
        struct Impl;
        std::unique_ptr<Impl> impl_;

        // The size impl_'s IWICBitmap/renderTarget were last (re)built
        // at - ensureRenderTarget() compares against this to decide
        // whether a resize actually happened. No pixel buffer of our own
        // to track: the IWICBitmap owns its own storage, read back via
        // IWICBitmapLock in render() rather than written into anything
        // this class allocates itself.
        int bufferWidth_ = 0;
        int bufferHeight_ = 0;
    };

    // Owns a persistent IDWriteTextLayout for one TextStorage's current
    // content, kept around across paint() calls - unlike TextRenderer,
    // which builds and discards a fresh IDWriteTextLayout every render()
    // (fine for painting alone, but wasteful and pointless to redo on
    // every mouse click or caret/selection repaint) - so hit-testing can
    // reuse the same already-computed layout instead. update() rebuilds
    // lazily: only when the text/font/maxWidth/maxHeight passed to it
    // actually differ from last time, the same "resolve lazily, cache
    // last" shape Font::blFont()/TextRenderer's own cached
    // IDWriteTextFormat already use.
    //
    // Every public method here is expressed in newui's own vocabulary
    // (Rect/TextRange/TextPosition, never a raw DirectWrite type) - same
    // Pimpl-driven reason TextRenderer's own public API stays D2D/DWrite-
    // free (see this file's own top-of-file comment).
    class TextLayoutEngine {
    public:
        TextLayoutEngine();
        ~TextLayoutEngine();

        TextLayoutEngine(const TextLayoutEngine&) = delete;
        TextLayoutEngine& operator=(const TextLayoutEngine&) = delete;

        // Rebuilds the underlying layout if storage's text(), font, or
        // maxWidth/maxHeight actually changed since the last call - a
        // no-op otherwise. Returns whether a usable layout exists
        // afterward (false if this is the first call and the underlying
        // DirectWrite calls failed) - every hit-testing method below
        // returns its own "nothing" value regardless, so checking this
        // is optional, not required before calling them.
        bool update(const TextStorage& storage, const Font& font, float maxWidth, float maxHeight);

        // One highlight rect per contiguous visual run range covers (a
        // multi-line selection spans more than one line, hence possibly
        // more than one rect per range - see IDWriteTextLayout::
        // HitTestTextRange()) - directly usable as TextSelection::draw()'s
        // own rects parameter. Empty if no layout has been built yet.
        std::vector<Rect> hitTestRange(const TextRange& range) const;

        // Where a caret at position should be drawn - directly usable as
        // Caret::draw()'s own topLeft/height parameters. Leaves both
        // outputs at their default (Point(), 0.0f) if no layout has been
        // built yet.
        void hitTestPosition(const TextPosition& position, Point& outTopLeft, float& outHeight) const;

        // The TextPosition nearest localPoint (e.g. a mouse click, in
        // the same local space update()'s own maxWidth/maxHeight - and
        // therefore the layout itself - were built against) - for
        // turning a click into a caret placement. TextPosition() (invalid)
        // if no layout has been built yet.
        TextPosition hitTestPoint(const Point& localPoint) const;

        // The [start, start+length) character range of the visual line
        // containing position - excluding any trailing hard line-break
        // character(s), so a caller using this for "select/move to the
        // end of the line" (e.g. TextController's own Home/End handling,
        // controls.h/.cpp) lands before the newline, not after it. Not
        // the same as "the paragraph" for a *wrapped* line - each visual
        // line DirectWrite actually draws gets its own range here, same
        // granularity hitTestRange()'s own per-line rects already use.
        // TextRange() (invalid) if no layout has been built yet or
        // position is invalid.
        TextRange lineRange(const TextPosition& position) const;

        // The full height every line of text actually needs at the
        // current maxWidth (from the last update() call), regardless of
        // maxHeight - DirectWrite doesn't truncate line generation just
        // because it exceeds the layout's own maxHeight (that only
        // affects alignment/hit-testing bounds, not how many lines get
        // laid out - see lineRange()'s own doc comment), so this can -
        // and for genuinely overflowing content, does - exceed maxHeight.
        // What TextController (controls.h/.cpp) uses to decide whether a
        // multi-line TextControl needs its vertical scrollbar. 0.0f if
        // no layout has been built yet.
        float contentHeight() const;

    private:
        // Holds the real IDWriteTextLayout (_com_ptr_t), plus a cached
        // IDWriteTextFormat (the same shared TextFormatCache helper
        // TextRenderer uses) - see this file's own top-of-file comment
        // on why neither can be named directly here.
        struct Impl;
        std::unique_ptr<Impl> impl_;

        std::wstring lastText_;
        float lastMaxWidth_ = 0.0f;
        float lastMaxHeight_ = 0.0f;
    };

}

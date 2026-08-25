#pragma once

#include <newui/newui.h>

#include <blend2d/blend2d.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace newui {

class View;

namespace detail {

    // Deleter for a GlobalAlloc()'d HGLOBAL - GlobalFree() is the natural
    // std::unique_ptr deleter for it, since HGLOBAL is itself already an
    // opaque pointer type (DECLARE_HANDLE).
    struct GlobalMemoryDeleter {
        void operator()(HGLOBAL handle) const {
            if (handle) {
                ::GlobalFree(handle);
            }
        }
    };

    // Move-only RAII ownership of a GlobalAlloc()'d HGLOBAL - a plain
    // std::unique_ptr alias rather than a hand-rolled class, since
    // GlobalFree() is exactly a unique_ptr deleter shape already.
    // release() (inherited from unique_ptr) is how a caller hands
    // ownership off to something that takes over ownership itself, e.g.
    // SetClipboardData().
    using GlobalMemoryHandle = std::unique_ptr<std::remove_pointer_t<HGLOBAL>, GlobalMemoryDeleter>;

    // Calls GlobalAlloc(flags, size). Throws std::runtime_error if the
    // allocation fails.
    GlobalMemoryHandle allocGlobalMemory(UINT flags, std::size_t size);

    // Deleter for a GlobalLock()'d pointer. GlobalUnlock() takes the
    // *handle*, not the locked pointer it returned - unlike
    // GlobalMemoryDeleter above, this deleter has to carry that handle
    // itself since std::unique_ptr's stored pointer alone isn't enough
    // to call it.
    class GlobalMemoryUnlocker {
    public:
        explicit GlobalMemoryUnlocker(HGLOBAL handle) : handle_(handle) {}

        void operator()(void*) const { ::GlobalUnlock(handle_); }

    private:
        HGLOBAL handle_;
    };

    // Move-only RAII scope for a GlobalLock() taken on some HGLOBAL -
    // does not own that handle itself, only the lock. get() returns the
    // locked pointer; cast it to whatever type the caller is reading or
    // writing through.
    using GlobalMemoryLock = std::unique_ptr<void, GlobalMemoryUnlocker>;

    // Calls GlobalLock(handle). Throws std::runtime_error if it returns
    // nullptr.
    GlobalMemoryLock lockGlobalMemory(HGLOBAL handle);

    // CF_HTML's real payload isn't raw HTML - it's a fixed-field ASCII
    // header (Version/StartHTML/EndHTML/StartFragment/EndFragment, each
    // a 10-digit zero-padded byte offset into the *whole* payload)
    // followed by the actual markup wrapped in <!--StartFragment-->/
    // <!--EndFragment--> comments. Pure data transforms, no clipboard
    // interaction - kept in detail:: (like the GlobalMemory* wrappers
    // above) so they're directly unit-testable without touching a real
    // clipboard.
    std::vector<std::uint8_t> wrapCfHtml(const std::vector<std::uint8_t>& rawHtml);

    // Reverse of wrapCfHtml() - extracts just the fragment between
    // StartFragment/EndFragment's offsets. Returns cfHtmlBytes unchanged
    // if the expected header tokens aren't found - a real defensive
    // need, not speculative, since nothing guarantees another
    // application's own "HTML Format" payload is well-formed.
    std::vector<std::uint8_t> unwrapCfHtml(const std::vector<std::uint8_t>& cfHtmlBytes);

    // RAII wrapper around OpenClipboard()/CloseClipboard(). The clipboard
    // is a single, process-wide, briefly-lockable resource another
    // process can be holding at the moment this is constructed - the
    // constructor retries a few times with a short delay before giving
    // up, since a competing OpenClipboard() is normally released almost
    // immediately. Unlike the two wrappers above, there's no pointer-shaped
    // resource here to hand off to std::unique_ptr - just a scope to
    // close on exit - so this stays a plain RAII class.
    class ClipboardScope {
    public:
        // Tries OpenClipboard(owner) up to maxAttempts times, waiting
        // retryDelay between attempts. Throws std::runtime_error if every
        // attempt fails.
        explicit ClipboardScope(HWND owner = nullptr, unsigned maxAttempts = 5,
            std::chrono::milliseconds retryDelay = std::chrono::milliseconds(10));

        ~ClipboardScope();

        ClipboardScope(const ClipboardScope&) = delete;
        ClipboardScope& operator=(const ClipboardScope&) = delete;
    };

}

// Public clipboard API - a static-method utility class (same call shape
// as UIColorManager) built on the RAII wrappers in newui::detail above.
// Failures (clipboard busy, format unavailable, allocation failure) are
// reported via the bool return, not an exception - a caller asking "did
// this work" shouldn't have to wrap every call in a try/catch to find
// out, unlike the detail:: wrappers' own invariant checks.
//
// Holds one piece of real process-wide state - the delayed-render
// registry - since clipboard ownership itself is a single, process-wide
// concept (only one window is ever the real delayed-render owner at a
// time), not something that belongs distributed across every RootView
// instance. Kept as a private Meyer's singleton (instance(), same
// pattern as UIColorManager's own) rather than a bare function-local
// static hidden inside one method - a real instance is the natural
// single home for whatever internal state this class needs, and stays
// easy to extend if more gets added later, instead of accumulating more
// independent statics scattered through clipboardmgr.cpp. Unlike
// UIColorManager, instance() itself stays private - nothing outside this
// class has ever needed a reference to it, only the static methods below.
class ClipboardManager {
public:
    // Claims clipboard ownership (EmptyClipboard()) and sets its content
    // to text as CF_UNICODETEXT. owner's rootView()'s HWND (see
    // View::rootView()/RootView::windowHandle()) is passed to
    // OpenClipboard() - nullptr (the default, or a View not currently
    // attached to a RootView) opens the clipboard unassociated with any
    // window. Returns false if the clipboard couldn't be opened or the
    // allocation failed.
    static bool setText(const std::wstring& text, View* owner = nullptr);

    // Reads CF_UNICODETEXT into outText, leaving it unmodified and
    // returning false if the clipboard couldn't be opened or holds no
    // text.
    static bool getText(std::wstring& outText);

    // Thin wrapper over RegisterClipboardFormatW() - registers formatName
    // (an arbitrary string, e.g. a MIME type) and returns its UINT atom,
    // or 0 on failure.
    static UINT registerCustomFormat(const std::wstring& formatName);

    // Claims clipboard ownership and writes data under format (typically
    // one registerCustomFormat() returned) - same owner/EmptyClipboard()
    // semantics as setText(). Returns false if the clipboard couldn't be
    // opened or the allocation failed.
    static bool setCustomData(UINT format, const std::vector<std::uint8_t>& data, View* owner = nullptr);

    // Reads the raw bytes currently on the clipboard under format into
    // outData, leaving it unmodified and returning false if the
    // clipboard couldn't be opened or holds nothing under that format.
    static bool getCustomData(UINT format, std::vector<std::uint8_t>& outData);

    // Supplied to setDelayedRenderer() to produce format's real data only
    // once some other application actually asks for it, instead of
    // paying to serialize it up front on every copy.
    using DelayedRenderer = std::function<std::vector<std::uint8_t>()>;

    // Registers renderer to answer format's data lazily, then claims
    // clipboard ownership and calls SetClipboardData(format, nullptr) -
    // the documented Win32 signal that this window will render format on
    // request. owner must be attached to a RootView (delayed rendering
    // needs a real window to receive WM_RENDERFORMAT/WM_RENDERALLFORMATS) -
    // returns false, and registers nothing, if it isn't, or if the
    // clipboard couldn't be opened. Replaces any previously registered
    // renderer for the same format. App code registers via this call and
    // never touches handleRenderFormat()/handleRenderAllFormats() below
    // directly - RootView::handleMessage() forwards the two Win32
    // messages there on this window's behalf.
    static bool setDelayedRenderer(UINT format, DelayedRenderer renderer, View* owner);

    // Removes format's registered DelayedRenderer, if any - e.g. once its
    // owning window is destroyed and the promised data can never
    // actually be rendered. A no-op if none is registered. Doesn't touch
    // the clipboard itself, only this registry - without this, a
    // renderer registered by code that's since gone away (e.g.
    // capturing state by reference) would sit here indefinitely, ready
    // to be invoked by a later, unrelated handleRenderAllFormats() call.
    static void clearDelayedRenderer(UINT format);

    // RootView::handleMessage()'s WM_RENDERFORMAT case calls this with
    // wParam - renders format via its registered DelayedRenderer (a
    // no-op if none is registered) and hands the bytes straight to
    // SetClipboardData(), without opening the clipboard first - matching
    // WM_RENDERFORMAT's own documented contract.
    static void handleRenderFormat(UINT format);

    // RootView::handleMessage()'s WM_RENDERALLFORMATS case calls this
    // with its own windowHandle() - opens the clipboard, renders every
    // still-registered DelayedRenderer, and closes it, so every promised
    // format is actually available once owner is destroyed (Win32's own
    // documented contract for this message). A no-op if nothing is
    // registered.
    static void handleRenderAllFormats(HWND owner);

    // --- Phase 4: MIME type mapping layer ---
    // A MIME-string-shaped view of the clipboard ("text/html",
    // "image/png", ...) built on registerCustomFormat()/setCustomData()/
    // getCustomData() above, not a separate mechanism - see
    // clipboard-plan.md's own Phase 4 for the full design and the
    // standard MIME <-> Win32 format table getOrRegisterFormat() is
    // pre-seeded with.

    // Resolves mimeType to its registered Win32 format - a pre-seeded
    // standard one for a well-known MIME type (e.g. "text/plain" ->
    // CF_UNICODETEXT, "text/html" -> the real "HTML Format" atom, not a
    // second "text/html"-named registration) or a freshly
    // RegisterClipboardFormatW()'d one otherwise. Cached both directions
    // after the first call for a given mimeType.
    static UINT getOrRegisterFormat(const std::wstring& mimeType);

    // Reverse of getOrRegisterFormat() - the MIME string formatId was
    // registered/resolved under. Falls back to GetClipboardFormatNameW()
    // for a format this process never registered itself (e.g. another
    // application's own custom format already resident on the
    // clipboard), caching that result too. Returns "" if formatId has no
    // string name at all (a plain predefined numeric format, e.g.
    // CF_BITMAP).
    static std::wstring formatName(UINT formatId);

    // Every MIME type currently resolvable on the clipboard -
    // EnumClipboardFormats() resolved through formatName(), skipping any
    // format that comes back with no string name. Empty (not an
    // exception) if the clipboard couldn't be opened.
    static std::vector<std::wstring> getAvailableMimeTypes();

    // Writes data under mimeType's resolved format - same owner/
    // EmptyClipboard() semantics as setCustomData(). "text/html" is
    // transparently CF_HTML-wrapped first (wrapCfHtml()); every other
    // mimeType is written as-is, including "text/plain" - unlike
    // setText(), data here is a raw byte blob, so a caller writing
    // "text/plain" through this instead of setText() directly is
    // responsible for it already being well-formed null-terminated
    // UTF-16 (CF_UNICODETEXT's own layout).
    static bool setMimeData(const std::wstring& mimeType, const std::vector<std::uint8_t>& data, View* owner = nullptr);

    // Reads mimeType's resolved format back - "text/html" is
    // transparently unwrapped from CF_HTML first (unwrapCfHtml()).
    static bool getMimeData(const std::wstring& mimeType, std::vector<std::uint8_t>& outData);

    // application/x-file-list <-> CF_HDROP (a real DROPFILES structure,
    // not a byte blob) - kept separate from setMimeData()/getMimeData()
    // rather than force that shape through a vector<uint8_t> signature.
    enum class DropEffect { None, Copy, Move, Link };

    // Claims clipboard ownership and writes paths as CF_HDROP, plus
    // Explorer's own "Preferred DropEffect" flag (a 4-byte DROPEFFECT_*
    // DWORD riding alongside CF_HDROP that tells Explorer whether to
    // still move the files after paste) under the same EmptyClipboard().
    // Returns false (writes nothing) if paths is empty.
    static bool setFileList(const std::vector<std::wstring>& paths, DropEffect effect = DropEffect::Copy, View* owner = nullptr);

    // Reads CF_HDROP's paths into outPaths via DragQueryFileW(). Returns
    // false, leaving outPaths unmodified, if the clipboard couldn't be
    // opened or holds no CF_HDROP.
    static bool getFileList(std::vector<std::wstring>& outPaths);

    // Whether CF_HDROP's paths (if present) were Cut (Move) or Copied -
    // Copy if "Preferred DropEffect" isn't present at all (Explorer's
    // own default when nothing else says otherwise). Read-only by
    // design: only ever *write* this deliberately via setFileList()'s
    // own effect parameter, never as a side effect of unrelated code -
    // Explorer relies on it to know whether to still move the files
    // after paste.
    static DropEffect getPreferredDropEffect();

    // image/bmp <-> CF_DIB - built on Blend2D's own BMP codec (already a
    // project dependency) rather than GDI+: CF_DIB's raw payload is
    // exactly a BMP file's body (BITMAPINFOHEADER + pixel data) with the
    // 14-byte BITMAPFILEHEADER stripped off, so encoding/decoding a real
    // BMP via BLImage and stripping/synthesizing that header around it
    // does the actual transcoding work. setImage() claims clipboard
    // ownership like setCustomData(); getImage() reads back like
    // getCustomData(). Returns false on any encode/decode failure
    // (including image being empty).
    static bool setImage(const BLImage& image, View* owner = nullptr);
    static bool getImage(BLImage& outImage);

    ClipboardManager(const ClipboardManager&) = delete;
    ClipboardManager& operator=(const ClipboardManager&) = delete;

private:
    ClipboardManager();

    void registerStandardMappings();

    // Meyer's singleton - guaranteed initialized before first use
    // regardless of static-initialization order across translation
    // units. Private: everything outside this class goes through the
    // static methods above instead.
    static ClipboardManager& instance();

    // Backs setDelayedRenderer()/handleRenderFormat()/
    // handleRenderAllFormats() - see their own doc comments above.
    std::unordered_map<UINT, DelayedRenderer> delayedRenderers_;

    // Backs getOrRegisterFormat()/formatName() - pre-seeded by
    // registerStandardMappings() at construction, grown lazily
    // afterward.
    std::unordered_map<std::wstring, UINT> mimeToFormat_;
    std::unordered_map<UINT, std::wstring> formatToMime_;
};

}

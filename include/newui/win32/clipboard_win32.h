#pragma once

// Windows-only clipboard helpers (HGLOBAL RAII, CF_HTML wrapping, OpenClipboard scope) behind ClipboardManager.

#include "newui/platform.h"

#if !NEWUI_PLATFORM_WIN32
#   error "newui/win32/clipboard_win32.h is Windows-only"
#endif

#include <newui/newui.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace newui {

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

}

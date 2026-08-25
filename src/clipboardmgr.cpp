#include "newui/clipboardmgr.h"

#include "newui/rootview.h"
#include "newui/view.h"

#include <shellapi.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace newui {
namespace detail {

    GlobalMemoryHandle allocGlobalMemory(UINT flags, std::size_t size) {
        GlobalMemoryHandle handle(::GlobalAlloc(flags, size));
        if (!handle) {
            throw std::runtime_error("GlobalAlloc failed");
        }
        return handle;
    }

    GlobalMemoryLock lockGlobalMemory(HGLOBAL handle) {
        void* ptr = ::GlobalLock(handle);
        if (!ptr) {
            throw std::runtime_error("GlobalLock failed");
        }
        return GlobalMemoryLock(ptr, GlobalMemoryUnlocker(handle));
    }

    ClipboardScope::ClipboardScope(HWND owner, unsigned maxAttempts, std::chrono::milliseconds retryDelay) {
        for (unsigned attempt = 0; attempt < maxAttempts; ++attempt) {
            if (::OpenClipboard(owner)) {
                return;
            }
            if (attempt + 1 < maxAttempts) {
                std::this_thread::sleep_for(retryDelay);
            }
        }
        throw std::runtime_error("OpenClipboard failed - clipboard busy");
    }

    ClipboardScope::~ClipboardScope() {
        ::CloseClipboard();
    }

    // owner's RootView's HWND, or nullptr if owner is null or not
    // currently attached to a RootView.
    HWND windowHandleOf(View* owner) {
        if (!owner || !owner->rootView()) {
            return nullptr;
        }
        return owner->rootView()->windowHandle();
    }

    namespace {
        const char* const kCfHtmlHeaderTemplate =
            "Version:0.9\r\n"
            "StartHTML:%010zu\r\n"
            "EndHTML:%010zu\r\n"
            "StartFragment:%010zu\r\n"
            "EndFragment:%010zu\r\n";
        const char* const kCfHtmlBodyPrefix = "<html>\r\n<body>\r\n<!--StartFragment-->";
        const char* const kCfHtmlBodySuffix = "<!--EndFragment-->\r\n</body>\r\n</html>";
    }

    std::vector<std::uint8_t> wrapCfHtml(const std::vector<std::uint8_t>& rawHtml) {
        // The header's own length is fixed once every offset is
        // zero-padded to 10 digits (%010zu), regardless of the real
        // values plugged in - computed here via a throwaway snprintf()
        // rather than hardcoded, so a future template change can't
        // silently desync the offsets it writes below.
        char probe[256];
        int headerLen = std::snprintf(probe, sizeof(probe), kCfHtmlHeaderTemplate,
            static_cast<std::size_t>(0), static_cast<std::size_t>(0),
            static_cast<std::size_t>(0), static_cast<std::size_t>(0));

        std::size_t prefixLen = std::strlen(kCfHtmlBodyPrefix);
        std::size_t suffixLen = std::strlen(kCfHtmlBodySuffix);

        std::size_t startHtml = static_cast<std::size_t>(headerLen);
        std::size_t startFragment = startHtml + prefixLen;
        std::size_t endFragment = startFragment + rawHtml.size();
        std::size_t endHtml = endFragment + suffixLen;

        char header[256];
        int written = std::snprintf(header, sizeof(header), kCfHtmlHeaderTemplate,
            startHtml, endHtml, startFragment, endFragment);

        std::vector<std::uint8_t> result;
        result.reserve(static_cast<std::size_t>(written) + prefixLen + rawHtml.size() + suffixLen);
        result.insert(result.end(), header, header + written);
        result.insert(result.end(), kCfHtmlBodyPrefix, kCfHtmlBodyPrefix + prefixLen);
        result.insert(result.end(), rawHtml.begin(), rawHtml.end());
        result.insert(result.end(), kCfHtmlBodySuffix, kCfHtmlBodySuffix + suffixLen);
        return result;
    }

    std::vector<std::uint8_t> unwrapCfHtml(const std::vector<std::uint8_t>& cfHtmlBytes) {
        std::string text(cfHtmlBytes.begin(), cfHtmlBytes.end());

        auto readOffset = [&text](const char* key) -> std::size_t {
            std::size_t pos = text.find(key);
            if (pos == std::string::npos) {
                return std::string::npos;
            }
            pos += std::strlen(key);
            return static_cast<std::size_t>(std::strtoull(text.c_str() + pos, nullptr, 10));
        };

        std::size_t startFragment = readOffset("StartFragment:");
        std::size_t endFragment = readOffset("EndFragment:");

        if (startFragment == std::string::npos || endFragment == std::string::npos ||
            startFragment >= cfHtmlBytes.size() || endFragment > cfHtmlBytes.size() ||
            startFragment >= endFragment) {
            return cfHtmlBytes;
        }

        return std::vector<std::uint8_t>(cfHtmlBytes.begin() + startFragment, cfHtmlBytes.begin() + endFragment);
    }

}

bool ClipboardManager::setText(const std::wstring& text, View* owner) {
    try {
        detail::ClipboardScope scope(detail::windowHandleOf(owner));
        if (!::EmptyClipboard()) {
            return false;
        }

        std::size_t size = (text.length() + 1) * sizeof(wchar_t);
        detail::GlobalMemoryHandle handle = detail::allocGlobalMemory(GHND, size);
        {
            detail::GlobalMemoryLock lock = detail::lockGlobalMemory(handle.get());
            std::memcpy(lock.get(), text.c_str(), size);
        }

        if (!::SetClipboardData(CF_UNICODETEXT, handle.get())) {
            return false;
        }
        handle.release();
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool ClipboardManager::getText(std::wstring& outText) {
    try {
        detail::ClipboardScope scope;
        if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) {
            return false;
        }

        HANDLE data = ::GetClipboardData(CF_UNICODETEXT);
        if (!data) {
            return false;
        }

        detail::GlobalMemoryLock lock = detail::lockGlobalMemory(static_cast<HGLOBAL>(data));
        outText = static_cast<const wchar_t*>(lock.get());
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

UINT ClipboardManager::registerCustomFormat(const std::wstring& formatName) {
    return ::RegisterClipboardFormatW(formatName.c_str());
}

bool ClipboardManager::setCustomData(UINT format, const std::vector<std::uint8_t>& data, View* owner) {
    try {
        detail::ClipboardScope scope(detail::windowHandleOf(owner));
        if (!::EmptyClipboard()) {
            return false;
        }

        // GlobalAlloc(GHND, 0)/SetClipboardData() don't reliably round-trip
        // a genuine zero-byte block (confirmed live - SetClipboardData()
        // fails for one) - allocate at least 1 byte regardless of data's
        // real size.
        detail::GlobalMemoryHandle handle = detail::allocGlobalMemory(GHND, data.empty() ? 1 : data.size());
        if (!data.empty()) {
            detail::GlobalMemoryLock lock = detail::lockGlobalMemory(handle.get());
            std::memcpy(lock.get(), data.data(), data.size());
        }

        if (!::SetClipboardData(format, handle.get())) {
            return false;
        }
        handle.release();
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool ClipboardManager::getCustomData(UINT format, std::vector<std::uint8_t>& outData) {
    try {
        detail::ClipboardScope scope;
        if (!::IsClipboardFormatAvailable(format)) {
            return false;
        }

        HANDLE data = ::GetClipboardData(format);
        if (!data) {
            return false;
        }

        HGLOBAL handle = static_cast<HGLOBAL>(data);
        std::size_t size = ::GlobalSize(handle);
        detail::GlobalMemoryLock lock = detail::lockGlobalMemory(handle);
        const std::uint8_t* bytes = static_cast<const std::uint8_t*>(lock.get());
        outData.assign(bytes, bytes + size);
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool ClipboardManager::setDelayedRenderer(UINT format, DelayedRenderer renderer, View* owner) {
    HWND hwnd = detail::windowHandleOf(owner);
    if (!hwnd) {
        return false;
    }

    try {
        detail::ClipboardScope scope(hwnd);
        if (!::EmptyClipboard()) {
            return false;
        }
        instance().delayedRenderers_[format] = std::move(renderer);
        ::SetClipboardData(format, nullptr);
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

void ClipboardManager::clearDelayedRenderer(UINT format) {
    instance().delayedRenderers_.erase(format);
}

void ClipboardManager::handleRenderFormat(UINT format) {
    auto& renderers = instance().delayedRenderers_;
    auto it = renderers.find(format);
    if (it == renderers.end()) {
        return;
    }

    std::vector<std::uint8_t> data = it->second();
    try {
        detail::GlobalMemoryHandle handle = detail::allocGlobalMemory(GHND, data.empty() ? 1 : data.size());
        if (!data.empty()) {
            detail::GlobalMemoryLock lock = detail::lockGlobalMemory(handle.get());
            std::memcpy(lock.get(), data.data(), data.size());
        }
        // No OpenClipboard() here - WM_RENDERFORMAT's own documented
        // contract is that the clipboard is already open and owned by
        // this window for the duration of this call.
        ::SetClipboardData(format, handle.get());
        handle.release();
    } catch (const std::runtime_error&) {
        // Nothing more this can do - format simply stays unrendered.
    }
}

void ClipboardManager::handleRenderAllFormats(HWND owner) {
    auto& renderers = instance().delayedRenderers_;
    if (renderers.empty()) {
        return;
    }

    try {
        detail::ClipboardScope scope(owner);
        for (auto& [format, renderer] : renderers) {
            std::vector<std::uint8_t> data = renderer();
            detail::GlobalMemoryHandle handle = detail::allocGlobalMemory(GHND, data.empty() ? 1 : data.size());
            if (!data.empty()) {
                detail::GlobalMemoryLock lock = detail::lockGlobalMemory(handle.get());
                std::memcpy(lock.get(), data.data(), data.size());
            }
            ::SetClipboardData(format, handle.get());
            handle.release();
        }
    } catch (const std::runtime_error&) {
        // Best-effort - the window is going away regardless.
    }
}

ClipboardManager::ClipboardManager() {
    registerStandardMappings();
}

void ClipboardManager::registerStandardMappings() {
    auto seed = [this](const std::wstring& mime, UINT format) {
        mimeToFormat_[mime] = format;
        formatToMime_[format] = mime;
    };

    seed(L"text/plain", CF_UNICODETEXT);
    seed(L"text/html", ::RegisterClipboardFormatW(L"HTML Format"));
    seed(L"text/rtf", ::RegisterClipboardFormatW(L"Rich Text Format"));
    seed(L"application/json", ::RegisterClipboardFormatW(L"application/json"));
    seed(L"image/png", ::RegisterClipboardFormatW(L"PNG"));
    seed(L"image/bmp", CF_DIB);
    seed(L"image/jpeg", ::RegisterClipboardFormatW(L"JFIF"));
    seed(L"application/x-file-list", CF_HDROP);
    seed(L"text/uri-list", ::RegisterClipboardFormatW(L"UniformResourceLocatorW"));
}

UINT ClipboardManager::getOrRegisterFormat(const std::wstring& mimeType) {
    ClipboardManager& mgr = instance();

    auto it = mgr.mimeToFormat_.find(mimeType);
    if (it != mgr.mimeToFormat_.end()) {
        return it->second;
    }

    UINT format = ::RegisterClipboardFormatW(mimeType.c_str());
    if (format != 0) {
        mgr.mimeToFormat_[mimeType] = format;
        mgr.formatToMime_[format] = mimeType;
    }
    return format;
}

std::wstring ClipboardManager::formatName(UINT formatId) {
    ClipboardManager& mgr = instance();

    auto it = mgr.formatToMime_.find(formatId);
    if (it != mgr.formatToMime_.end()) {
        return it->second;
    }

    wchar_t buffer[256];
    int length = ::GetClipboardFormatNameW(formatId, buffer, 256);
    if (length <= 0) {
        return L"";
    }

    std::wstring name(buffer, static_cast<std::size_t>(length));
    mgr.formatToMime_[formatId] = name;
    mgr.mimeToFormat_[name] = formatId;
    return name;
}

std::vector<std::wstring> ClipboardManager::getAvailableMimeTypes() {
    std::vector<std::wstring> result;
    try {
        detail::ClipboardScope scope;
        UINT format = 0;
        while ((format = ::EnumClipboardFormats(format)) != 0) {
            std::wstring name = formatName(format);
            if (!name.empty()) {
                result.push_back(std::move(name));
            }
        }
    } catch (const std::runtime_error&) {
        // Clipboard couldn't be opened - report nothing available rather
        // than throwing, matching every other query method's contract.
    }
    return result;
}

bool ClipboardManager::setMimeData(const std::wstring& mimeType, const std::vector<std::uint8_t>& data, View* owner) {
    UINT format = getOrRegisterFormat(mimeType);
    if (format == 0) {
        return false;
    }
    return setCustomData(format, mimeType == L"text/html" ? detail::wrapCfHtml(data) : data, owner);
}

bool ClipboardManager::getMimeData(const std::wstring& mimeType, std::vector<std::uint8_t>& outData) {
    UINT format = getOrRegisterFormat(mimeType);
    if (format == 0) {
        return false;
    }

    std::vector<std::uint8_t> raw;
    if (!getCustomData(format, raw)) {
        return false;
    }
    outData = mimeType == L"text/html" ? detail::unwrapCfHtml(raw) : std::move(raw);
    return true;
}

bool ClipboardManager::setFileList(const std::vector<std::wstring>& paths, DropEffect effect, View* owner) {
    if (paths.empty()) {
        return false;
    }

    try {
        detail::ClipboardScope scope(detail::windowHandleOf(owner));
        if (!::EmptyClipboard()) {
            return false;
        }

        // DROPFILES header, then every path packed back-to-back as
        // null-terminated wchar_t strings, double-null-terminated at
        // the very end.
        std::size_t pathBytes = sizeof(wchar_t);
        for (const auto& path : paths) {
            pathBytes += (path.size() + 1) * sizeof(wchar_t);
        }

        detail::GlobalMemoryHandle dropHandle = detail::allocGlobalMemory(GHND, sizeof(DROPFILES) + pathBytes);
        {
            detail::GlobalMemoryLock lock = detail::lockGlobalMemory(dropHandle.get());
            auto* dropFiles = static_cast<DROPFILES*>(lock.get());
            dropFiles->pFiles = sizeof(DROPFILES);
            dropFiles->pt.x = 0;
            dropFiles->pt.y = 0;
            dropFiles->fNC = FALSE;
            dropFiles->fWide = TRUE;

            auto* dest = reinterpret_cast<wchar_t*>(static_cast<std::uint8_t*>(lock.get()) + sizeof(DROPFILES));
            for (const auto& path : paths) {
                std::memcpy(dest, path.c_str(), (path.size() + 1) * sizeof(wchar_t));
                dest += path.size() + 1;
            }
            *dest = L'\0';
        }

        if (!::SetClipboardData(CF_HDROP, dropHandle.get())) {
            return false;
        }
        dropHandle.release();

        DWORD effectFlags = DROPEFFECT_COPY;
        if (effect == DropEffect::Move) {
            effectFlags = DROPEFFECT_MOVE;
        } else if (effect == DropEffect::Link) {
            effectFlags = DROPEFFECT_LINK;
        }

        detail::GlobalMemoryHandle effectHandle = detail::allocGlobalMemory(GHND, sizeof(DWORD));
        {
            detail::GlobalMemoryLock lock = detail::lockGlobalMemory(effectHandle.get());
            *static_cast<DWORD*>(lock.get()) = effectFlags;
        }

        UINT dropEffectFormat = ::RegisterClipboardFormatW(L"Preferred DropEffect");
        if (!::SetClipboardData(dropEffectFormat, effectHandle.get())) {
            return false;
        }
        effectHandle.release();

        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

bool ClipboardManager::getFileList(std::vector<std::wstring>& outPaths) {
    try {
        detail::ClipboardScope scope;
        if (!::IsClipboardFormatAvailable(CF_HDROP)) {
            return false;
        }

        HANDLE data = ::GetClipboardData(CF_HDROP);
        if (!data) {
            return false;
        }

        HDROP hDrop = static_cast<HDROP>(data);
        UINT fileCount = ::DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);

        std::vector<std::wstring> paths;
        paths.reserve(fileCount);
        for (UINT i = 0; i < fileCount; ++i) {
            UINT length = ::DragQueryFileW(hDrop, i, nullptr, 0);
            std::vector<wchar_t> buffer(static_cast<std::size_t>(length) + 1);
            ::DragQueryFileW(hDrop, i, buffer.data(), length + 1);
            paths.emplace_back(buffer.data(), length);
        }

        outPaths = std::move(paths);
        return true;
    } catch (const std::runtime_error&) {
        return false;
    }
}

ClipboardManager::DropEffect ClipboardManager::getPreferredDropEffect() {
    UINT format = ::RegisterClipboardFormatW(L"Preferred DropEffect");
    if (!::IsClipboardFormatAvailable(format)) {
        return DropEffect::Copy;
    }

    try {
        detail::ClipboardScope scope;
        HANDLE data = ::GetClipboardData(format);
        if (!data) {
            return DropEffect::Copy;
        }

        detail::GlobalMemoryLock lock = detail::lockGlobalMemory(static_cast<HGLOBAL>(data));
        DWORD effect = *static_cast<const DWORD*>(lock.get());
        if (effect & DROPEFFECT_MOVE) {
            return DropEffect::Move;
        }
        if (effect & DROPEFFECT_LINK) {
            return DropEffect::Link;
        }
        if (effect & DROPEFFECT_COPY) {
            return DropEffect::Copy;
        }
        return DropEffect::None;
    } catch (const std::runtime_error&) {
        return DropEffect::Copy;
    }
}

bool ClipboardManager::setImage(const BLImage& image, View* owner) {
    if (image.size().w <= 0 || image.size().h <= 0) {
        return false;
    }

    BLImageCodec codec;
    if (codec.find_by_name("BMP") != BL_SUCCESS) {
        return false;
    }

    BLArray<std::uint8_t> bmpBytes;
    if (image.write_to_data(bmpBytes, codec) != BL_SUCCESS) {
        return false;
    }
    if (bmpBytes.size() <= sizeof(BITMAPFILEHEADER)) {
        return false;
    }

    // A BMP file is exactly a 14-byte BITMAPFILEHEADER followed by
    // CF_DIB's own real payload (BITMAPINFOHEADER + pixel data) - strip
    // the file header off.
    std::vector<std::uint8_t> dibBytes(bmpBytes.data() + sizeof(BITMAPFILEHEADER),
        bmpBytes.data() + bmpBytes.size());
    return setCustomData(CF_DIB, dibBytes, owner);
}

bool ClipboardManager::getImage(BLImage& outImage) {
    std::vector<std::uint8_t> dibBytes;
    if (!getCustomData(CF_DIB, dibBytes)) {
        return false;
    }
    if (dibBytes.size() < sizeof(BITMAPINFOHEADER)) {
        return false;
    }

    // Reverse of setImage() - reconstruct a full BMP file by
    // synthesizing the 14-byte BITMAPFILEHEADER CF_DIB doesn't carry.
    // bfOffBits has to point past the BITMAPINFOHEADER *and* any color
    // table to where the real pixel data starts (only relevant for
    // <=8bpp images - anything Blend2D itself writes is 24/32bpp, but
    // getImage() may just as well be reading a DIB some other
    // application put on the clipboard).
    const auto* infoHeader = reinterpret_cast<const BITMAPINFOHEADER*>(dibBytes.data());
    std::size_t headerAndColorsSize = infoHeader->biSize;
    if (infoHeader->biBitCount != 0 && infoHeader->biBitCount <= 8) {
        std::size_t colorsCount = (infoHeader->biClrUsed != 0)
            ? infoHeader->biClrUsed
            : (static_cast<std::size_t>(1) << infoHeader->biBitCount);
        headerAndColorsSize += colorsCount * sizeof(RGBQUAD);
    }

    BITMAPFILEHEADER fileHeader = {};
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfSize = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + dibBytes.size());
    fileHeader.bfOffBits = static_cast<DWORD>(sizeof(BITMAPFILEHEADER) + headerAndColorsSize);

    std::vector<std::uint8_t> bmpBytes(sizeof(BITMAPFILEHEADER) + dibBytes.size());
    std::memcpy(bmpBytes.data(), &fileHeader, sizeof(BITMAPFILEHEADER));
    std::memcpy(bmpBytes.data() + sizeof(BITMAPFILEHEADER), dibBytes.data(), dibBytes.size());

    return outImage.read_from_data(bmpBytes.data(), bmpBytes.size()) == BL_SUCCESS;
}

ClipboardManager& ClipboardManager::instance() {
    static ClipboardManager mgr;
    return mgr;
}

}

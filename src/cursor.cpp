#include "newui/cursor.h"
#include "newui/bundle.h"

#include <cstring>
#include <vector>

// Implementation details private to Cursor - resolveCursor()/
// createCursorFromImage()/loadCursorFromFile() used to be free functions
// declared in cursor.h; Cursor is now the only public surface for any of
// this (setCursorKind()/setPath()/setImage()/handle()), so these moved
// here, out of the header entirely, into an anonymous namespace (internal
// linkage - genuinely inaccessible outside this translation unit, not just
// "undeclared elsewhere") - same convention viewstyle.cpp already uses for
// its own single-file helpers (edge3DStyleToString()/edge3DStyleFromString()).
namespace {

    // Resolves kind to a real, showable HCURSOR - the actual ::SetCursor()
    // call happens in RootView::handleMessage()'s WM_SETCURSOR case
    // (rootview.cpp). System shapes are loaded via LoadCursor(nullptr,
    // IDC_*) - shared cursors owned by Windows, never DestroyCursor()
    // these. CursorKind::Custom returns customHandle unchanged (may be
    // null if nothing was ever actually set - treat a null result as
    // "nothing to show").
    HCURSOR resolveCursor(newui::CursorKind kind, HCURSOR customHandle) {
        switch (kind) {
            case newui::CursorKind::Arrow: return ::LoadCursorW(nullptr, IDC_ARROW);
            case newui::CursorKind::IBeam: return ::LoadCursorW(nullptr, IDC_IBEAM);
            case newui::CursorKind::Wait: return ::LoadCursorW(nullptr, IDC_WAIT);
            case newui::CursorKind::Cross: return ::LoadCursorW(nullptr, IDC_CROSS);
            case newui::CursorKind::Hand: return ::LoadCursorW(nullptr, IDC_HAND);
            case newui::CursorKind::SizeNS: return ::LoadCursorW(nullptr, IDC_SIZENS);
            case newui::CursorKind::SizeWE: return ::LoadCursorW(nullptr, IDC_SIZEWE);
            case newui::CursorKind::SizeNWSE: return ::LoadCursorW(nullptr, IDC_SIZENWSE);
            case newui::CursorKind::SizeNESW: return ::LoadCursorW(nullptr, IDC_SIZENESW);
            case newui::CursorKind::SizeAll: return ::LoadCursorW(nullptr, IDC_SIZEALL);
            case newui::CursorKind::No: return ::LoadCursorW(nullptr, IDC_NO);
            case newui::CursorKind::AppStarting: return ::LoadCursorW(nullptr, IDC_APPSTARTING);
            case newui::CursorKind::Help: return ::LoadCursorW(nullptr, IDC_HELP);
            case newui::CursorKind::Custom: return customHandle;
        }
        return ::LoadCursorW(nullptr, IDC_ARROW);
    }

    // Builds a real, alpha-blended Win32 cursor from an already-decoded
    // image - the image's own alpha channel becomes the cursor's
    // transparent/semi-transparent regions (a true per-pixel alpha
    // channel, not a binary color-keyed mask - see the CreateDIBSection +
    // all-zero AND mask below). Fails (returns nullptr) if image is
    // empty, or if either dimension exceeds maxSize (default 32,
    // Windows' traditional cursor size - this is meant for small
    // hand-authored cursor art, not arbitrary images). hotspotX/hotspotY
    // are pixel coordinates within image (default (0,0), the top-left
    // corner - the pixel Windows treats as "the actual click point";
    // pass e.g. the image's center for a crosshair-style cursor).
    //
    // Caller owns the returned HCURSOR - call ::DestroyCursor() on it
    // once nothing references it anymore. Cursor::setImage() is the only
    // caller - it takes care of that ownership for you.
    HCURSOR createCursorFromImage(const BLImage& image, int hotspotX, int hotspotY, int maxSize) {
        BLImage img = image;  // BLImage's own copy is cheap/COW - convert() below needs a non-const instance
        if (img.format() != BL_FORMAT_PRGB32 && img.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
            return nullptr;
        }

        BLImageData data;
        img.get_data(&data);
        const int width = data.size.w;
        const int height = data.size.h;
        if (width <= 0 || height <= 0 || width > maxSize || height > maxSize) {
            return nullptr;
        }
        // Every BLImage this codebase produces or decodes (RootView's own
        // buffer, ThemedViewStyle's buffered-paint bits, and - the case
        // that matters here - BLImage::read_from_file()'s built-in codecs)
        // comes out top-down/positive-stride; bail rather than silently
        // mis-rendering a bottom-up image this code was never exercised
        // against.
        if (data.stride <= 0) {
            return nullptr;
        }

        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height;  // top-down, matching data.stride's row order
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* colorBits = nullptr;
        HBITMAP hbmColor = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &colorBits, nullptr, 0);
        if (hbmColor == nullptr) {
            return nullptr;
        }

        // Premultiplied top-down BGRA (BL_FORMAT_PRGB32) is byte-for-byte
        // what a 32bpp top-down DIB section expects - same "no conversion
        // needed" fact ThemedViewStyle::paint() relies on (viewstyle.cpp).
        // Row-by-row because data.stride (blend2d's own, possibly padded)
        // and the DIB's stride (always exactly width*4 for 32bpp) aren't
        // guaranteed to match.
        const uint8_t* src = static_cast<const uint8_t*>(data.pixel_data);
        uint8_t* dst = static_cast<uint8_t*>(colorBits);
        const size_t dstStride = size_t(width) * 4;
        for (int y = 0; y < height; ++y) {
            std::memcpy(dst + size_t(y) * dstStride, src + size_t(y) * size_t(data.stride), dstStride);
        }

        // A monochrome AND mask is still required by ICONINFO, but an
        // all-zero one is the documented way to say "ignore me, use the
        // color bitmap's own per-pixel alpha instead" for a 32bpp color
        // bitmap - real semi-transparency (anti-aliased edges, etc.) only
        // survives through that alpha channel, not through a binary
        // color-keyed mask (contrast with older, pre-alpha-cursor Win32
        // code that derives a real mono mask from the color bitmap itself).
        const size_t maskStride = ((size_t(width) + 15) / 16) * 2;  // scanlines are WORD-aligned
        std::vector<uint8_t> maskBits(maskStride * size_t(height), 0);
        HBITMAP hbmMask = ::CreateBitmap(width, height, 1, 1, maskBits.data());
        if (hbmMask == nullptr) {
            ::DeleteObject(hbmColor);
            return nullptr;
        }

        ICONINFO iconInfo = {};
        iconInfo.fIcon = FALSE;  // a cursor, not an icon
        iconInfo.xHotspot = static_cast<DWORD>(hotspotX);
        iconInfo.yHotspot = static_cast<DWORD>(hotspotY);
        iconInfo.hbmMask = hbmMask;
        iconInfo.hbmColor = hbmColor;

        // CreateIconIndirect copies both bitmaps into the cursor resource
        // itself, so the originals are safe (and need) to delete right
        // after, success or failure either way.
        HICON icon = ::CreateIconIndirect(&iconInfo);

        ::DeleteObject(hbmColor);
        ::DeleteObject(hbmMask);

        return reinterpret_cast<HCURSOR>(icon);
    }

    // Convenience: BLImage::read_from_file() (decodes PNG/BMP/JPEG/QOI -
    // whatever the file actually is, blend2d builds all four codecs in
    // unconditionally - see Bundle::loadImage(), the other existing
    // caller of read_from_file()) followed by createCursorFromImage() in
    // one call. Despite the name this isn't PNG-specific - PNG (with a
    // real alpha channel) is just the intended use. If path isn't
    // directly readable, falls back to Bundle::instance().resourcePath
    // ("Cursors/" + path) - same "try the literal path, then a
    // Bundle-relative Resources/<Kind>/ one" shape as
    // FontManager::createFont() (fontmanager.cpp), so a cursor file can
    // just live under Resources/Cursors/ and be referenced by name alone.
    // Returns nullptr on any failure: not found either way, undecodable
    // format, or an oversized image (see createCursorFromImage()).
    HCURSOR loadCursorFromFile(const std::string& path, int hotspotX, int hotspotY, int maxSize) {
        BLImage image;
        if (image.read_from_file(path.c_str()) != BL_SUCCESS) {
            std::string bundlePath = newui::Bundle::instance().resourcePath("Cursors/" + path);
            if (bundlePath.empty() || image.read_from_file(bundlePath.c_str()) != BL_SUCCESS) {
                return nullptr;
            }
        }

        return createCursorFromImage(image, hotspotX, hotspotY, maxSize);
    }

}  // namespace

namespace newui {

    bool Cursor::setPath(const std::string& path, int hotspotX, int hotspotY, int maxSize) {
        if (maxSize > Cursor::MaxCursorSize) {
            return false;
        }

        HCURSOR loaded = loadCursorFromFile(path, hotspotX, hotspotY, maxSize);
        if (loaded == nullptr) {
            return false;
        }

        releaseOwnedHandle();
        kind_ = CursorKind::Custom;
        path_ = path;
        handle_ = loaded;
        return true;
    }

    bool Cursor::setImage(const BLImage& image, int hotspotX, int hotspotY, int maxSize) {
        HCURSOR loaded = createCursorFromImage(image, hotspotX, hotspotY, maxSize);
        if (loaded == nullptr) {
            return false;
        }

        releaseOwnedHandle();
        kind_ = CursorKind::Custom;
        path_.clear();
        handle_ = loaded;
        return true;
    }

    HCURSOR Cursor::handle() const {
        return resolveCursor(kind_, handle_);
    }

    void Cursor::releaseOwnedHandle() {
        if (ownsHandle()) {
            ::DestroyCursor(handle_);
        }
        handle_ = nullptr;
    }

    std::string Cursor::cursorKindToString(CursorKind kind) {
        switch (kind) {
            case CursorKind::Arrow: return "Arrow";
            case CursorKind::IBeam: return "IBeam";
            case CursorKind::Wait: return "Wait";
            case CursorKind::Cross: return "Cross";
            case CursorKind::Hand: return "Hand";
            case CursorKind::SizeNS: return "SizeNS";
            case CursorKind::SizeWE: return "SizeWE";
            case CursorKind::SizeNWSE: return "SizeNWSE";
            case CursorKind::SizeNESW: return "SizeNESW";
            case CursorKind::SizeAll: return "SizeAll";
            case CursorKind::No: return "No";
            case CursorKind::AppStarting: return "AppStarting";
            case CursorKind::Help: return "Help";
            case CursorKind::Custom: return "Custom";
        }
        return "Arrow";
    }

    CursorKind Cursor::cursorKindFromString(const std::string& s, CursorKind defaultValue) {
        if (s == "Arrow") return CursorKind::Arrow;
        if (s == "IBeam") return CursorKind::IBeam;
        if (s == "Wait") return CursorKind::Wait;
        if (s == "Cross") return CursorKind::Cross;
        if (s == "Hand") return CursorKind::Hand;
        if (s == "SizeNS") return CursorKind::SizeNS;
        if (s == "SizeWE") return CursorKind::SizeWE;
        if (s == "SizeNWSE") return CursorKind::SizeNWSE;
        if (s == "SizeNESW") return CursorKind::SizeNESW;
        if (s == "SizeAll") return CursorKind::SizeAll;
        if (s == "No") return CursorKind::No;
        if (s == "AppStarting") return CursorKind::AppStarting;
        if (s == "Help") return CursorKind::Help;
        if (s == "Custom") return CursorKind::Custom;
        return defaultValue;
    }

}

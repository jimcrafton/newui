#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <blend2d/blend2d.h>

namespace newui {

    // Defined in font.h; only used here as a return type, so a forward
    // declaration is enough and avoids a font.h <-> fontmanager.h include
    // cycle (font.h includes this header to reach FontManager::instance()).
    class Font;

    // One system font: its display name (as registered) and the file it
    // loads from. See FontManager::listFonts().
    struct SystemFontInfo {
        std::string name;
        std::string filePath;
    };

    // Which of Windows' standard UI fonts to fetch via
    // FontManager::getSystemFont() - see NONCLIENTMETRICS on MSDN, which
    // is where each of these comes from.
    enum class SystemUIFont {
        Caption,       // NONCLIENTMETRICS::lfCaptionFont - window title bars
        SmallCaption,  // lfSmCaptionFont - tool window title bars
        Menu,          // lfMenuFont - menu text
        Status,        // lfStatusFont - status bar text
        Message        // lfMessageFont - dialog/message box text; the closest
                        // thing Windows has to a general-purpose default UI font
    };

    // Enumerates and loads the system's installed TrueType/OpenType fonts,
    // and caches BLFont instances by name+size on behalf of Font. The
    // enumeration side (listFonts()/createFont()) is static - there's
    // exactly one "the system's fonts" per process, discovered lazily on
    // first use and cached afterward. The caching side (getFont()) lives on
    // the singleton returned by instance(), since it owns the BLFont
    // instances it hands out.
    class FontManager {
    public:
        // The system's installed fonts that blend2d could actually load as
        // a BLFontFace. Blend2D itself only supports TrueType/OpenType
        // (see BLFontFaceType - it has no other font face type), so a
        // successful load is proof enough that a font qualifies; anything
        // else registered on the system (Type 1, bitmap fonts, ...) fails
        // to load and is silently excluded. Computed once, on first call.
        static const std::vector<SystemFontInfo>& listFonts();

        // Loads nameOrPath as a BLFont at the given size. nameOrPath is
        // first matched case-insensitively against listFonts()'s names;
        // if that doesn't match anything, it's tried directly as a file
        // path instead. Returns false, leaving outFont untouched, if
        // neither resolves to a loadable TrueType/OpenType font.
        static bool createFont(const std::string& nameOrPath, float size, BLFont& outFont);

        // Reads which=Message (etc.) out of the current
        // SPI_GETNONCLIENTMETRICS snapshot and returns it as a Font: name
        // from LOGFONT::lfFaceName, size from the (unsigned) pixel height in
        // LOGFONT::lfHeight, and bold/italic/strikeThrough/underlined from
        // the matching LOGFONT flags. Reflects the system's current UI font
        // settings at call time - unlike listFonts()/createFont(), this
        // isn't cached, since NONCLIENTMETRICS can change at runtime (e.g.
        // a WM_SETTINGCHANGE for a theme/DPI change). Returns a
        // default-constructed Font if SystemParametersInfo() fails.
        static Font getSystemFont(SystemUIFont which = SystemUIFont::Message);

        // The process-wide FontManager singleton.
        static FontManager& instance();

        FontManager(const FontManager&) = delete;
        FontManager& operator=(const FontManager&) = delete;

        // Returns the BLFont for (name, size), keyed by "name+size" in an
        // internal cache: a repeat request for the same name+size returns
        // the same cached instance instead of loading again. The returned
        // pointer is owned by this FontManager and stays valid for the
        // rest of the process's lifetime; used by Font::blFont(). Returns
        // nullptr, without caching anything, if name can't be resolved to
        // a loadable font (see createFont()).
        BLFont* getFont(const std::string& name, float size);

    private:
        FontManager() = default;

        std::unordered_map<std::string, std::unique_ptr<BLFont>> fontCache_;
    };

}

#include "newui/fontmanager.h"
#include "newui/font.h"
#include "newui/newui.h"

#include <cstring>
#include <unordered_map>

namespace newui {

namespace {

std::string toLowerAscii(const std::string& s) {
    std::string result = s;
    for (char& c : result) {
        if (c >= 'A' && c <= 'Z') {
            c = char(c - 'A' + 'a');
        }
    }
    return result;
}

bool isAbsolutePath(const std::string& path) {
    return (path.size() >= 2 && path[1] == ':')
        || (path.size() >= 2 && path[0] == '\\' && path[1] == '\\');
}

std::string getFontsDirectory() {
    char windowsDir[MAX_PATH] = {};
    UINT len = ::GetWindowsDirectoryA(windowsDir, MAX_PATH);
    std::string dir = (len > 0 && len < MAX_PATH) ? std::string(windowsDir) : "C:\\Windows";
    return dir + "\\Fonts";
}

// Reads name/filename pairs out of the Windows font registry key under
// rootKey (HKEY_LOCAL_MACHINE for system-wide fonts, HKEY_CURRENT_USER for
// per-user ones), resolving each filename against fontsDir if it isn't
// already an absolute path, and appends the results to candidates. This
// only reads the registry - it doesn't verify the files are actually
// loadable fonts; that happens later, once, via BLFontFace.
void enumerateRegistryFonts(HKEY rootKey, const std::string& fontsDir, std::vector<SystemFontInfo>& candidates) {
    HKEY fontsKey;
    if (::RegOpenKeyExA(rootKey, "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts", 0, KEY_READ, &fontsKey) != ERROR_SUCCESS) {
        return;
    }

    DWORD index = 0;
    char valueName[512];
    BYTE valueData[MAX_PATH * 2];

    for (;;) {
        DWORD valueNameSize = sizeof(valueName);
        DWORD valueDataSize = sizeof(valueData);
        DWORD type = 0;

        LONG result = ::RegEnumValueA(fontsKey, index, valueName, &valueNameSize, nullptr, &type, valueData, &valueDataSize);
        if (result == ERROR_NO_MORE_ITEMS) {
            break;
        }
        ++index;

        if (result != ERROR_SUCCESS || type != REG_SZ || valueDataSize == 0) {
            continue;
        }

        std::string displayName(valueName, strnlen(valueName, valueNameSize));
        std::string fileName(reinterpret_cast<char*>(valueData), strnlen(reinterpret_cast<char*>(valueData), valueDataSize));
        if (displayName.empty() || fileName.empty()) {
            continue;
        }

        // Registry display names look like "Segoe UI (TrueType)" or
        // "Cambria & Cambria Math (TrueType)" - strip the trailing
        // " (...)" to get the proper name.
        size_t parenPos = displayName.rfind(" (");
        std::string properName = (parenPos != std::string::npos) ? displayName.substr(0, parenPos) : displayName;

        std::string fullPath = isAbsolutePath(fileName) ? fileName : (fontsDir + "\\" + fileName);

        candidates.push_back(SystemFontInfo{properName, fullPath});
    }

    ::RegCloseKey(fontsKey);
}

struct FontIndex {
    std::vector<SystemFontInfo> fonts;
    std::unordered_map<std::string, std::string> pathByLowerName;
};

const FontIndex& fontIndex() {
    static const FontIndex index = [] {
        FontIndex idx;

        std::string fontsDir = getFontsDirectory();

        std::vector<SystemFontInfo> candidates;
        enumerateRegistryFonts(HKEY_LOCAL_MACHINE, fontsDir, candidates);
        enumerateRegistryFonts(HKEY_CURRENT_USER, fontsDir, candidates);

        for (const SystemFontInfo& candidate : candidates) {
            // The actual TrueType/OpenType filter: only keep entries
            // blend2d can load. See FontManager::listFonts()'s doc comment.
            BLFontFace face;
            if (face.create_from_file(candidate.filePath.c_str()) != BL_SUCCESS) {
                continue;
            }

            idx.pathByLowerName.emplace(toLowerAscii(candidate.name), candidate.filePath);
            idx.fonts.push_back(candidate);
        }

        return idx;
    }();

    return index;
}

}  // namespace

const std::vector<SystemFontInfo>& FontManager::listFonts() {
    return fontIndex().fonts;
}

bool FontManager::createFont(const std::string& nameOrPath, float size, BLFont& outFont) {
    const FontIndex& idx = fontIndex();

    auto it = idx.pathByLowerName.find(toLowerAscii(nameOrPath));
    const std::string& path = (it != idx.pathByLowerName.end()) ? it->second : nameOrPath;

    BLFontFace face;
    if (face.create_from_file(path.c_str()) != BL_SUCCESS) {
        return false;
    }

    BLFont font;
    if (font.create_from_face(face, size) != BL_SUCCESS) {
        return false;
    }

    outFont = font;
    return true;
}

Font FontManager::getSystemFont(SystemUIFont which) {
    NONCLIENTMETRICSA ncm = {};
    ncm.cbSize = sizeof(ncm);
    if (!::SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return Font();
    }

    const LOGFONTA* logFont;
    switch (which) {
        case SystemUIFont::Caption:      logFont = &ncm.lfCaptionFont; break;
        case SystemUIFont::SmallCaption: logFont = &ncm.lfSmCaptionFont; break;
        case SystemUIFont::Menu:         logFont = &ncm.lfMenuFont; break;
        case SystemUIFont::Status:       logFont = &ncm.lfStatusFont; break;
        case SystemUIFont::Message:      default: logFont = &ncm.lfMessageFont; break;
    }

    // lfHeight is negative for a character height (the common case) or
    // positive for a cell height including internal leading; either way,
    // its magnitude is the pixel size blend2d's BLFont::create_from_face()
    // expects.
    float size = float(logFont->lfHeight < 0 ? -logFont->lfHeight : logFont->lfHeight);

    Font font(logFont->lfFaceName, size);
    font.setBold(logFont->lfWeight >= FW_BOLD);
    font.setItalic(logFont->lfItalic != 0);
    font.setStrikeThrough(logFont->lfStrikeOut != 0);
    font.setUnderlined(logFont->lfUnderline != 0);
    return font;
}

FontManager& FontManager::instance() {
    static FontManager fontManager;
    return fontManager;
}

BLFont* FontManager::getFont(const std::string& name, float size) {
    std::string key = name + std::to_string(size);

    auto it = fontCache_.find(key);
    if (it != fontCache_.end()) {
        return it->second.get();
    }

    auto font = std::make_unique<BLFont>();
    if (!createFont(name, size, *font)) {
        return nullptr;
    }

    BLFont* result = font.get();
    fontCache_.emplace(std::move(key), std::move(font));
    return result;
}

}

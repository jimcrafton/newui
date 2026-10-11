#include "newui/fontmanager.h"
#include "newui/font.h"
#include "newui/newui.h"
#include "newui/bundle.h"

#include <algorithm>
#include <cstring>
#include <filesystem>
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

// Where a name's font is: installed in Windows, registered by the application, or both. A registered one is used.
struct FontEntry {
    std::string systemPath;
    std::string bundledPath;

    const std::string& path() const { return bundledPath.empty() ? systemPath : bundledPath; }
};

struct FontIndex {
    std::vector<SystemFontInfo> fonts;
    std::unordered_map<std::string, FontEntry> byLowerName;
};

// Fonts are read memory-mapped: only the tables blend2d touches are paged in, so scanning hundreds of files is cheap.
constexpr BLFileReadFlags kReadFlags = BL_FILE_READ_MMAP_ENABLED;

std::vector<std::string>& registeredFiles() {
    static std::vector<std::string> files;
    return files;
}

unsigned& registeredVersion() {
    static unsigned version = 0;
    return version;
}

FontIndex& fontIndex() {
    static FontIndex index = [] {
        FontIndex idx;

        std::string fontsDir = getFontsDirectory();

        std::vector<SystemFontInfo> candidates;
        enumerateRegistryFonts(HKEY_LOCAL_MACHINE, fontsDir, candidates);
        enumerateRegistryFonts(HKEY_CURRENT_USER, fontsDir, candidates);

        for (const SystemFontInfo& candidate : candidates) {
            // The actual TrueType/OpenType filter: only keep entries
            // blend2d can load. See FontManager::listFonts()'s doc comment.
            BLFontFace face;
            if (face.create_from_file(candidate.filePath.c_str(), kReadFlags) != BL_SUCCESS) {
                continue;
            }

            idx.byLowerName.emplace(toLowerAscii(candidate.name), FontEntry{ candidate.filePath, std::string() });
            idx.fonts.push_back(candidate);
        }

        return idx;
    }();

    return index;
}

// The entry for a family name - also as "<name> Regular", which is how Windows registers some
// families (Cascadia Mono) whose name DirectWrite knows without it. Null if there is none.
const FontEntry* findFont(const std::string& name) {
    const FontIndex& idx = fontIndex();
    auto it = idx.byLowerName.find(toLowerAscii(name));
    if (it == idx.byLowerName.end()) {
        it = idx.byLowerName.find(toLowerAscii(name + " Regular"));
    }
    return it != idx.byLowerName.end() ? &it->second : nullptr;
}

bool hasFontExtension(const std::filesystem::path& path) {
    std::string ext = toLowerAscii(path.extension().string());
    return ext == ".ttf" || ext == ".otf";
}

}  // namespace

bool FontManager::isInstalled(const std::string& name) {
    const FontEntry* entry = findFont(name);
    return entry != nullptr && !entry->systemPath.empty();
}

bool FontManager::isAvailable(const std::string& name) {
    return findFont(name) != nullptr;
}

const std::vector<std::string>& FontManager::registeredFontFiles() {
    return registeredFiles();
}

unsigned FontManager::registeredFontVersion() {
    return registeredVersion();
}

bool FontManager::addFontFile(const std::string& path) {
    FontIndex& idx = fontIndex();
    BLFontFace face;
    if (face.create_from_file(path.c_str(), kReadFlags) != BL_SUCCESS) {
        return false;
    }

    const std::string family = face.family_name().data();
    const std::string style = face.subfamily_name().data();
    std::string fullName = face.full_name().data();
    if (fullName.empty()) {
        fullName = style.empty() ? family : family + " " + style;
    }

    auto add = [&](const std::string& name) {
        if (!name.empty()) {
            idx.byLowerName[toLowerAscii(name)].bundledPath = path;
        }
    };
    add(fullName);
    if (!family.empty()) {
        if (!style.empty()) {
            add(family + " " + style);
        }
        if (style.empty() || toLowerAscii(style) == "regular") {
            add(family);
        }
    }
    idx.fonts.push_back(SystemFontInfo{ fullName, path, true });
    registeredFiles().push_back(path);
    ++registeredVersion();
    return true;
}

std::size_t FontManager::addFontDirectory(const std::string& directory) {
    std::error_code ec;
    std::size_t added = 0;
    std::filesystem::recursive_directory_iterator it(directory, std::filesystem::directory_options::skip_permission_denied, ec);
    for (; !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && hasFontExtension(it->path()) && addFontFile(it->path().string())) {
            ++added;
        }
    }
    return added;
}

Font FontManager::monospaceFont(float size) {
    static const char* const kPreferred[] = { "Cascadia Mono", "Consolas", "Lucida Console", "Courier New" };
    for (const char* name : kPreferred) {
        if (isAvailable(name)) {
            return Font(name, size);
        }
    }
    return Font("Courier New", size);
}

const std::vector<SystemFontInfo>& FontManager::listFonts() {
    return fontIndex().fonts;
}

bool FontManager::createFont(const std::string& nameOrPath, float size, BLFont& outFont) {
    const FontEntry* known = findFont(nameOrPath);
    const std::string& path = known != nullptr ? known->path() : nameOrPath;

    BLFontFace face;
    if (face.create_from_file(path.c_str(), kReadFlags) != BL_SUCCESS) {
        // Neither a known system font name nor a directly-loadable path -
        // last resort, try it as a Bundle-relative resource
        // (Resources/Fonts/<nameOrPath>).
        std::string bundlePath = Bundle::instance().resourcePath("Fonts/" + nameOrPath);
        if (bundlePath.empty() || face.create_from_file(bundlePath.c_str(), kReadFlags) != BL_SUCCESS) {
            return false;
        }
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

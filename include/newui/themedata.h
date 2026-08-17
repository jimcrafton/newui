#pragma once

#include <newui/color.h>
#include <newui/geometry.h>
#include <newui/uicolormanager.h>

#include <optional>
#include <string>
#include <unordered_map>

namespace newui {

    // Cached data for one (theme class, part, state) triple - a subset
    // of what ThemeData's own class comment describes. Every field is
    // optional since a real .theme file only ever records what
    // themesgen.py actually managed to query successfully for that
    // triple (matches queryThemeColor()'s own existing "try it, fall
    // back if not" contract, uicolormanager.cpp) - a missing field here
    // just means the caller falls back to its own live uxtheme query for
    // that one piece, not the whole triple.
    struct ThemePartData {
        std::optional<Size> size;

        // GetThemeBackgroundContentRect()'s own deflation, as four
        // independent insets - not a Rect (position+size), which isn't
        // the right shape for this; matches the four-float signature
        // Rect::deflate(left,top,right,bottom) already takes.
        std::optional<float> contentLeft;
        std::optional<float> contentTop;
        std::optional<float> contentRight;
        std::optional<float> contentBottom;

        std::optional<Color> fillColor;
        std::optional<Color> edgeFillColor;
        std::optional<Color> borderColor;
        std::optional<Color> textColor;
    };

    // Pre-generated theme data (colors + control part sizes/content-rect
    // insets, across every interaction state - Normal/Hot/Pressed/
    // Disabled/Checked/... - not just the neutral one this toolkit's own
    // live uxtheme queries limit themselves to) loaded from a `.theme`
    // file (see tools/themesgen/themesgen.py, which generates one from a
    // real Windows install's actual active visual style) - consulted by
    // UIColorManager::colorFor() and ThemedViewStyle::partSize()/
    // computeClientBounds() *before* their own existing live uxtheme
    // queries, falling back to those unchanged whenever this has no
    // matching entry (including, always, when nothing has been loaded at
    // all - isLoaded() is false, roles_/parts_ stay empty, every call
    // site's behavior is then byte-identical to before ThemeData existed).
    //
    // Meyer's singleton, same style as Bundle (bundle.h's own comment on
    // why that's the right shape here, not Application's) - no implicit/
    // lazy loading of its own; per explicit design, a caller (
    // Application::run() once at startup, Frame's WM_SETTINGCHANGE(
    // "ImmersiveColorSet") handler on every live Light/Dark toggle) has
    // to actually call load()/reload()/reloadForCurrentMode() before
    // isLoaded() ever becomes true.
    class ThemeData {
    public:
        static ThemeData& instance();

        ThemeData(const ThemeData&) = delete;
        ThemeData& operator=(const ThemeData&) = delete;

        // The single flag every lookup below checks first - a never-
        // loaded (or failed-to-load) ThemeData costs one branch per call
        // site, not a real (always-missing) hash lookup.
        bool isLoaded() const { return loaded_; }

        // Loads relativePath (resolved the same way every other app
        // resource is - Bundle::instance().loadTextFile(), so relative
        // to Resources/) as a .theme (JSON5) file, replacing whatever
        // was previously loaded. Leaves existing data untouched and
        // returns false if the path doesn't resolve or fails to parse -
        // a bad reload doesn't blow away a previously-good one.
        // Remembers relativePath (only on success) for reload().
        bool load(const std::string& relativePath);

        // Re-load()s whatever path the last successful load() used.
        // Returns false (no-op) if nothing has ever loaded successfully.
        bool reload();

        // Loads "Themes/dark.theme" or "Themes/light.theme" depending on
        // UIColorManager::isDarkMode() right now - see Frame's
        // WM_SETTINGCHANGE("ImmersiveColorSet") handling (calls this on
        // every live Light/Dark toggle) and Application::run() (calls it
        // once at startup). Missing files are not an error here either -
        // an app that never ran themesgen just keeps isLoaded() false
        // and every consumer keeps using its existing live-query path.
        bool reloadForCurrentMode();

        // Discards whatever's loaded - isLoaded() goes back to false and
        // every lookup falls straight through to its own live-query path
        // again, same as before anything was ever loaded (also what a
        // headless test needing a clean slate calls between cases,
        // since this is a process-lifetime singleton - see test_themedata.cpp).
        void unload();

        // UIColorManager::colorFor()'s own fast path - returns false
        // (outColor untouched) if !isLoaded() or role isn't present in
        // the loaded file (HighlightBackground/HighlightText never are -
        // see themesgen.py's own doc comment for why).
        bool tryColorFor(UIColorRole role, Color& outColor) const;

        // ThemedViewStyle::partSize()/computeClientBounds()'s own fast
        // path - themeClassName/partId/stateId are exactly what those
        // already have in hand (themeClassName_/partId()/stateId(...)).
        // Returns nullptr if !isLoaded() or this exact triple isn't
        // present in the loaded file.
        const ThemePartData* tryPartData(const std::wstring& themeClassName, int partId, int stateId) const;

    private:
        ThemeData() = default;

        bool loaded_ = false;
        std::string lastLoadedPath_;
        std::unordered_map<UIColorRole, Color> roles_;
        std::unordered_map<std::string, ThemePartData> parts_;
    };

}

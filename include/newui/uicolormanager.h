#pragma once

#include <newui/newui.h>
#include <newui/color.h>

namespace newui {

    // Semantic color roles this toolkit's own app-drawn UI (panel
    // backgrounds/borders, text, selection highlights, ...) can ask for
    // instead of hardcoding a literal color - see
    // UIColorManager::colorFor(). Deliberately separate from SystemColor
    // (color_constants.h)/Color::fromSystemColor(): GetSysColor() values
    // are the legacy Windows 9x/2000-era "color scheme" concept and don't
    // track the modern Light/Dark mode setting at all (confirmed live -
    // toggling it leaves GetSysColor() unchanged) - UIColorManager exists
    // specifically to fill that gap with hand-picked light/dark palettes.
    enum class UIColorRole {
        WindowBackground,
        WindowText,
        ControlBackground,
        ControlText,
        ControlBorder,
        DisabledText,
        HighlightBackground,
        HighlightText,

        // A hyperlink-style Control's (Label::setHotLink(true), controls.h)
        // text color - LinkText at rest, LinkHoverText while hovered.
        // LinkText is the real system hyperlink color (COLOR_HOTLIGHT,
        // queried via GetThemeSysColor() - what Explorer/native dialogs
        // use for a link); LinkHoverText is that same value lightened,
        // since there's no separate system "hover" slot to query -
        // automatically dark-mode-adapted the same way every other role
        // is (see colorFor()'s own comment).
        LinkText,
        LinkHoverText,
    };

    // Tracks Windows' system-wide Light/Dark mode setting and hands back
    // a UIColorRole's color for whichever one is currently active - the
    // "give app-drawn UI a theme-aware palette" half of dark-mode
    // support. See ThemedViewStyle::paint() (viewstyle.cpp) for the other
    // half: approximating a dark look for *native* uxtheme-rendered
    // controls, which have no real dark visual data to switch to in the
    // first place, so a semantic color role can't help them - that part
    // still needs the pixel-level fake.
    //
    // Meyer's singleton, like Bundle/SerializationRegistry - no Win32
    // registration/uniqueness constraint to guard, unlike Application.
    class UIColorManager {
    public:
        // Meyer's singleton (see the class comment above) with no real
        // per-instance state of its own - isDarkMode()/colorFor() below
        // are static for that reason, so a caller can reach either
        // directly (UIColorManager::isDarkMode()) without going through
        // instance() first; instance() itself is kept only for source
        // compatibility with existing UIColorManager::instance().foo()
        // call sites.
        static UIColorManager& instance();

        UIColorManager(const UIColorManager&) = delete;
        UIColorManager& operator=(const UIColorManager&) = delete;

        // Reads HKCU's AppsUseLightTheme fresh on every call rather than
        // caching - a single RegGetValueW() is cheap enough that there's
        // no real cost to always being current, and it sidesteps needing
        // to invalidate a cache in step with Frame's own
        // WM_SETTINGCHANGE("ImmersiveColorSet")/WM_THEMECHANGED handling
        // (see RootView::refreshThemes()) rather than possibly drifting
        // out of sync with it. Undocumented value (no official API
        // reports it), but extremely widely relied upon - every major
        // browser/Electron app reads this same key. 0 = dark; missing
        // key/value, or 1, = light - matches Microsoft's own default for
        // a system that's never had the setting touched.
        static bool isDarkMode();

        // HighlightBackground/HighlightText follow the user's actual
        // Windows accent color (DwmGetColorizationColor(), a real,
        // documented Win32 API - no WinRT needed) rather than a fixed
        // blue, with HighlightText picked black/white by whichever gives
        // better contrast against it (see Color::luminosity()). Falls
        // back to a fixed Windows-blue accent if DWM isn't available.
        // Every other role is a fixed hand-picked light/dark pair.
        static Color colorFor(UIColorRole role);

    private:
        UIColorManager() = default;
    };

    // ------------------------------------------------------------------
    // Real native Windows dark chrome opt-in - distinct from
    // UIColorManager/ThemedViewStyle's own "fake" dark mode above:
    // classic uxtheme parts (BUTTON/EDIT/TRACKBAR/...) have no real dark
    // asset at all, so ThemedViewStyle::paint() approximates one by
    // inverting pixels it already drew. A native TrackPopupMenuEx()
    // popup (ContextMenu, menus.h) isn't drawn by this toolkit at all -
    // it's the OS's own menu window - so that trick doesn't reach it;
    // Windows itself can render it with real dark chrome, but only for a
    // process that has explicitly opted in via these undocumented
    // uxtheme.dll ordinal exports (no header/import lib declares them -
    // resolved by ordinal number via GetProcAddress, the same approach
    // every other app supporting native Windows dark mode has to use -
    // Windows Terminal, Chromium, the widely-cited ysc3839/win32-darkmode
    // reference project). Ordinals are stable across Windows 10 1809+
    // builds but unsupported/unguaranteed by Microsoft; both functions
    // below degrade silently (do nothing) if resolving fails, same
    // "degrade, don't throw" convention Application::run()'s DPI-
    // awareness call already uses for a similarly OS-version-sensitive
    // feature.
    // ------------------------------------------------------------------

    // Opts this whole process into following the system Light/Dark mode
    // setting for native chrome wherever Windows itself supports it -
    // call once, early (see Application::run()). Doesn't force Dark
    // mode - a process that calls this on a system still set to Light
    // mode looks unaffected; it just stops *blocking* Windows from going
    // dark on its own native surfaces (a native TrackPopupMenuEx() popup
    // chief among them) when the system is in Dark mode.
    void enableProcessDarkModeSupport();

    // Opts a single window into real native dark chrome -
    // AllowDarkModeForWindow (ordinal) plus SetWindowTheme(hwnd,
    // L"DarkMode_Explorer", nullptr) (that part IS documented, declared
    // in <uxtheme.h>). enableProcessDarkModeSupport() must already have
    // run once in this process for this to have any visible effect. Call
    // on a popup menu's owner window before TrackPopupMenuEx() - see
    // ContextMenu::show() (menus.cpp). Idempotent and cheap to call on
    // every show() - actual work only happens the first time a given
    // HWND is seen (SetWindowTheme() sends that window a real
    // WM_THEMECHANGED every time it runs, so redoing it on every call
    // would re-trigger Frame's own WM_THEMECHANGED handling, including
    // Application::onThemeChanged(), on every single call - see its own
    // doc comment in uicolormanager.cpp for the full story).
    void enableDarkModeForWindow(HWND hwnd);

    // Un-sticks native TrackPopupMenuEx() popups (ContextMenu, menus.h)
    // from whichever Light/Dark mode was active the first time this
    // process ever showed one. uxtheme.dll caches its own "is the system
    // in dark mode" policy flag plus a set of pre-built menu theme
    // handles internally, separate from (and not invalidated by) either
    // AllowDarkModeForWindow/SetWindowTheme above or this toolkit's own
    // RootView::refreshThemes() - so without this, every app-drawn/
    // uxtheme-part control updates live on a theme toggle but a native
    // popup menu keeps rendering stale until the process restarts.
    // RefreshImmersiveColorPolicyState (ordinal 104) refreshes that
    // cached policy flag; FlushMenuThemes (ordinal 136) forces cached
    // menu theme handles to be rebuilt from it. Call on
    // WM_SETTINGCHANGE("ImmersiveColorSet") - see Frame::handleMessage().
    // Same "degrade silently if resolving fails" convention as the two
    // functions above.
    void refreshNativeMenuDarkModePolicy();

}

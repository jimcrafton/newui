#include "newui/uicolormanager.h"

#include <dwmapi.h>
#include <uxtheme.h>
#include <vssym32.h>

namespace {

	// Undocumented uxtheme.dll ordinal exports - see enableProcessDarkModeSupport()/
	// enableDarkModeForWindow()'s own doc comments in uicolormanager.h for why
	// these can't just be declared normally and #included.
	enum class PreferredAppMode { Default, AllowDark, ForceDark, ForceLight, Max };
	using SetPreferredAppModeFn = PreferredAppMode(WINAPI*)(PreferredAppMode);
	using AllowDarkModeForWindowFn = BOOL(WINAPI*)(HWND, BOOL);

	// Resolved once, lazily - loaded (never freed; this DLL is already
	// permanently mapped into every themed process via comctl32/uxtheme's
	// own normal use elsewhere in this codebase, e.g. OpenThemeData()) on
	// first use rather than at static-init time, so a build/run on a
	// Windows version old enough to be missing uxtheme.dll entirely
	// still starts up fine - callers only find out (and degrade
	// silently) the first time they actually try to use it.
	HMODULE uxthemeModule() {
		static HMODULE mod = ::LoadLibraryW(L"uxtheme.dll");
		return mod;
	}

	// Queries propId from themeClassName's partId/stateId via a
	// throwaway HTHEME (OpenThemeData(NULL, ...) works fine without a
	// live HWND - it just can't be per-monitor-DPI-aware, which this
	// doesn't need). Returns false (outColor untouched) if theming is
	// unavailable, or the current visual style simply doesn't define
	// that property for that part/state - not every combination is
	// populated by every style, so this is a "try it, fall back if not"
	// helper, not a guarantee.
	bool queryThemeColor(const wchar_t* themeClassName, int partId, int stateId, int propId, newui::Color& outColor) {
		HTHEME theme = ::OpenThemeData(nullptr, themeClassName);
		if (theme == nullptr) {
			return false;
		}

		COLORREF ref = 0;
		bool ok = SUCCEEDED(::GetThemeColor(theme, partId, stateId, propId, &ref));
		if (ok) {
			outColor = newui::Color(
				float(GetRValue(ref)) / 255.0f,
				float(GetGValue(ref)) / 255.0f,
				float(GetBValue(ref)) / 255.0f,
				1.0f);
		}

		::CloseThemeData(theme);
		return ok;
	}

	// Distinct from queryThemeColor() above: GetThemeSysColor() reads a
	// theme's own [SysMetrics]-section rendition of a classic winuser.h
	// COLOR_* system color slot (falling back to the plain global system
	// color if hTheme is null) rather than one specific part/state's own
	// fill/text/edge property. Used for WindowBackground/ControlBackground
	// below after TAB/TABP_PANE's own TMT_FILLCOLOR turned out unreliable
	// in practice - verified live (color picker against a running
	// instance) to come back a plain medium blue (#4c85c7) rather than a
	// neutral panel gray on at least one real Windows 11 install/visual
	// style, i.e. TABP_PANE's fill apparently isn't the plain neutral
	// background this role is meant to represent everywhere - some tab
	// control skins tint it toward the accent color instead.
	// GetThemeSysColor() always succeeds (COLORREF, not a BOOL/HRESULT) -
	// no "unpopulated property" failure mode to report the way
	// queryThemeColor() has, so no bool return here.
	newui::Color queryThemeSysColor(const wchar_t* themeClassName, int sysColorId) {
		HTHEME theme = ::OpenThemeData(nullptr, themeClassName);
		COLORREF ref = ::GetThemeSysColor(theme, sysColorId);
		if (theme != nullptr) {
			::CloseThemeData(theme);
		}

		return newui::Color(
			float(GetRValue(ref)) / 255.0f,
			float(GetGValue(ref)) / 255.0f,
			float(GetBValue(ref)) / 255.0f,
			1.0f);
	}

	// Inverts HSL lightness, hue/alpha untouched - the same "smart
	// invert" heuristic ThemedViewStyle::paint() applies to a whole
	// native-control pixel buffer (see its own doc comment in
	// viewstyle.cpp), applied here to a single semantic color instead.
	// Used to turn whatever queryThemeColor()/a fixed fallback returns
	// (always the current, non-dark-aware visual style's actual color -
	// see UIColorManager::colorFor()'s own doc comment) into an
	// approximate dark equivalent.
	newui::Color invertLightness(const newui::Color& color) {
		newui::HSLColor hsl = color.toHSL();
		hsl.l = 1.0f - hsl.l;
		return newui::Color::fromHSL(hsl);
	}

}

namespace newui {

	UIColorManager& UIColorManager::instance() {
		static UIColorManager instance;
		return instance;
	}

	bool UIColorManager::isDarkMode() const {
		DWORD value = 1;
		DWORD size = sizeof(value);
		LONG status = ::RegGetValueW(
			HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
			L"AppsUseLightTheme",
			RRF_RT_REG_DWORD,
			nullptr,
			&value,
			&size);
		return status == ERROR_SUCCESS && value == 0;
	}

	Color UIColorManager::colorFor(UIColorRole role) const {
		if (role == UIColorRole::HighlightBackground || role == UIColorRole::HighlightText) {
			DWORD colorizationColor = 0;
			BOOL opaqueBlend = FALSE;
			Color accent(0x0067C0u, false);  // fixed Windows-blue fallback if DWM isn't available
			if (SUCCEEDED(::DwmGetColorizationColor(&colorizationColor, &opaqueBlend))) {
				accent = Color(static_cast<std::uint32_t>(colorizationColor), true);
			}
			if (role == UIColorRole::HighlightBackground) {
				return accent;
			}
			// Light text on the accent color if it's dark enough to need
			// it, dark text otherwise.
			return accent.luminosity() < 0.5f ? Color(0xFFFFFFu, false) : Color(0x000000u, false);
		}

		// Every other role: query the current (non-dark-aware) visual
		// style's real color via GetThemeColor where there's a reliably
		// populated property to ask for, falling back to a fixed light
		// value otherwise - then invert lightness if the system is in
		// Dark mode. GetThemeColor alone can't distinguish light/dark
		// (the classic theme classes below don't carry separate dark
		// variants - confirmed live: toggling the system setting doesn't
		// change what they return), so the invert step is what actually
		// makes this respond to it; querying still means the light-mode
		// starting point (and therefore the derived dark one) reflects
		// the real current visual style instead of a hand-guessed value.
		Color base;
		switch (role) {
			case UIColorRole::WindowBackground:
			case UIColorRole::ControlBackground:
				// COLOR_WINDOW via the current theme's own [SysMetrics]
				// rendition (queryThemeSysColor()) - see its own doc
				// comment for why this replaced an earlier TAB/TABP_PANE/
				// TMT_FILLCOLOR query that turned out to return an
				// accent-tinted blue instead of a neutral panel color.
				base = queryThemeSysColor(L"WINDOW", COLOR_WINDOW);
				break;

			case UIColorRole::WindowText:
			case UIColorRole::ControlText:
				// BUTTON/BP_PUSHBUTTON's label text color - needed to
				// render a button's own caption, so reliably populated.
				if (!queryThemeColor(L"BUTTON", BP_PUSHBUTTON, PBS_NORMAL, TMT_TEXTCOLOR, base)) {
					base = Color(0x1B1B1Bu, false);
				}
				break;

			case UIColorRole::ControlBorder:
				if (!queryThemeColor(L"BUTTON", BP_PUSHBUTTON, PBS_NORMAL, TMT_EDGEFILLCOLOR, base)) {
					base = Color(0xC6C6C6u, false);
				}
				break;

			case UIColorRole::DisabledText:
				if (!queryThemeColor(L"BUTTON", BP_PUSHBUTTON, PBS_DISABLED, TMT_TEXTCOLOR, base)) {
					base = Color(0x8C8C8Cu, false);
				}
				break;

			default:
				base = Color(0x1B1B1Bu, false);
				break;
		}

		return isDarkMode() ? invertLightness(base) : base;
	}

	void enableProcessDarkModeSupport() {
		HMODULE mod = uxthemeModule();
		if (mod == nullptr) {
			return;
		}

		auto setPreferredAppMode = reinterpret_cast<SetPreferredAppModeFn>(
			::GetProcAddress(mod, MAKEINTRESOURCEA(135)));
		if (setPreferredAppMode != nullptr) {
			setPreferredAppMode(PreferredAppMode::AllowDark);
		}
	}

	void enableDarkModeForWindow(HWND hwnd) {
		if (hwnd == nullptr) {
			return;
		}

		HMODULE mod = uxthemeModule();
		if (mod != nullptr) {
			auto allowDarkModeForWindow = reinterpret_cast<AllowDarkModeForWindowFn>(
				::GetProcAddress(mod, MAKEINTRESOURCEA(133)));
			if (allowDarkModeForWindow != nullptr) {
				allowDarkModeForWindow(hwnd, TRUE);
			}
		}

		::SetWindowTheme(hwnd, L"DarkMode_Explorer", nullptr);
	}

}

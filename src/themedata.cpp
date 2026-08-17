#include "newui/themedata.h"
#include "newui/bundle.h"
#include "newui/viewstyle.h"  // real BP_*/PBS_*/etc. symbols the tables below use

#include <json5/json5.hpp>
#include <json5/json5_input.hpp>

#include <cstdlib>
#include <optional>

namespace {

	using newui::UIColorRole;

	// Real win32 theme-part/state symbolic names -> their real integer
	// values, straight from <vssym32.h> via the same #include chain
	// viewstyle.h already established (see its own "NOMINMAX hazard"
	// comment) - not hand-copied literals, so these can never silently
	// drift out of sync with the real SDK headers the way a copy-pasted
	// number could. One entry per symbol any ThemedXxxStyle subclass in
	// viewstyle.h actually constructs via its own partId()/stateId() -
	// tools/themesgen/themesgen.py keeps its own copy of this same
	// registry in sync by hand (Python can't #include a C header), with
	// the full part-by-part reasoning in its own comments.
	const std::unordered_map<std::string, int>& partNameTable() {
		static const std::unordered_map<std::string, int> table = {
			{"BP_PUSHBUTTON", BP_PUSHBUTTON},
			{"BP_CHECKBOX", BP_CHECKBOX},
			{"BP_RADIOBUTTON", BP_RADIOBUTTON},
			{"BP_GROUPBOX", BP_GROUPBOX},
			{"TP_BUTTON", TP_BUTTON},
			{"TP_DROPDOWNBUTTON", TP_DROPDOWNBUTTON},
			{"TP_DROPDOWNBUTTONGLYPH", TP_DROPDOWNBUTTONGLYPH},
			{"TP_SPLITBUTTON", TP_SPLITBUTTON},
			{"TP_SPLITBUTTONDROPDOWN", TP_SPLITBUTTONDROPDOWN},
			{"TP_SEPARATOR", TP_SEPARATOR},
			{"TP_SEPARATORVERT", TP_SEPARATORVERT},
			{"SP_PANE", SP_PANE},
			{"RP_BAND", RP_BAND},
			{"RP_CHEVRON", RP_CHEVRON},
			{"RP_CHEVRONVERT", RP_CHEVRONVERT},
			{"TTP_STANDARD", TTP_STANDARD},
			{"SPNP_UP", SPNP_UP},
			{"SPNP_DOWN", SPNP_DOWN},
			{"EP_EDITTEXT", EP_EDITTEXT},
			{"LVP_LISTITEM", LVP_LISTITEM},
			{"HP_HEADERITEM", HP_HEADERITEM},
			{"HP_HEADERSORTARROW", HP_HEADERSORTARROW},
			{"TVP_TREEITEM", TVP_TREEITEM},
			{"TVP_GLYPH", TVP_GLYPH},
			{"TABP_TOPTABITEM", TABP_TOPTABITEM},
			{"TABP_TOPTABITEMLEFTEDGE", TABP_TOPTABITEMLEFTEDGE},
			{"TABP_TOPTABITEMRIGHTEDGE", TABP_TOPTABITEMRIGHTEDGE},
			{"TABP_TOPTABITEMBOTHEDGE", TABP_TOPTABITEMBOTHEDGE},
			{"TABP_TABITEM", TABP_TABITEM},
			{"TABP_TABITEMLEFTEDGE", TABP_TABITEMLEFTEDGE},
			{"TABP_TABITEMRIGHTEDGE", TABP_TABITEMRIGHTEDGE},
			{"TABP_TABITEMBOTHEDGE", TABP_TABITEMBOTHEDGE},
			{"TABP_PANE", TABP_PANE},
			{"TKP_TRACK", TKP_TRACK},
			{"TKP_TRACKVERT", TKP_TRACKVERT},
			{"TKP_THUMB", TKP_THUMB},
			{"TKP_THUMBVERT", TKP_THUMBVERT},
			{"TKP_TICS", TKP_TICS},
			{"TKP_TICSVERT", TKP_TICSVERT},
			{"PP_BAR", PP_BAR},
			{"PP_BARVERT", PP_BARVERT},
			{"PP_FILL", PP_FILL},
			{"PP_FILLVERT", PP_FILLVERT},
			{"SBP_THUMBBTNHORZ", SBP_THUMBBTNHORZ},
			{"SBP_THUMBBTNVERT", SBP_THUMBBTNVERT},
			{"SBP_ARROWBTN", SBP_ARROWBTN},
			{"SBP_LOWERTRACKHORZ", SBP_LOWERTRACKHORZ},
			{"SBP_UPPERTRACKHORZ", SBP_UPPERTRACKHORZ},
			{"SBP_LOWERTRACKVERT", SBP_LOWERTRACKVERT},
			{"SBP_UPPERTRACKVERT", SBP_UPPERTRACKVERT},
			{"MENU_BARITEM", MENU_BARITEM},
			{"MENU_BARBACKGROUND", MENU_BARBACKGROUND},
		};
		return table;
	}

	const std::unordered_map<std::string, int>& stateNameTable() {
		static const std::unordered_map<std::string, int> table = {
			{"PBS_NORMAL", PBS_NORMAL}, {"PBS_HOT", PBS_HOT}, {"PBS_PRESSED", PBS_PRESSED}, {"PBS_DISABLED", PBS_DISABLED},
			{"CBS_UNCHECKEDNORMAL", CBS_UNCHECKEDNORMAL}, {"CBS_UNCHECKEDHOT", CBS_UNCHECKEDHOT}, {"CBS_UNCHECKEDPRESSED", CBS_UNCHECKEDPRESSED}, {"CBS_UNCHECKEDDISABLED", CBS_UNCHECKEDDISABLED},
			{"CBS_CHECKEDNORMAL", CBS_CHECKEDNORMAL}, {"CBS_CHECKEDHOT", CBS_CHECKEDHOT}, {"CBS_CHECKEDPRESSED", CBS_CHECKEDPRESSED}, {"CBS_CHECKEDDISABLED", CBS_CHECKEDDISABLED},
			{"RBS_UNCHECKEDNORMAL", RBS_UNCHECKEDNORMAL}, {"RBS_UNCHECKEDHOT", RBS_UNCHECKEDHOT}, {"RBS_UNCHECKEDPRESSED", RBS_UNCHECKEDPRESSED}, {"RBS_UNCHECKEDDISABLED", RBS_UNCHECKEDDISABLED},
			{"RBS_CHECKEDNORMAL", RBS_CHECKEDNORMAL}, {"RBS_CHECKEDHOT", RBS_CHECKEDHOT}, {"RBS_CHECKEDPRESSED", RBS_CHECKEDPRESSED}, {"RBS_CHECKEDDISABLED", RBS_CHECKEDDISABLED},
			{"GBS_NORMAL", GBS_NORMAL}, {"GBS_DISABLED", GBS_DISABLED},
			{"TS_NORMAL", TS_NORMAL}, {"TS_HOT", TS_HOT}, {"TS_PRESSED", TS_PRESSED}, {"TS_DISABLED", TS_DISABLED}, {"TS_CHECKED", TS_CHECKED}, {"TS_HOTCHECKED", TS_HOTCHECKED},
			{"CHEVS_NORMAL", CHEVS_NORMAL}, {"CHEVS_HOT", CHEVS_HOT}, {"CHEVS_PRESSED", CHEVS_PRESSED},
			{"CHEVSV_NORMAL", CHEVSV_NORMAL}, {"CHEVSV_HOT", CHEVSV_HOT}, {"CHEVSV_PRESSED", CHEVSV_PRESSED},
			{"TTSS_NORMAL", TTSS_NORMAL}, {"TTSS_LINK", TTSS_LINK},
			{"UPS_NORMAL", UPS_NORMAL}, {"UPS_HOT", UPS_HOT}, {"UPS_PRESSED", UPS_PRESSED}, {"UPS_DISABLED", UPS_DISABLED},
			{"DNS_NORMAL", DNS_NORMAL}, {"DNS_HOT", DNS_HOT}, {"DNS_PRESSED", DNS_PRESSED}, {"DNS_DISABLED", DNS_DISABLED},
			{"ETS_NORMAL", ETS_NORMAL}, {"ETS_HOT", ETS_HOT}, {"ETS_DISABLED", ETS_DISABLED}, {"ETS_FOCUSED", ETS_FOCUSED}, {"ETS_READONLY", ETS_READONLY},
			{"LISS_NORMAL", LISS_NORMAL}, {"LISS_HOT", LISS_HOT}, {"LISS_SELECTED", LISS_SELECTED}, {"LISS_DISABLED", LISS_DISABLED}, {"LISS_HOTSELECTED", LISS_HOTSELECTED},
			{"HIS_NORMAL", HIS_NORMAL}, {"HIS_HOT", HIS_HOT}, {"HIS_PRESSED", HIS_PRESSED}, {"HIS_SORTEDNORMAL", HIS_SORTEDNORMAL}, {"HIS_SORTEDHOT", HIS_SORTEDHOT}, {"HIS_SORTEDPRESSED", HIS_SORTEDPRESSED},
			{"HSAS_SORTEDUP", HSAS_SORTEDUP}, {"HSAS_SORTEDDOWN", HSAS_SORTEDDOWN},
			{"TREIS_NORMAL", TREIS_NORMAL}, {"TREIS_HOT", TREIS_HOT}, {"TREIS_SELECTED", TREIS_SELECTED}, {"TREIS_DISABLED", TREIS_DISABLED}, {"TREIS_HOTSELECTED", TREIS_HOTSELECTED},
			{"GLPS_CLOSED", GLPS_CLOSED}, {"GLPS_OPENED", GLPS_OPENED},
			{"TIS_NORMAL", TIS_NORMAL}, {"TIS_HOT", TIS_HOT}, {"TIS_SELECTED", TIS_SELECTED}, {"TIS_DISABLED", TIS_DISABLED},
			{"TRS_NORMAL", TRS_NORMAL}, {"TRVS_NORMAL", TRVS_NORMAL},
			{"TUS_NORMAL", TUS_NORMAL}, {"TUS_HOT", TUS_HOT}, {"TUS_PRESSED", TUS_PRESSED}, {"TUS_DISABLED", TUS_DISABLED},
			{"TUVS_NORMAL", TUVS_NORMAL}, {"TUVS_HOT", TUVS_HOT}, {"TUVS_PRESSED", TUVS_PRESSED}, {"TUVS_DISABLED", TUVS_DISABLED},
			{"TSS_NORMAL", TSS_NORMAL}, {"TSVS_NORMAL", TSVS_NORMAL},
			{"PBFS_NORMAL", PBFS_NORMAL}, {"PBFS_ERROR", PBFS_ERROR}, {"PBFS_PAUSED", PBFS_PAUSED},
			{"PBFVS_NORMAL", PBFVS_NORMAL}, {"PBFVS_ERROR", PBFVS_ERROR}, {"PBFVS_PAUSED", PBFVS_PAUSED},
			{"SCRBS_NORMAL", SCRBS_NORMAL}, {"SCRBS_HOT", SCRBS_HOT}, {"SCRBS_PRESSED", SCRBS_PRESSED}, {"SCRBS_DISABLED", SCRBS_DISABLED},
			{"ABS_UPNORMAL", ABS_UPNORMAL}, {"ABS_UPHOT", ABS_UPHOT}, {"ABS_UPPRESSED", ABS_UPPRESSED}, {"ABS_UPDISABLED", ABS_UPDISABLED},
			{"ABS_DOWNNORMAL", ABS_DOWNNORMAL}, {"ABS_DOWNHOT", ABS_DOWNHOT}, {"ABS_DOWNPRESSED", ABS_DOWNPRESSED}, {"ABS_DOWNDISABLED", ABS_DOWNDISABLED},
			{"ABS_LEFTNORMAL", ABS_LEFTNORMAL}, {"ABS_LEFTHOT", ABS_LEFTHOT}, {"ABS_LEFTPRESSED", ABS_LEFTPRESSED}, {"ABS_LEFTDISABLED", ABS_LEFTDISABLED},
			{"ABS_RIGHTNORMAL", ABS_RIGHTNORMAL}, {"ABS_RIGHTHOT", ABS_RIGHTHOT}, {"ABS_RIGHTPRESSED", ABS_RIGHTPRESSED}, {"ABS_RIGHTDISABLED", ABS_RIGHTDISABLED},
			{"MBI_NORMAL", MBI_NORMAL}, {"MBI_HOT", MBI_HOT}, {"MBI_PUSHED", MBI_PUSHED}, {"MBI_DISABLED", MBI_DISABLED},
			{"MB_ACTIVE", MB_ACTIVE},
			// Sentinel for the handful of parts with no real state enum
			// at all (STATUS/SP_PANE, REBAR/RP_BAND, TAB/TABP_PANE,
			// PROGRESS/PP_BAR(VERT) - see viewstyle.h's own stateId()
			// overrides for those, which just hardcode 0) - a symbolic
			// name rather than the literal string "0" deliberately: this
			// json5 implementation's own parser doesn't accept an
			// all-digit quoted object key (confirmed live - a ".theme"
			// file using "0" as a key fails to parse at all), so every
			// key in the file needs to be a real identifier regardless
			// of what it resolves to.
			{"DEFAULT", 0},
		};
		return table;
	}

	const std::unordered_map<std::string, UIColorRole>& roleNameTable() {
		static const std::unordered_map<std::string, UIColorRole> table = {
			{"WindowBackground", UIColorRole::WindowBackground},
			{"WindowText", UIColorRole::WindowText},
			{"ControlBackground", UIColorRole::ControlBackground},
			{"ControlText", UIColorRole::ControlText},
			{"ControlBorder", UIColorRole::ControlBorder},
			{"DisabledText", UIColorRole::DisabledText},
			{"LinkText", UIColorRole::LinkText},
			{"LinkHoverText", UIColorRole::LinkHoverText},
			// HighlightBackground/HighlightText deliberately absent - see
			// themesgen.py's own doc comment for why (a live DWM accent-
			// color setting, not a static per-visual-style value).
		};
		return table;
	}

	// name is first looked up in table (the normal, symbolic-name path);
	// falling back to parsing name itself as a plain integer covers both
	// the handful of parts with no real state enum at all (state "0" -
	// STATUS/REBAR's RP_BAND/PP_BAR/etc., see themesgen.py's own
	// per-part notes) and gives anyone hand-editing a .theme file a raw-
	// number escape hatch if a symbol table entry is ever missing.
	std::optional<int> resolveSymbol(const std::unordered_map<std::string, int>& table, const std::string& name) {
		auto it = table.find(name);
		if (it != table.end()) {
			return it->second;
		}

		char* end = nullptr;
		long value = std::strtol(name.c_str(), &end, 10);
		if (end != name.c_str() && *end == '\0') {
			return static_cast<int>(value);
		}
		return std::nullopt;
	}

	// Theme class names ("BUTTON", "SCROLLBAR", ...) are plain ASCII
	// identifiers - a trivial per-character narrow is exact and doesn't
	// need a real Unicode-aware conversion (WideCharToMultiByte etc.).
	std::string narrow(const std::wstring& wide) {
		std::string result;
		result.reserve(wide.size());
		for (wchar_t ch : wide) {
			result.push_back(static_cast<char>(ch));
		}
		return result;
	}

	std::string buildPartKey(const std::string& themeClassName, int partId, int stateId) {
		return themeClassName + ":" + std::to_string(partId) + ":" + std::to_string(stateId);
	}

	// Reads a "#RRGGBBAA" string value at key from obj into outColor if
	// present and valid - leaves outColor untouched otherwise, same
	// "unpopulated field just doesn't override anything" contract every
	// ThemePartData field has.
	void readOptionalColor(const json5::value& obj, const char* key, std::optional<newui::Color>& outColor) {
		newui::Color parsed;
		if (newui::Color::fromString(obj[key].get_c_str(""), parsed)) {
			outColor = parsed;
		}
	}

}

namespace newui {

	ThemeData& ThemeData::instance() {
		static ThemeData instance;
		return instance;
	}

	bool ThemeData::load(const std::string& relativePath) {
		std::string text = Bundle::instance().loadTextFile(relativePath);
		if (text.empty()) {
			return false;
		}

		json5::document doc;
		if (json5::from_string(text, doc)) {
			return false;
		}

		std::unordered_map<UIColorRole, Color> newRoles;
		for (auto [roleName, roleValue] : json5::object_view(doc["roles"])) {
			auto roleIt = roleNameTable().find(roleName);
			if (roleIt == roleNameTable().end()) {
				continue;
			}
			Color color;
			if (Color::fromString(roleValue.get_c_str(""), color)) {
				newRoles[roleIt->second] = color;
			}
		}

		std::unordered_map<std::string, ThemePartData> newParts;
		for (auto [className, classValue] : json5::object_view(doc["parts"])) {
			for (auto [partName, partValue] : json5::object_view(classValue)) {
				std::optional<int> partId = resolveSymbol(partNameTable(), partName);
				if (!partId) {
					continue;
				}

				for (auto [stateName, stateValue] : json5::object_view(partValue)) {
					std::optional<int> stateId = resolveSymbol(stateNameTable(), stateName);
					if (!stateId) {
						continue;
					}

					ThemePartData data;

					json5::value sizeVal = stateValue["size"];
					if (sizeVal.is_object()) {
						data.size = Size(sizeVal["width"].get<float>(0.0f), sizeVal["height"].get<float>(0.0f));
					}

					json5::value rectVal = stateValue["contentRect"];
					if (rectVal.is_object()) {
						data.contentLeft = rectVal["left"].get<float>(0.0f);
						data.contentTop = rectVal["top"].get<float>(0.0f);
						data.contentRight = rectVal["right"].get<float>(0.0f);
						data.contentBottom = rectVal["bottom"].get<float>(0.0f);
					}

					json5::value colorsVal = stateValue["colors"];
					if (colorsVal.is_object()) {
						readOptionalColor(colorsVal, "fillColor", data.fillColor);
						readOptionalColor(colorsVal, "edgeFillColor", data.edgeFillColor);
						readOptionalColor(colorsVal, "borderColor", data.borderColor);
						readOptionalColor(colorsVal, "textColor", data.textColor);
					}

					newParts[buildPartKey(className, *partId, *stateId)] = data;
				}
			}
		}

		roles_ = std::move(newRoles);
		parts_ = std::move(newParts);
		loaded_ = true;
		lastLoadedPath_ = relativePath;
		return true;
	}

	bool ThemeData::reload() {
		if (lastLoadedPath_.empty()) {
			return false;
		}
		// Deliberately doesn't clear lastLoadedPath_/loaded_ on failure -
		// see load()'s own "leave existing data untouched" contract.
		return load(lastLoadedPath_);
	}

	bool ThemeData::reloadForCurrentMode() {
		return load(UIColorManager::isDarkMode() ? "Themes/dark.theme" : "Themes/light.theme");
	}

	void ThemeData::unload() {
		loaded_ = false;
		lastLoadedPath_.clear();
		roles_.clear();
		parts_.clear();
	}

	bool ThemeData::tryColorFor(UIColorRole role, Color& outColor) const {
		if (!loaded_) {
			return false;
		}
		auto it = roles_.find(role);
		if (it == roles_.end()) {
			return false;
		}
		outColor = it->second;
		return true;
	}

	const ThemePartData* ThemeData::tryPartData(const std::wstring& themeClassName, int partId, int stateId) const {
		if (!loaded_) {
			return nullptr;
		}
		auto it = parts_.find(buildPartKey(narrow(themeClassName), partId, stateId));
		return it != parts_.end() ? &it->second : nullptr;
	}

}

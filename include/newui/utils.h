
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#define NOMINMAX
#include <windows.h>
#include <newui/platform.h>

#include <cstdint>
#include <string>
#include <typeinfo>

namespace newui {
	// typeid(...).name() as readable C++ text with the namespace kept:
	// "newui::Size". MSVC's name() is already readable but carries a
	// "class "/"struct "/"enum "/"union " prefix, which is stripped; on
	// Itanium-ABI compilers (Clang/GCC) name() is mangled ("N5newui4SizeE"), so it
	// goes through abi::__cxa_demangle first. All the helpers below build on this,
	// so they return the same strings on every platform.
	std::string readableTypeName(const std::type_info& info);

	// "class newui::ButtonStyle" -> "ButtonStyle": readableTypeName() with
	// everything up to the last "::" dropped.
	std::string demangleTypeName(const std::type_info& info);
	std::string extractNamespace(const std::type_info& info);

	// UTF-8 <-> UTF-16 conversion via MultiByteToWideChar()/WideCharToMultiByte()
	// - the shared home for what used to be separate private copies of this
	// exact conversion in dialogs.cpp and text.cpp; reflection.cpp's
	// std::wstring property support (TextField::text()) is a third consumer.
	std::wstring utf8ToWide(const std::string& text);
	std::string wideToUtf8(const std::wstring& text);

	// Portable UTF-8 <-> UTF-16 (no platform API, same results everywhere). These are what
	// the text code moves to (Step 2 of docs/phase0-portability-plan.md): std::wstring is
	// 16-bit on Windows but 32-bit on Apple, std::u16string is 16-bit everywhere.
	// Malformed input never fails: each maximal invalid subsequence (UTF-8) or unpaired
	// surrogate (UTF-16) becomes U+FFFD, as the Unicode standard recommends.
	std::u16string utf8ToUtf16(const std::string& text);
	std::string utf16ToUtf8(const std::u16string& text);

	// The Win32 boundary: wchar_t is 16-bit here, so UTF-16 text (char16_t) and the wide strings
	// Windows APIs take are the same bytes. These casts are the only place that relies on it; the
	// rest of the text code uses char16_t / std::u16string.
#ifdef _WIN32
	static_assert(sizeof(wchar_t) == sizeof(char16_t), "Win32 wchar_t is UTF-16");
	inline const wchar_t* toWide(const char16_t* text) { return reinterpret_cast<const wchar_t*>(text); }
	inline const wchar_t* toWide(const std::u16string& text) { return reinterpret_cast<const wchar_t*>(text.c_str()); }
	inline std::wstring toWideString(const std::u16string& text) { return std::wstring(toWide(text), text.size()); }
	inline std::u16string fromWide(const std::wstring& text) {
		return std::u16string(reinterpret_cast<const char16_t*>(text.c_str()), text.size());
	}
#endif

	// A path with '\' turned into '/' and any trailing '/' dropped (a lone "/" stays), so paths
	// from different sources compare and join the same way.
	std::string normalizePath(std::string path);

	// text lower-cased with Windows' own case rules (full Unicode, not just ASCII), for comparing
	// paths and names case-insensitively.
	std::string toLowerCase(const std::string& text);

	// Windows' per-user and shared folders, so app code asks the system instead of reading environment
	// variables. Stateless: every call asks the system, so there is nothing to construct or configure.
	// Each path is UTF-8 with '/' separators and no trailing one (see normalizePath), or an empty string
	// when the system has none to give. None of these creates a folder.
	class SpecialFolders {
	public:
		static std::string home();             // the user's profile folder
		static std::string desktop();
		static std::string documents();
		static std::string downloads();
		static std::string localAppData();     // %LOCALAPPDATA%: this user's data on this machine, not roamed
		static std::string roamingAppData();   // %APPDATA%: this user's data that roams
		static std::string programData();      // %PROGRAMDATA%: shared by every user of the machine
		static std::string programFiles();
		static std::string temp();             // the temporary folder (GetTempPath), which honours TMP/TEMP

		// A new, empty file in temp() with a unique name (prefix: up to three characters of it are used);
		// returns its path, or an empty string if it could not be made. The caller deletes it.
		static std::string createTempFile(const std::string& prefix = std::string());

		SpecialFolders() = delete;
	};


	struct KeyboardEventInfo {
		int scanCode = 0;
		int VKeyCode = 0;
		bool altKeyDown = false;
		int repeatCount = 0;
		int isExtendedKey = 0;
		int keyMask = 0;
		std::uint16_t character = 0;
	};


	std::uint32_t translateVirtualKey(int vkCode, int charCode);
	std::uint32_t translateButtonMask(std::uint32_t win32ButtonMask);
	std::uint32_t translateKeyMask(std::uint32_t win32KeyMask);

	std::uint32_t translateCharToVKCode(int charCode);

	bool translateKeyEventInfo(NativeWindowHandle hwnd, NativeMessage message, NativeWParam wParam, NativeLParam lParam, KeyboardEventInfo& outInfo);
}
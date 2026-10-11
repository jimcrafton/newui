
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <cstdint>
#include <string>
#include <typeinfo>

namespace newui {
	// Strips MSVC's typeid(...).name() decoration down to a bare class
	// name: "class newui::ButtonStyle" -> "ButtonStyle" ("class "/"struct "/
	// "enum "/"union " prefix stripped, then everything up to the last "::"
	// dropped). MSVC only (this project's sole toolchain) - unlike
	// Itanium-ABI compilers (GCC/Clang), MSVC's name() is already
	// human-readable, not mangled, so this needs no abi::__cxa_demangle
	// equivalent.
	std::string demangleTypeName(const std::type_info& info);
	std::string extractNamespace(const std::type_info& info);

	// UTF-8 <-> UTF-16 conversion via MultiByteToWideChar()/WideCharToMultiByte()
	// - the shared home for what used to be separate private copies of this
	// exact conversion in dialogs.cpp and text.cpp; reflection.cpp's
	// std::wstring property support (TextField::text()) is a third consumer.
	std::wstring utf8ToWide(const std::string& text);
	std::string wideToUtf8(const std::wstring& text);

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
		WORD character = 0;
	};


	std::uint32_t translateVirtualKey(int vkCode, int charCode);
	std::uint32_t translateButtonMask(UINT win32ButtonMask);
	std::uint32_t translateKeyMask(UINT win32KeyMask);

	std::uint32_t translateCharToVKCode(int charCode);

	bool translateKeyEventInfo(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, KeyboardEventInfo& outInfo);
}

#pragma once
#include <windows.h>

#include <cstdint>
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

#pragma once
#include <cstdint>


namespace newui {

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
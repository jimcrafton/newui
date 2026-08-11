#include "newui/newui.h"
#include "newui/utils.h"
#include "newui/keyboard_constants.h"
#include "newui/mouse_constants.h"
#include <bitset>


namespace newui {
	std::string extractNamespace(const std::type_info& info)
	{
		std::string result = "";

		std::string name = info.name();
		size_t pos = name.rfind("::");
		if (pos != std::string::npos) {
			result = name.substr(0, pos + 2);
		}
		return result;
	}

	std::string demangleTypeName(const std::type_info& info) {
		std::string name = info.name();

		static const char* kPrefixes[] = { "class ", "struct ", "enum ", "union " };
		for (const char* prefix : kPrefixes) {
			size_t len = std::strlen(prefix);
			if (name.compare(0, len, prefix) == 0) {
				name.erase(0, len);
				break;
			}
		}

		size_t pos = name.rfind("::");
		if (pos != std::string::npos) {
			name.erase(0, pos + 2);
		}

		return name;
	}


	std::uint32_t translateVirtualKey(int vkCode, int charCode) {
		std::uint32_t result = 0;

		switch (vkCode) {
		case VK_F1: {
			result = vkF1;
		}
		break;

		case VK_F2: {
			result = vkF2;
		}
		break;

		case VK_F3: {
			result = vkF3;
		}
		break;

		case VK_F4: {
			result = vkF4;
		}
		break;

		case VK_F5: {
			result = vkF5;
		}
		break;

		case VK_F6: {
			result = vkF6;
		}
		break;

		case VK_F7: {
			result = vkF7;
		}
		break;

		case VK_F8: {
			result = vkF8;
		}
		break;

		case VK_F9: {
			result = vkF9;
		}
		break;

		case VK_F10: {
			result = vkF10;
		}
		break;

		case VK_F11: {
			result = vkF11;
		}
		break;

		case VK_F12: {
			result = vkF12;
		}
		break;

		case VK_UP: {
			result = vkUpArrow;
		}
		break;

		case VK_DOWN: {
			result = vkDownArrow;
		}
		break;

		case VK_LEFT: {
			result = vkLeftArrow;
		}
		break;

		case VK_RIGHT: {
			result = vkRightArrow;
		}
		break;

		case VK_DELETE: {
			result = vkDelete;
		}
		break;

		case VK_RETURN: {
			result = vkReturn;
		}
		break;

		case VK_BACK: {
			result = vkBackSpace;
		}
		break;

		case VK_SPACE: {
			result = vkSpaceBar;
		}
		break;

		case VK_ESCAPE: {
			result = vkEscape;
		}
		break;

		case VK_NEXT: {
			result = vkPgDown;
		}
		break;

		case VK_PRIOR: {
			result = vkPgUp;
		}
		break;

		case VK_HOME: {
			result = vkHome;
		}
		break;

		case VK_END: {
			result = vkEnd;
		}
		break;

		case VK_CONTROL: {
			result = vkCtrl;
		}
		break;

		case VK_MENU: {
			result = vkAlt;
		}
		break;

		case VK_SHIFT: {
			result = vkShift;
		}
		break;

		case VK_TAB: {
			result = vkTab;
		}
		break;

		// from WINUSER.h
		// VK_0 thru VK_9 are the same as ASCII '0' thru '9' (0x30 - 0x39)
		case VK_NUMPAD0: case 0x30: {
			result = vkNumber0;
		}
		break;

		case VK_NUMPAD1: case 0x31: {
			result = vkNumber1;
		}
		break;

		case VK_NUMPAD2: case 0x32: {
			result = vkNumber2;
		}
		break;

		case VK_NUMPAD3: case 0x33: {
			result = vkNumber3;
		}
		break;

		case VK_NUMPAD4: case 0x34: {
			result = vkNumber4;
		}
		break;

		case VK_NUMPAD5: case 0x35: {
			result = vkNumber5;
		}
		break;

		case VK_NUMPAD6: case 0x36: {
			result = vkNumber6;
		}
		break;

		case VK_NUMPAD7: case 0x37: {
			result = vkNumber7;
		}
		break;

		case VK_NUMPAD8: case 0x38: {
			result = vkNumber8;
		}
		break;

		case VK_NUMPAD9: case 0x39: {
			result = vkNumber9;
		}
		break;

		// from WINUSER.h
		// VK_A thru VK_Z are the same as ASCII 'A' thru 'Z' (0x41 - 0x5A)
		case 'A': {
			result = vkLetterA;
		}
		break;

		case 'B': {
			result = vkLetterB;
		}
		break;

		case 'C': {
			result = vkLetterC;
		}
		break;

		case 'D': {
			result = vkLetterD;
		}
		break;

		case 'E': {
			result = vkLetterE;
		}
		break;

		case 'F': {
			result = vkLetterF;
		}
		break;

		case 'G': {
			result = vkLetterG;
		}
		break;

		case 'H': {
			result = vkLetterH;
		}
		break;

		case 'I': {
			result = vkLetterI;
		}
		break;

		case 'J': {
			result = vkLetterJ;
		}
		break;

		case 'K': {
			result = vkLetterK;
		}
		break;

		case 'L': {
			result = vkLetterL;
		}
		break;

		case 'M': {
			result = vkLetterM;
		}
		break;

		case 'N': {
			result = vkLetterN;
		}
		break;

		case 'O': {
			result = vkLetterO;
		}
		break;

		case 'P': {
			result = vkLetterP;
		}
		break;

		case 'Q': {
			result = vkLetterQ;
		}
		break;

		case 'R': {
			result = vkLetterR;
		}
		break;

		case 'S': {
			result = vkLetterS;
		}
		break;

		case 'T': {
			result = vkLetterT;
		}
		break;

		case 'U': {
			result = vkLetterU;
		}
		break;

		case 'V': {
			result = vkLetterV;
		}
		break;

		case 'W': {
			result = vkLetterW;
		}
		break;

		case 'X': {
			result = vkLetterX;
		}
		break;

		case 'Y': {
			result = vkLetterY;
		}
		break;

		case 'Z': {
			result = vkLetterZ;
		}
		break;

		case VK_SNAPSHOT: {
			result = vkPrintScreen;
		}
		break;

		case VK_PAUSE: {
			result = vkPause;
		}
		break;

		case VK_SCROLL: {
			result = vkScrollLock;
		}
		break;

		case VK_MULTIPLY: {
			result = vkMultiplySign;
		}
		break;

		case VK_ADD: {
			result = vkPlusSign;
		}
		break;

		/*
		case VK_SEPARATOR: {
			result = vkScrollLock;
		}
		break;
		*/

		case VK_SUBTRACT: {
			result = vkMinusSign;
		}
		break;

		case VK_DECIMAL: {
			result = vkPeriod;
		}
		break;

		case VK_DIVIDE: {
			result = vkDivideSign;
		}
		break;
		}



		if (vkUndefined == vkCode) { //tranlsate by ascii value

			switch (charCode) {
				case '{': {
					vkCode = vkOpenBrace;
				}
				break;

				case '}': {
					vkCode = vkCloseBrace;
				}
				break;

				case '[': {
					vkCode = vkOpenBracket;
				}
				break;

				case ']': {
					vkCode = vkCloseBracket;
				}
				break;

				case '~': {
					vkCode = vkTilde;
				}
				break;

				case '`': {
					vkCode = vkLeftApostrophe;
				}
				break;
			}
		}
		return result;
	}


	std::uint32_t translateButtonMask(UINT win32ButtonMask)
	{
		std::uint32_t result = mbmUndefined;

		if ((win32ButtonMask & MK_LBUTTON) != 0) {
			result |= mbmLeftButton;
		}

		if ((win32ButtonMask & MK_MBUTTON) != 0) {
			result |= mbmMiddleButton;
		}

		if ((win32ButtonMask & MK_RBUTTON) != 0) {
			result |= mbmRightButton;
		}

		return result;
	}

	std::uint32_t translateKeyMask(UINT win32KeyMask)
	{
		std::uint32_t result = kmUndefined;

		if ((win32KeyMask & MK_CONTROL) != 0) {
			result |= kmCtrl;
		}

		if ((win32KeyMask & MK_SHIFT) != 0) {
			result |= kmShift;
		}

		if (::GetAsyncKeyState(VK_MENU) < 0) {
			result |= kmAlt;
		}

		return result;
	}


	bool translateKeyEventInfo(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, KeyboardEventInfo& outInfo)
	{
		BYTE keyState[256];

		memset(keyState, 0, sizeof(keyState));
		
		outInfo.scanCode = ((BYTE*)&lParam)[2]; //gets bits 16-23

		if (GetKeyboardState(&keyState[0])) {
			
			outInfo.repeatCount = lParam & 0xFFFF; //mask out the upper 16 bits

			outInfo.altKeyDown = (lParam & KB_CONTEXT_CODE) != 0;
			outInfo.isExtendedKey = (lParam & KB_IS_EXTENDED_KEY) != 0;
			outInfo.character = 0;

			outInfo.VKeyCode = MapVirtualKey(outInfo.scanCode, 1);

		}

		HKL keyboardLayout = GetKeyboardLayout(GetWindowThreadProcessId(hwnd, NULL));

		ToAsciiEx(outInfo.VKeyCode, outInfo.scanCode, &keyState[0], &outInfo.character, 1, keyboardLayout);
		std::bitset<16> keyBits;
		keyBits = GetAsyncKeyState(VK_SHIFT);
		if (keyBits[15] == 1) {
			outInfo.keyMask |= kmShift;
		}

		keyBits = GetAsyncKeyState(VK_CONTROL);
		if (keyBits[15] == 1) {
			outInfo.keyMask |= kmCtrl;
		}

		keyBits = GetAsyncKeyState(VK_MENU);
		if (keyBits[15] == 1) {
			outInfo.altKeyDown = true;
			outInfo.keyMask |= kmAlt;
		}

		return true;
	}


	std::uint32_t translateCharToVKCode(int charCode)
	{
		std::uint32_t result = 0;

		charCode = tolower(charCode);
		switch (charCode) {
			case 'a': {
				result = vkLetterA;
			}
					break;

			case 'b': {
				result = vkLetterB;
			}
					break;

			case 'c': {
				result = vkLetterC;
			}
					break;

			case 'd': {
				result = vkLetterD;
			}
					break;

			case 'e': {
				result = vkLetterE;
			}
					break;

			case 'f': {
				result = vkLetterF;
			}
					break;

			case 'g': {
				result = vkLetterG;
			}
					break;

			case 'h': {
				result = vkLetterH;
			}
					break;

			case 'i': {
				result = vkLetterI;
			}
					break;

			case 'j': {
				result = vkLetterJ;
			}
					break;

			case 'k': {
				result = vkLetterK;
			}
					break;

			case 'l': {
				result = vkLetterL;
			}
					break;

			case 'm': {
				result = vkLetterM;
			}
					break;

			case 'n': {
				result = vkLetterN;
			}
					break;

			case 'o': {
				result = vkLetterO;
			}
					break;

			case 'p': {
				result = vkLetterP;
			}
					break;

			case 'q': {
				result = vkLetterQ;
			}
					break;

			case 'r': {
				result = vkLetterR;
			}
					break;

			case 's': {
				result = vkLetterS;
			}
					break;

			case 't': {
				result = vkLetterT;
			}
					break;

			case 'u': {
				result = vkLetterU;
			}
					break;

			case 'v': {
				result = vkLetterV;
			}
					break;

			case 'w': {
				result = vkLetterW;
			}
					break;

			case 'x': {
				result = vkLetterX;
			}
					break;

			case 'y': {
				result = vkLetterY;
			}
					break;

			case 'z': {
				result = vkLetterZ;
			}
					break;

			case '0': {
				result = vkNumber0;
			}
					break;

			case '1': {
				result = vkNumber1;
			}
					break;

			case '2': {
				result = vkNumber2;
			}
					break;

			case '3': {
				result = vkNumber3;
			}
					break;

			case '4': {
				result = vkNumber4;
			}
					break;

			case '5': {
				result = vkNumber5;
			}
					break;

			case '6': {
				result = vkNumber6;
			}
					break;

			case '7': {
				result = vkNumber7;
			}
					break;

			case '8': {
				result = vkNumber8;
			}
					break;

			case '9': {
				result = vkNumber9;
			}
					break;

			case '/': {
				result = vkDivideSign;
			}
					break;

			case '+': {
				result = vkPlusSign;
			}
					break;

			case '-': {
				result = vkMinusSign;
			}
					break;

			case '=': {
				result = vkEqualsSign;
			}
					break;

			case '_': {
				result = vkUnderbar;
			}
					break;

			case '|': {
				result = vkUprightBar;
			}
					break;

			case '{': {
				result = vkOpenBrace;
			}
					break;

			case '}': {
				result = vkCloseBrace;
			}
					break;

			case '[': {
				result = vkOpenBracket;
			}
					break;

			case ']': {
				result = vkCloseBracket;
			}
					break;

			case '<': {
				result = vkLessThan;
			}
					break;

			case '>': {
				result = vkGreaterThan;
			}
					break;

			case '.': {
				result = vkPeriod;
			}
					break;

			case ',': {
				result = vkComma;
			}
					break;

			case '!': {
				result = vkExclamation;
			}
					break;

			case '~': {
				result = vkTilde;
			}
					break;

			case '`': {
				result = vkLeftApostrophe;
			}
					break;

			case '@': {
				result = vkCommercialAt;
			}
					break;

			case '#': {
				result = vkNumberSign;
			}
					break;

			case '$': {
				result = vkDollarSign;
			}
					break;

			case '%': {
				result = vkPercent;
			}
					break;

			case '^': {
				result = vkCircumflex;
			}
					break;

			case '&': {
				result = vkAmpersand;
			}
					break;

			case '*': {
				result = vkAsterix;
			}
					break;

			case '(': {
				result = vkOpenParen;
			}
					break;

			case ')': {
				result = vkCloseParen;
			}
					break;

			case ':': {
				result = vkColon;
			}
					break;

			case ';': {
				result = vkSemiColon;
			}
					break;

			case '"': {
				result = vkDoubleQuote;
			}
					break;

			case '\'': {
				result = vkSingleQuote;
			}
					 break;

			case '\\': {
				result = vkBackSlash;
			}
					 break;

			case '?': {
				result = vkQuestionMark;
			}
				break;
		}
		return result;
	}
}

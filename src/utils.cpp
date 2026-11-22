#include "newui/newui.h"
#include "newui/utils.h"
#include "newui/keyboard_constants.h"
#include "newui/mouse_constants.h"
#include <bitset>
#include <cstdlib>
#include <cstring>
#include <memory>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif
#include <KnownFolders.h>
#include <ShlObj.h>


namespace newui {
	std::wstring utf8ToWide(const std::string& text)
	{
		if (text.empty()) {
			return std::wstring();
		}
		int required = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
		if (required <= 0) {
			return std::wstring();
		}
		std::wstring result(static_cast<std::size_t>(required), L'\0');
		::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), required);
		return result;
	}

	std::string wideToUtf8(const std::wstring& text)
	{
		if (text.empty()) {
			return std::string();
		}
		int required = ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
		if (required <= 0) {
			return std::string();
		}
		std::string result(static_cast<std::size_t>(required), '\0');
		::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), result.data(), required, nullptr, nullptr);
		return result;
	}

	std::u16string utf8ToUtf16(const std::string& text)
	{
		std::u16string out;
		out.reserve(text.size());
		const std::size_t n = text.size();
		std::size_t i = 0;
		auto byteAt = [&](std::size_t k) { return static_cast<unsigned char>(text[k]); };

		while (i < n) {
			const unsigned char lead = byteAt(i);
			if (lead < 0x80) {
				out.push_back(static_cast<char16_t>(lead));
				++i;
				continue;
			}

			// Number of continuation bytes and the allowed range of the first one
			// (Unicode Table 3-7), which rules out overlongs, surrogates and > U+10FFFF.
			int extra = 0;
			unsigned char lo = 0x80, hi = 0xBF;
			char32_t cp = 0;
			if (lead >= 0xC2 && lead <= 0xDF) { extra = 1; cp = lead & 0x1F; }
			else if (lead >= 0xE0 && lead <= 0xEF) {
				extra = 2; cp = lead & 0x0F;
				if (lead == 0xE0) lo = 0xA0;
				if (lead == 0xED) hi = 0x9F;
			}
			else if (lead >= 0xF0 && lead <= 0xF4) {
				extra = 3; cp = lead & 0x07;
				if (lead == 0xF0) lo = 0x90;
				if (lead == 0xF4) hi = 0x8F;
			}
			else {
				out.push_back(u'\uFFFD');   // stray continuation byte, C0/C1, F5..FF
				++i;
				continue;
			}

			std::size_t used = 1;
			bool ok = true;
			for (int k = 0; k < extra; ++k) {
				if (i + used >= n) { ok = false; break; }
				const unsigned char b = byteAt(i + used);
				if (b < lo || b > hi) { ok = false; break; }
				cp = (cp << 6) | (b & 0x3F);
				lo = 0x80; hi = 0xBF;
				++used;
			}
			i += used;
			if (!ok) {
				out.push_back(u'\uFFFD');
				continue;
			}
			if (cp >= 0x10000) {
				cp -= 0x10000;
				out.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
				out.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
			}
			else {
				out.push_back(static_cast<char16_t>(cp));
			}
		}
		return out;
	}

	std::string utf16ToUtf8(const std::u16string& text)
	{
		std::string out;
		out.reserve(text.size());
		const std::size_t n = text.size();
		for (std::size_t i = 0; i < n; ++i) {
			char32_t cp = text[i];
			if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < n && text[i + 1] >= 0xDC00 && text[i + 1] <= 0xDFFF) {
				cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
				++i;
			}
			else if (cp >= 0xD800 && cp <= 0xDFFF) {
				cp = 0xFFFD;   // unpaired surrogate
			}

			if (cp < 0x80) {
				out.push_back(static_cast<char>(cp));
			}
			else if (cp < 0x800) {
				out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else if (cp < 0x10000) {
				out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
			else {
				out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
				out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
			}
		}
		return out;
	}

	std::string normalizePath(std::string path)
	{
		for (char& c : path) {
			if (c == '\\') c = '/';
		}
		while (path.size() > 1 && path.back() == '/') path.pop_back();
		return path;
	}

	std::string toLowerCase(const std::string& text)
	{
		std::wstring wide = utf8ToWide(text);
		if (!wide.empty()) {
			::CharLowerBuffW(wide.data(), static_cast<DWORD>(wide.size()));
		}
		return wideToUtf8(wide);
	}

	namespace {
		std::string knownFolder(const KNOWNFOLDERID& id)
		{
			PWSTR path = nullptr;
			std::string result;
			if (SUCCEEDED(::SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &path)) && path != nullptr) {
				result = normalizePath(wideToUtf8(path));
			}
			::CoTaskMemFree(path);   // allocated even when the call fails
			return result;
		}
	}

	std::string SpecialFolders::home() { return knownFolder(FOLDERID_Profile); }
	std::string SpecialFolders::desktop() { return knownFolder(FOLDERID_Desktop); }
	std::string SpecialFolders::documents() { return knownFolder(FOLDERID_Documents); }
	std::string SpecialFolders::downloads() { return knownFolder(FOLDERID_Downloads); }
	std::string SpecialFolders::localAppData() { return knownFolder(FOLDERID_LocalAppData); }
	std::string SpecialFolders::roamingAppData() { return knownFolder(FOLDERID_RoamingAppData); }
	std::string SpecialFolders::programData() { return knownFolder(FOLDERID_ProgramData); }
	std::string SpecialFolders::programFiles() { return knownFolder(FOLDERID_ProgramFiles); }

	std::string SpecialFolders::temp()
	{
		wchar_t buffer[MAX_PATH + 2];
		const DWORD size = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
		const DWORD length = ::GetTempPathW(size, buffer);
		if (length == 0 || length >= size) {
			return std::string();
		}
		return normalizePath(wideToUtf8(std::wstring(buffer, length)));
	}

	std::string SpecialFolders::createTempFile(const std::string& prefix)
	{
		wchar_t folder[MAX_PATH + 2];
		const DWORD size = static_cast<DWORD>(sizeof(folder) / sizeof(folder[0]));
		const DWORD length = ::GetTempPathW(size, folder);
		if (length == 0 || length >= size) {
			return std::string();
		}
		wchar_t file[MAX_PATH + 2];
		if (::GetTempFileNameW(folder, utf8ToWide(prefix).c_str(), 0, file) == 0) {   // creates the empty file
			return std::string();
		}
		return normalizePath(wideToUtf8(file));
	}

	std::string readableTypeName(const std::type_info& info)
	{
		std::string name = info.name();

#if defined(__GNUC__) || defined(__clang__)
		int status = 0;
		std::unique_ptr<char, void (*)(void*)> demangled(abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status), std::free);
		if (status == 0 && demangled) {
			name = demangled.get();
		}
#endif

		// MSVC's "class "/"struct "/"enum "/"union " keyword prefix.
		static const char* kPrefixes[] = { "class ", "struct ", "enum ", "union " };
		for (const char* prefix : kPrefixes) {
			size_t len = std::strlen(prefix);
			if (name.compare(0, len, prefix) == 0) {
				name.erase(0, len);
				break;
			}
		}
		return name;
	}

	std::string extractNamespace(const std::type_info& info)
	{
		const std::string name = readableTypeName(info);
		size_t pos = name.rfind("::");
		return pos != std::string::npos ? name.substr(0, pos + 2) : std::string();
	}

	std::string demangleTypeName(const std::type_info& info) {
		std::string name = readableTypeName(info);
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

		case VK_INSERT: {
			result = vkInsert;
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
		// GetKeyState, not GetAsyncKeyState: this is the modifier state as of the message being handled. The async
		// (live) state is already wrong when keys are injected in one quick batch (SendInput: Ctrl down, Z down,
		// Z up, Ctrl up) because Ctrl has been released again by the time the Z message is processed.
		if (GetKeyState(VK_SHIFT) < 0) {
			outInfo.keyMask |= kmShift;
		}

		if (GetKeyState(VK_CONTROL) < 0) {
			outInfo.keyMask |= kmCtrl;
		}

		if (GetKeyState(VK_MENU) < 0) {
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

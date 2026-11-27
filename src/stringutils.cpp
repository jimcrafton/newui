// The platform-neutral part of newui/utils.h: UTF-8/UTF-16 conversion, path normalising and type names.
// The rest (utils.cpp) needs the Win32 API.
#include "newui/utils.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

namespace newui {

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

}

#include "newui/presentsurface.h"

#include <atomic>
#include <cctype>

#include "newui/dxgipresentsurface.h"
#include "newui/gdipresentsurface.h"

namespace {

	// -1 = never set; otherwise a PresentBackend value.
	std::atomic<int> g_defaultOverride{ -1 };

	newui::PresentBackend readEnvironmentBackend() {
		char buffer[32] = {};
		const DWORD length = ::GetEnvironmentVariableA("NEWUI_PRESENT", buffer, DWORD(sizeof(buffer)));
		newui::PresentBackend backend = newui::PresentBackend::Gdi;
		if (length > 0 && length < sizeof(buffer)) {
			newui::parsePresentBackend(buffer, backend);
		}
		return backend;
	}

}

namespace newui {

	bool parsePresentBackend(const std::string& text, PresentBackend& out) {
		std::string lower;
		for (char c : text) {
			lower += char(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower == "gdi") {
			out = PresentBackend::Gdi;
			return true;
		}
		if (lower == "dxgi") {
			out = PresentBackend::Dxgi;
			return true;
		}
		return false;
	}

	PresentBackend defaultPresentBackend() {
		const int override = g_defaultOverride.load();
		if (override >= 0) {
			return static_cast<PresentBackend>(override);
		}
		static const PresentBackend fromEnvironment = readEnvironmentBackend();
		return fromEnvironment;
	}

	void setDefaultPresentBackend(PresentBackend backend) {
		g_defaultOverride.store(static_cast<int>(backend));
	}

	std::unique_ptr<PresentSurface> createPresentSurface(PresentBackend backend) {
		if (backend == PresentBackend::Dxgi) {
			return std::make_unique<DxgiPresentSurface>();
		}
		return std::make_unique<GdiPresentSurface>();
	}

}

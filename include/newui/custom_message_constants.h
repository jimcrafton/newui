#pragma once
#include <cstdint>


namespace newui {

	// Equals WM_APP + 1 on Windows (runloop.cpp static_asserts that); a plain value here so this header needs no <windows.h>.
	static constexpr unsigned int kTaskMessage = 0x8000 + 1;

}


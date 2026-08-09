
#pragma once
#include <cstdint>


namespace newui {
	enum MouseButtonMasks {
		mbmUndefined = 0,
		mbmLeftButton = 1,
		mbmMiddleButton = 2,
		mbmRightButton = 4,
		mbmPrimaryButton = mbmLeftButton,
		mbmSecondaryButton = mbmRightButton,
		mbmTertiaryButton = mbmMiddleButton
	};
}
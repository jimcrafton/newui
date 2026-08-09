#include "newui/newui.h"

#include <gtest/gtest.h>

TEST(Version, MatchesCMakeProjectVersion) {
    EXPECT_STREQ(newui::version(), NEWUI_EXPECTED_VERSION);
}

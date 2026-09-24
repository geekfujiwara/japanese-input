#include "astelio/core.h"

#include <gtest/gtest.h>

#include <string_view>

TEST(CoreVersion, MatchesProjectVersion)
{
    EXPECT_EQ(std::string_view{astelio_core_version()}, std::string_view{ASTELIO_EXPECTED_VERSION});
}

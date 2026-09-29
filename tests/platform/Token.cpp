#include "platform/Token.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>

TEST(Token, ReadsThisProcesssIntegrityLevel) {
    EXPECT_THAT(IntegrityLevel(GetCurrentProcessToken()),
                testing::AnyOf(std::uint32_t{SECURITY_MANDATORY_LOW_RID}, std::uint32_t{SECURITY_MANDATORY_MEDIUM_RID}, std::uint32_t{SECURITY_MANDATORY_HIGH_RID},
                               std::uint32_t{SECURITY_MANDATORY_SYSTEM_RID}));
}

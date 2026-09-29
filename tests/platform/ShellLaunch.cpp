#include "platform/ShellLaunch.hpp"

#include <windows.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>

// A session with no shell, such as a service's, has no window to read.
TEST(ShellLaunch, ReadsTheShellsIntegrityLevelOrSaysThereIsNoShell) {
    if (GetShellWindow() == nullptr) {
        EXPECT_THROW(ShellIntegrityLevel(std::chrono::milliseconds(0)), ShellLaunchError);
        return;
    }
    EXPECT_THAT(ShellIntegrityLevel(std::chrono::milliseconds(0)), testing::AnyOf(std::uint32_t{SECURITY_MANDATORY_MEDIUM_RID}, std::uint32_t{SECURITY_MANDATORY_HIGH_RID}));
}

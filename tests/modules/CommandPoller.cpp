#include "modules/CommandPoller.hpp"

#include <gtest/gtest.h>

TEST(CommandPoller, FirstLineDropsTheRestAndTheLineEnding) {
    EXPECT_EQ(FirstLine("sunny 21C\r\ntooltip\r\n"), "sunny 21C");
    EXPECT_EQ(FirstLine("unix\nsecond"), "unix");
    EXPECT_EQ(FirstLine("no newline"), "no newline");
    EXPECT_EQ(FirstLine(""), "");
}

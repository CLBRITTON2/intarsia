#pragma once

#include <cstdint>

// The latest present the display showed: its present count (as the swap chain numbers its presents) and the refresh
// count it showed on.
struct ShownPresent {
    std::uint32_t present;
    std::uint32_t refresh;
};

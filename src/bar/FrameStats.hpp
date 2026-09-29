#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

// How one animation's frames went on one bar, logged when it ends so its pacing can be read from the log.
struct FrameStats {
    std::uint32_t                         frames;
    std::chrono::steady_clock::time_point first;
    std::chrono::steady_clock::time_point last;
    std::chrono::steady_clock::duration   longestInterval;
    std::chrono::steady_clock::duration   longestPresent;
};

// stats with one more frame, presented at `at` after a present that took `present`. No stats starts them.
FrameStats WithFrame(const std::optional<FrameStats>& stats, std::chrono::steady_clock::time_point at, std::chrono::steady_clock::duration present);

#pragma once

#include "render/ShownPresent.hpp"

#include <windows.h>

#include <chrono>
#include <cstdint>
#include <optional>

// How one animation's frames went on one bar, logged when it ends so its pacing can be read from the log. Frames the
// display never showed and refreshes that showed a frame again are counted from the swap chain's statistics, which the
// presents' own timing cannot show.
struct FrameStats {
    std::uint32_t                         frames;
    std::chrono::steady_clock::time_point first;
    std::chrono::steady_clock::time_point last;
    std::chrono::steady_clock::duration   longestInterval;
    std::chrono::steady_clock::duration   longestPresent;
    std::uint32_t                         droppedFrames;
    std::uint32_t                         repeatedRefreshes;
    std::uint32_t                         firstPresent; // the swap chain's number for the animation's first present
    std::optional<ShownPresent>           lastShown;    // null until the swap chain has statistics
};

// stats with one more frame, the swap chain's present `presented`, made at `at` by a present that took `present`, once
// the display has shown `shown`. No stats starts them.
FrameStats WithFrame(const std::optional<FrameStats>& stats, std::chrono::steady_clock::time_point at, std::chrono::steady_clock::duration present, std::uint32_t presented,
                     const std::optional<ShownPresent>& shown);

// Writes stats to the log as the animation_frames event of the bar on monitorRect.
void LogAnimationFrames(const FrameStats& stats, const RECT& monitorRect);

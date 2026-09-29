#include "bar/FrameStats.hpp"

#include <algorithm>

FrameStats WithFrame(const std::optional<FrameStats>& stats, std::chrono::steady_clock::time_point at, std::chrono::steady_clock::duration present) {
    if (!stats.has_value()) {
        return FrameStats{.frames = 1, .first = at, .last = at, .longestInterval = std::chrono::steady_clock::duration::zero(), .longestPresent = present};
    }
    return FrameStats{.frames          = stats->frames + 1,
                      .first           = stats->first,
                      .last            = at,
                      .longestInterval = std::max(stats->longestInterval, at - stats->last),
                      .longestPresent  = std::max(stats->longestPresent, present)};
}

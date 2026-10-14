#include "bar/FrameStats.hpp"

#include "platform/AppBar.hpp"
#include "platform/Log.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <string>

namespace {

    struct ShownGaps {
        std::uint32_t dropped;
        std::uint32_t repeated;
    };

    // How far count runs past limit, 0 when it does not.
    std::uint32_t Excess(std::uint32_t count, std::uint32_t limit) {
        if (count > limit) {
            return count - limit;
        }
        return 0;
    }

    // The display shows one present per refresh at most, so presents that advanced past the refreshes were never shown,
    // and refreshes that advanced past the presents showed a frame again. A gap that starts at a present from before the
    // animation spans the idle refreshes ahead of it, so it counts nothing. Unsigned subtraction and the signed reading
    // of its result carry across the counters' wraparound.
    ShownGaps GapsBetween(const std::optional<ShownPresent>& before, const std::optional<ShownPresent>& after, std::uint32_t firstPresent) {
        if (!before.has_value() || !after.has_value() || static_cast<std::int32_t>(before->present - firstPresent) < 0) {
            return ShownGaps{.dropped = 0, .repeated = 0};
        }
        const std::uint32_t presents  = after->present - before->present;
        const std::uint32_t refreshes = after->refresh - before->refresh;
        return ShownGaps{.dropped = Excess(presents, refreshes), .repeated = Excess(refreshes, presents)};
    }

    // The first frame is the one the animation started on, so the intervals between frames number one fewer.
    std::chrono::steady_clock::duration MeanInterval(const FrameStats& stats) {
        if (stats.frames > 1) {
            return (stats.last - stats.first) / (stats.frames - 1);
        }
        return std::chrono::steady_clock::duration::zero();
    }

    std::string MillisecondsText(std::chrono::steady_clock::duration duration) {
        return std::format("{:.1f}", std::chrono::duration<double, std::milli>(duration).count());
    }

} // namespace

FrameStats WithFrame(const std::optional<FrameStats>& stats, std::chrono::steady_clock::time_point at, std::chrono::steady_clock::duration present, std::uint32_t presented,
                     const std::optional<ShownPresent>& shown) {
    if (!stats.has_value()) {
        return FrameStats{
            .frames            = 1,
            .first             = at,
            .last              = at,
            .longestInterval   = std::chrono::steady_clock::duration::zero(),
            .longestPresent    = present,
            .droppedFrames     = 0,
            .repeatedRefreshes = 0,
            .firstPresent      = presented,
            .lastShown         = shown,
        };
    }
    const ShownGaps gaps = GapsBetween(stats->lastShown, shown, stats->firstPresent);
    return FrameStats{
        .frames            = stats->frames + 1,
        .first             = stats->first,
        .last              = at,
        .longestInterval   = std::max(stats->longestInterval, at - stats->last),
        .longestPresent    = std::max(stats->longestPresent, present),
        .droppedFrames     = stats->droppedFrames + gaps.dropped,
        .repeatedRefreshes = stats->repeatedRefreshes + gaps.repeated,
        .firstPresent      = stats->firstPresent,
        .lastShown         = shown,
    };
}

void LogAnimationFrames(const FrameStats& stats, const RECT& monitorRect) {
    Log(LogLevel::Info, "bar", "animation_frames",
        {{.key = "monitor", .value = RectText(monitorRect)},
         {.key = "frames", .value = std::to_string(stats.frames)},
         {.key = "length_ms", .value = MillisecondsText(stats.last - stats.first)},
         {.key = "mean_interval_ms", .value = MillisecondsText(MeanInterval(stats))},
         {.key = "longest_interval_ms", .value = MillisecondsText(stats.longestInterval)},
         {.key = "longest_present_ms", .value = MillisecondsText(stats.longestPresent)},
         {.key = "dropped_frames", .value = std::to_string(stats.droppedFrames)},
         {.key = "repeated_refreshes", .value = std::to_string(stats.repeatedRefreshes)}});
}

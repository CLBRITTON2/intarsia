#include "bar/FrameStats.hpp"

#include <algorithm>

namespace {

    struct ShownGaps {
        std::uint32_t dropped;
        std::uint32_t repeated;
    };

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
        return ShownGaps{.dropped = presents > refreshes ? presents - refreshes : 0, .repeated = refreshes > presents ? refreshes - presents : 0};
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

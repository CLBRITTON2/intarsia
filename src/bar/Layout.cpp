#include "bar/Layout.hpp"

#include <iterator>
#include <numeric>

namespace {

    // A section whose modules all show nothing has no box, as one with no modules.
    float SectionLength(const std::vector<float>& lengths, EndInsets insets) {
        const float modules = std::accumulate(lengths.begin(), lengths.end(), 0.0f);
        return modules > 0.0f ? modules + insets.start + insets.end : 0.0f;
    }

    SectionLayout PlaceSection(const std::vector<float>& lengths, float start, EndInsets insets) {
        SectionLayout section{
            .start   = start,
            .length  = SectionLength(lengths, insets),
            .offsets = {},
        };
        section.offsets.reserve(lengths.size());
        std::exclusive_scan(lengths.begin(), lengths.end(), std::back_inserter(section.offsets), start + insets.start);
        return section;
    }

} // namespace

BarLayout ArrangeBar(const Sections<float>& lengths, float barLength, float margin, EndInsets insets) {
    return BarLayout{
        .left   = PlaceSection(lengths.left, margin, insets),
        .center = PlaceSection(lengths.center, (barLength - SectionLength(lengths.center, insets)) / 2.0f, insets),
        .right  = PlaceSection(lengths.right, barLength - margin - SectionLength(lengths.right, insets), insets),
    };
}

GroupLayout ArrangeGroup(float toggleLength, const std::vector<float>& memberLengths, float revealed, EndInsets insets) {
    const float members = std::accumulate(memberLengths.begin(), memberLengths.end(), 0.0f);
    const float shown   = revealed * members;
    const float toggle  = toggleLength + insets.start + insets.end;
    GroupLayout group{
        .length       = toggle + shown,
        .toggleLength = toggle,
        .shownLength  = shown,
        .offsets      = {},
    };
    group.offsets.reserve(memberLengths.size());
    std::exclusive_scan(memberLengths.begin(), memberLengths.end(), std::back_inserter(group.offsets), toggle);
    return group;
}

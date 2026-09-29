#include "bar/Layout.hpp"

#include <numeric>

namespace {

    // A section whose modules all show nothing has no box, as one with no modules.
    float SectionLength(const std::vector<float>& lengths, float padding) {
        const float modules = std::accumulate(lengths.begin(), lengths.end(), 0.0f);
        return modules > 0.0f ? modules + (2.0f * padding) : 0.0f;
    }

    SectionLayout PlaceSection(const std::vector<float>& lengths, float start, float padding) {
        SectionLayout section{.start = start, .length = SectionLength(lengths, padding), .offsets = {}};
        section.offsets.reserve(lengths.size());
        float next = start + padding;
        for (const float length : lengths) {
            section.offsets.push_back(next);
            next += length;
        }
        return section;
    }

} // namespace

BarLayout ArrangeBar(const Sections<float>& lengths, float barLength, float margin, float padding) {
    return BarLayout{
        .left   = PlaceSection(lengths.left, margin, padding),
        .center = PlaceSection(lengths.center, (barLength - SectionLength(lengths.center, padding)) / 2.0f, padding),
        .right  = PlaceSection(lengths.right, barLength - margin - SectionLength(lengths.right, padding), padding),
    };
}

GroupLayout ArrangeGroup(float toggleLength, const std::vector<float>& memberLengths, float revealed, float padding) {
    const float members = std::accumulate(memberLengths.begin(), memberLengths.end(), 0.0f);
    const float shown   = revealed * members;
    const float toggle  = toggleLength + (2.0f * padding);
    GroupLayout group{.length = toggle + shown, .toggleLength = toggle, .shownStart = toggle, .shownLength = shown, .offsets = {}};
    group.offsets.reserve(memberLengths.size());
    float next = toggle;
    for (const float length : memberLengths) {
        group.offsets.push_back(next);
        next += length;
    }
    return group;
}

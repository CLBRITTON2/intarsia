#include "bar/Layout.hpp"

#include <numeric>

namespace {

    float SectionLength(const std::vector<float>& lengths, float padding) {
        if (lengths.empty()) {
            return 0.0f;
        }
        return std::accumulate(lengths.begin(), lengths.end(), 0.0f) + (2.0f * padding);
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

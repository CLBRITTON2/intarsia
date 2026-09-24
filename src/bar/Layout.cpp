#include "bar/Layout.hpp"

#include <numeric>

namespace {

    std::vector<float> PackFrom(const std::vector<float>& lengths, float start, float spacing) {
        std::vector<float> offsets;
        offsets.reserve(lengths.size());
        float next = start;
        for (const float length : lengths) {
            offsets.push_back(next);
            next += length + spacing;
        }
        return offsets;
    }

    float PackedLength(const std::vector<float>& lengths, float spacing) {
        if (lengths.empty()) {
            return 0.0f;
        }
        return std::accumulate(lengths.begin(), lengths.end(), 0.0f) + (spacing * static_cast<float>(lengths.size() - 1));
    }

} // namespace

Sections<float> ArrangeSections(const Sections<float>& lengths, float barLength, float spacing) {
    return {
        .left   = PackFrom(lengths.left, spacing, spacing),
        .center = PackFrom(lengths.center, (barLength - PackedLength(lengths.center, spacing)) / 2.0f, spacing),
        .right  = PackFrom(lengths.right, barLength - spacing - PackedLength(lengths.right, spacing), spacing),
    };
}

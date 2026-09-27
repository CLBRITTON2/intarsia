#pragma once

#include "bar/Sections.hpp"

#include <vector>

// Where one section's box sits along the bar's main axis, and where each of its modules' boxes starts.
struct SectionLayout {
    float              start;
    float              length; // 0 for a section with no modules
    std::vector<float> offsets;
};

struct BarLayout {
    SectionLayout left;
    SectionLayout center;
    SectionLayout right;
};

// Sections for module boxes of the given lengths along the main axis. Left sits margin from the start, right margin
// from the end, center is centered on the whole bar. padding is between a section's edge and its modules, which sit
// side by side.
BarLayout ArrangeBar(const Sections<float>& lengths, float barLength, float margin, float padding);

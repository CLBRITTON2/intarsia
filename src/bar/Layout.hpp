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

// A group's box along the main axis, and where each of its members' boxes starts, from the start of the group's.
struct GroupLayout {
    float              length;
    float              toggleLength; // the toggle inside padding on both sides, the whole box while folded
    float              shownStart;   // the span of the members the fold uncovers
    float              shownLength;
    std::vector<float> offsets;
};

// A group's toggle inside padding on both sides, then its members box to box, so the gap from the toggle to the first
// member is the padding plus the member's own, as between any two modules. revealed (0 folded, 1 unfolded) is the
// share of the members' total length the box takes, so members past it lie outside the shown span.
GroupLayout ArrangeGroup(float toggleLength, const std::vector<float>& memberLengths, float revealed, float padding);

#pragma once

#include "bar/Sections.hpp"

#include <vector>

// How far a box's content sits in from its start and its end along the bar: its padding plus its border on that side.
struct EndInsets {
    float start;
    float end;
};

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
// from the end, center is centered on the whole bar. insets are between a section's ends and its modules, which sit
// side by side.
BarLayout ArrangeBar(const Sections<float>& lengths, float barLength, float margin, EndInsets insets);

// A group's box along the main axis, and where each of its members' boxes starts, from the start of the group's.
struct GroupLayout {
    float              length;
    float              toggleLength; // the toggle inside the insets at both ends, the whole box while folded
    float              shownStart;   // the span of the members the fold uncovers
    float              shownLength;
    std::vector<float> offsets;
};

// A group's toggle inside its insets at both ends, then its members box to box, so the gap from the toggle to the first
// member is the end inset plus the member's own start, as between any two modules. revealed (0 folded, 1 unfolded) is
// the share of the members' total length the box takes, so members past it lie outside the shown span.
GroupLayout ArrangeGroup(float toggleLength, const std::vector<float>& memberLengths, float revealed, EndInsets insets);

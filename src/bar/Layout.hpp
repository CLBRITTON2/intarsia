#pragma once

#include "bar/Sections.hpp"

// Offsets along the bar's main axis for modules of the given lengths. Left packs from the start, right packs from the
// end, center is centered on the whole bar. spacing separates modules and pads both ends.
Sections<float> ArrangeSections(const Sections<float>& lengths, float barLength, float spacing);

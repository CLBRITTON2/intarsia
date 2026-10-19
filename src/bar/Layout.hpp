#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "modules/ModuleFactory.hpp"
#include "render/Axis.hpp"
#include "render/Box.hpp"

#include <d2d1.h>

#include <vector>

// How far a box's content sits in from its start and its end along the bar: its padding plus its border on that side.
struct EndInsets {
    float start;
    float end;
};

// Where one section's box sits along the bar's main axis, and where each of its modules' boxes starts.
struct SectionLayout {
    float start;
    float length; // 0 for a section with no modules
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
    float length;
    float toggleLength; // the toggle inside the insets at both ends, the whole box while folded
    float shownLength;  // the span of the members the fold uncovers, from toggleLength
    std::vector<float> offsets;
};

// A group's toggle inside its insets at both ends, then its members box to box, so the gap from the toggle to the first
// member is the end inset plus the member's own start, as between any two modules. revealed (0 folded, 1 unfolded) is
// the share of the members' total length the box takes, so members past it lie outside the shown span.
GroupLayout ArrangeGroup(float toggleLength, const std::vector<float>& memberLengths, float revealed, EndInsets insets);

// The style's content insets at the start and end along axis.
EndInsets MainInsets(const BoxStyle& style, Axis axis);

// The style's content insets at the start and end across axis.
EndInsets CrossInsets(const BoxStyle& style, Axis axis);

// Where a module's content starts in its box: past the insets along the bar, centered between them across it.
D2D1_POINT_2F ContentOrigin(Axis axis, const D2D1_RECT_F& box, const Insets& insets, D2D1_SIZE_F content);

// What placing a bar's modules reads of the bar: the way it runs, its section style and its size in DIPs.
struct BarGeometry {
    Axis axis;
    const SectionStyle& section;
    D2D1_SIZE_F size;
};

// How far across the bar a module's box may reach before its own margin: the bar's thickness inside the section's
// margin and insets.
float BoxCrossLength(const BarGeometry& bar);

// The section's box, inside the bar's margin.
D2D1_RECT_F SectionBounds(const BarGeometry& bar, const SectionLayout& layout);

// Where a module's box is drawn, and where its content starts, for routing clicks to it.
struct HitBox {
    IModule* module;
    D2D1_RECT_F bounds;
    D2D1_POINT_2F contentOrigin;
};

// One entry as placed for a frame: its box and, for a group, its toggle's span, its members' boxes in its members'
// order, and the span the fold uncovers of them, outside of which they neither draw nor take the cursor.
struct PlacedEntry {
    const ModuleEntry* entry;
    HitBox box;
    D2D1_RECT_F toggleBounds;
    std::vector<HitBox> members;
    D2D1_RECT_F shownBounds;
};

// Every module's box for a frame, and the sections' boxes they sit in.
struct PlacedBar {
    BarLayout layout;
    Sections<PlacedEntry> entries;
};

// Places every module of every section from the size each last laid out at.
PlacedBar PlaceBar(const BarGeometry& bar, const Sections<ModuleEntry>& modules);

// Where each module takes the cursor: a group's members before the group, since the first box holding a point wins, and
// of a member only what the fold uncovers.
std::vector<HitBox> HitBoxes(const Sections<PlacedEntry>& entries);

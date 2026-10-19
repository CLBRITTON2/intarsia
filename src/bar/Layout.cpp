#include "bar/Layout.hpp"

#include <iterator>
#include <numeric>
#include <utility>

namespace {

    // A section whose modules all show nothing has no box, as one with no modules.
    float SectionLength(const std::vector<float>& lengths, EndInsets insets) {
        const float modules = std::accumulate(lengths.begin(), lengths.end(), 0.0f);
        if (modules > 0.0f) {
            return modules + insets.start + insets.end;
        }
        return 0.0f;
    }

    SectionLayout ArrangeSection(const std::vector<float>& lengths, float start, EndInsets insets) {
        SectionLayout section{
            .start = start,
            .length = SectionLength(lengths, insets),
            .offsets = {},
        };
        section.offsets.reserve(lengths.size());
        std::exclusive_scan(lengths.begin(), lengths.end(), std::back_inserter(section.offsets), start + insets.start);
        return section;
    }

    // What a module that is not a group takes along the bar, nothing when it shows nothing.
    float ModuleBoxLength(Axis axis, const ModuleEntry& entry) {
        const float content = MainLength(entry.module->Size(), axis);
        if (content <= 0.0f) {
            return 0.0f;
        }
        const EndInsets insets = MainInsets(entry.style.box, axis);
        return content + insets.start + insets.end + (2.0f * MainOf(entry.style.margin, axis));
    }

    GroupLayout ArrangeMembers(Axis axis, const ModuleEntry& entry, const std::vector<float>& memberLengths) {
        return ArrangeGroup(MainLength(entry.module->Size(), axis), memberLengths, entry.group->Revealed(), MainInsets(entry.style.box, axis));
    }

    // Each module's room along the bar: what it draws plus its insets and margin at both ends. A module showing nothing,
    // such as a battery module with no battery, takes no room at all. A group's length takes in the revealed share of its
    // members'.
    std::vector<float> BoxLengths(Axis axis, const std::vector<ModuleEntry>& entries) {
        std::vector<float> lengths;
        lengths.reserve(entries.size());
        for (const ModuleEntry& entry : entries) {
            if (entry.group != nullptr) {
                lengths.push_back(ArrangeMembers(axis, entry, BoxLengths(axis, entry.members)).length + (2.0f * MainOf(entry.style.margin, axis)));
            } else {
                lengths.push_back(ModuleBoxLength(axis, entry));
            }
        }
        return lengths;
    }

    // The entry's box from start along the bar, spanning across it as every module's box does, less its margin.
    HitBox PlaceBox(const BarGeometry& bar, const ModuleEntry& entry, float start, float length) {
        const float margin = CrossOf(entry.style.margin, bar.axis);
        const float boxStart = CrossOf(bar.section.margin, bar.axis) + CrossInsets(bar.section.box, bar.axis).start + margin;
        const D2D1_RECT_F box = MainCrossRect(bar.axis, start, length, boxStart, BoxCrossLength(bar) - (2.0f * margin));
        return HitBox{
            .module = entry.module.get(),
            .bounds = box,
            .contentOrigin = ContentOrigin(bar.axis, box, ContentInsets(entry.style.box), entry.module->Size()),
        };
    }

    // The entry's box within the room BoxLengths gave it, past its margin at both ends.
    HitBox PlaceInRoom(const BarGeometry& bar, const ModuleEntry& entry, float start, float length) {
        const float margin = MainOf(entry.style.margin, bar.axis);
        return PlaceBox(bar, entry, start + margin, length - (2.0f * margin));
    }

    // The entry in the room from start that BoxLengths gave it.
    PlacedEntry PlaceEntry(const BarGeometry& bar, const ModuleEntry& entry, float start, float length) {
        PlacedEntry placed{
            .entry = &entry,
            .box = PlaceInRoom(bar, entry, start, length),
            .toggleBounds = {},
            .members = {},
            .shownBounds = {},
        };
        if (entry.group != nullptr) {
            const float boxStart = start + MainOf(entry.style.margin, bar.axis);
            const std::vector<float> memberLengths = BoxLengths(bar.axis, entry.members);
            const GroupLayout group = ArrangeMembers(bar.axis, entry, memberLengths);
            placed.toggleBounds = PlaceBox(bar, entry, boxStart, group.toggleLength).bounds;
            placed.shownBounds = PlaceBox(bar, entry, boxStart + group.toggleLength, group.shownLength).bounds;
            for (size_t member = 0; member < entry.members.size(); ++member) {
                placed.members.push_back(PlaceInRoom(bar, entry.members[member], boxStart + group.offsets[member], memberLengths[member]));
            }
        }
        return placed;
    }

    std::vector<PlacedEntry> PlaceSection(const BarGeometry& bar, const std::vector<ModuleEntry>& entries, const std::vector<float>& lengths, const SectionLayout& layout) {
        std::vector<PlacedEntry> placed;
        if (layout.length <= 0.0f) {
            return placed;
        }
        placed.reserve(entries.size());
        for (size_t index = 0; index < entries.size(); ++index) {
            placed.push_back(PlaceEntry(bar, entries[index], layout.offsets[index], lengths[index]));
        }
        return placed;
    }

} // namespace

BarLayout ArrangeBar(const Sections<float>& lengths, float barLength, float margin, EndInsets insets) {
    return BarLayout{
        .left = ArrangeSection(lengths.left, margin, insets),
        .center = ArrangeSection(lengths.center, (barLength - SectionLength(lengths.center, insets)) / 2.0f, insets),
        .right = ArrangeSection(lengths.right, barLength - margin - SectionLength(lengths.right, insets), insets),
    };
}

GroupLayout ArrangeGroup(float toggleLength, const std::vector<float>& memberLengths, float revealed, EndInsets insets) {
    const float members = std::accumulate(memberLengths.begin(), memberLengths.end(), 0.0f);
    const float shown = revealed * members;
    const float toggle = toggleLength + insets.start + insets.end;
    GroupLayout group{
        .length = toggle + shown,
        .toggleLength = toggle,
        .shownLength = shown,
        .offsets = {},
    };
    group.offsets.reserve(memberLengths.size());
    std::exclusive_scan(memberLengths.begin(), memberLengths.end(), std::back_inserter(group.offsets), toggle);
    return group;
}

EndInsets MainInsets(const BoxStyle& style, Axis axis) {
    const Insets insets = ContentInsets(style);
    if (axis == Axis::Horizontal) {
        return EndInsets{.start = insets.left, .end = insets.right};
    }
    return EndInsets{.start = insets.top, .end = insets.bottom};
}

EndInsets CrossInsets(const BoxStyle& style, Axis axis) {
    const Insets insets = ContentInsets(style);
    if (axis == Axis::Horizontal) {
        return EndInsets{.start = insets.top, .end = insets.bottom};
    }
    return EndInsets{.start = insets.left, .end = insets.right};
}

D2D1_POINT_2F ContentOrigin(Axis axis, const D2D1_RECT_F& box, const Insets& insets, D2D1_SIZE_F content) {
    if (axis == Axis::Horizontal) {
        return D2D1::Point2F(box.left + insets.left, box.top + insets.top + ((box.bottom - box.top - insets.top - insets.bottom - content.height) / 2.0f));
    }
    return D2D1::Point2F(box.left + insets.left + ((box.right - box.left - insets.left - insets.right - content.width) / 2.0f), box.top + insets.top);
}

float BoxCrossLength(const BarGeometry& bar) {
    const EndInsets insets = CrossInsets(bar.section.box, bar.axis);
    return CrossLength(bar.size, bar.axis) - (2.0f * CrossOf(bar.section.margin, bar.axis)) - insets.start - insets.end;
}

D2D1_RECT_F SectionBounds(const BarGeometry& bar, const SectionLayout& layout) {
    const float margin = CrossOf(bar.section.margin, bar.axis);
    return MainCrossRect(bar.axis, layout.start, layout.length, margin, CrossLength(bar.size, bar.axis) - (2.0f * margin));
}

PlacedBar PlaceBar(const BarGeometry& bar, const Sections<ModuleEntry>& modules) {
    const Sections<float> lengths{
        .left = BoxLengths(bar.axis, modules.left),
        .center = BoxLengths(bar.axis, modules.center),
        .right = BoxLengths(bar.axis, modules.right),
    };
    BarLayout layout = ArrangeBar(lengths, MainLength(bar.size, bar.axis), MainOf(bar.section.margin, bar.axis), MainInsets(bar.section.box, bar.axis));
    Sections<PlacedEntry> entries{
        .left = PlaceSection(bar, modules.left, lengths.left, layout.left),
        .center = PlaceSection(bar, modules.center, lengths.center, layout.center),
        .right = PlaceSection(bar, modules.right, lengths.right, layout.right),
    };
    return PlacedBar{.layout = std::move(layout), .entries = std::move(entries)};
}

std::vector<HitBox> HitBoxes(const Sections<PlacedEntry>& entries) {
    std::vector<HitBox> boxes;
    ForEachInSections(entries, [&boxes](const PlacedEntry& placed) {
        for (const HitBox& member : placed.members) {
            const D2D1_RECT_F shown = Intersection(member.bounds, placed.shownBounds);
            if (shown.right > shown.left && shown.bottom > shown.top) {
                boxes.push_back(HitBox{
                    .module = member.module,
                    .bounds = shown,
                    .contentOrigin = member.contentOrigin,
                });
            }
        }
        boxes.push_back(placed.box);
    });
    return boxes;
}

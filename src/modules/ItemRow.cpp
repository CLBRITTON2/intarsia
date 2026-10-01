#include "modules/ItemRow.hpp"

#include "bar/Box.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

ItemRow EmptyItemRow() {
    return ItemRow{.boxes = {}, .size = {.width = 0.0f, .height = 0.0f}, .axis = Axis::Horizontal};
}

ItemRow ArrangeItems(const std::vector<D2D1_SIZE_F>& contents, const std::vector<const ItemStyle*>& styles, Axis axis) {
    if (contents.size() != styles.size()) {
        throw std::invalid_argument(std::format("{} item contents with {} styles", contents.size(), styles.size()));
    }
    float cross = 0.0f;
    for (size_t index = 0; index < contents.size(); ++index) {
        cross = std::max(cross, CrossLength(contents[index], axis) + (2.0f * CrossOf(styles[index]->padding, axis)) + (2.0f * CrossOf(styles[index]->margin, axis)));
    }
    std::vector<D2D1_RECT_F> boxes;
    boxes.reserve(contents.size());
    float start = 0.0f;
    for (size_t index = 0; index < contents.size(); ++index) {
        const ItemStyle& style    = *styles[index];
        const float      boxMain  = MainLength(contents[index], axis) + (2.0f * MainOf(style.padding, axis));
        const float      boxCross = CrossLength(contents[index], axis) + (2.0f * CrossOf(style.padding, axis));
        start += MainOf(style.margin, axis);
        boxes.push_back(MainCrossRect(axis, start, boxMain, (cross - boxCross) / 2.0f, boxCross));
        start += boxMain + MainOf(style.margin, axis);
    }
    return ItemRow{.boxes = std::move(boxes), .size = MainCrossSize(axis, start, cross), .axis = axis};
}

float ItemTextInset(const ItemStyle& style, Axis axis) {
    return CrossOf(style.padding, axis) + CrossOf(style.margin, axis);
}

std::optional<size_t> ItemAt(const ItemRow& row, D2D1_POINT_2F point) {
    const auto box = std::ranges::find_if(row.boxes, [point](const D2D1_RECT_F& candidate) {
        return point.x >= candidate.left && point.x < candidate.right && point.y >= candidate.top && point.y < candidate.bottom;
    });
    if (box == row.boxes.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(box - row.boxes.begin());
}

D2D1_POINT_2F ItemContentOrigin(const D2D1_RECT_F& box, const ItemStyle& style, D2D1_POINT_2F origin) {
    return D2D1::Point2F(origin.x + box.left + style.padding.xDips, origin.y + box.top + style.padding.yDips);
}

void DrawItemBackground(const RenderContext& render, const D2D1_RECT_F& box, const ItemStyle& style, bool hovered, D2D1_POINT_2F origin) {
    const D2D1_RECT_F placed = D2D1::RectF(origin.x + box.left, origin.y + box.top, origin.x + box.right, origin.y + box.bottom);
    const BoxStyle    boxStyle{.background = style.background, .border = {}, .radiusDips = style.radiusDips, .padding = style.padding, .shadows = style.shadows};
    DrawBox(render.context, *render.brushes, placed, boxStyle, hovered ? style.hoverBackground : style.background);
}

const D2D1_COLOR_F& ItemColor(const ItemStyle& style, bool hovered) {
    return hovered ? style.hoverColor : style.color;
}

bool HoveredItem::Set(std::optional<size_t> item) {
    const bool changed = item != _item;
    _item              = item;
    return changed;
}

bool HoveredItem::Is(size_t index) const {
    return _item == index;
}

std::optional<size_t> HoveredItem::Get() const {
    return _item;
}

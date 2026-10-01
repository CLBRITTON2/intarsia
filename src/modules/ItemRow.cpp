#include "modules/ItemRow.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>

ItemRow ArrangeItems(const std::vector<D2D1_SIZE_F>& contents, const std::vector<const ItemStyle*>& styles) {
    if (contents.size() != styles.size()) {
        throw std::invalid_argument(std::format("{} item contents with {} styles", contents.size(), styles.size()));
    }
    float height = 0.0f;
    for (size_t index = 0; index < contents.size(); ++index) {
        height = std::max(height, contents[index].height + (2.0f * styles[index]->padding.yDips) + (2.0f * styles[index]->margin.yDips));
    }
    ItemRow row{.boxes = {}, .size = {.width = 0.0f, .height = height}};
    row.boxes.reserve(contents.size());
    float left = 0.0f;
    for (size_t index = 0; index < contents.size(); ++index) {
        const ItemStyle& style     = *styles[index];
        const float      boxWidth  = contents[index].width + (2.0f * style.padding.xDips);
        const float      boxHeight = contents[index].height + (2.0f * style.padding.yDips);
        const float      top       = (height - boxHeight) / 2.0f;
        left += style.margin.xDips;
        row.boxes.push_back(D2D1::RectF(left, top, left + boxWidth, top + boxHeight));
        left += boxWidth + style.margin.xDips;
    }
    row.size.width = left;
    return row;
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
    const D2D1_COLOR_F& fill = hovered ? style.hoverBackground : style.background;
    if (fill.a <= 0.0f) {
        return;
    }
    const D2D1_RECT_F placed = D2D1::RectF(origin.x + box.left, origin.y + box.top, origin.x + box.right, origin.y + box.bottom);
    const float       radius = std::min(style.radiusDips, (box.bottom - box.top) / 2.0f);
    render.context->FillRoundedRectangle(D2D1::RoundedRect(placed, radius, radius), render.brushes->Get(fill));
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

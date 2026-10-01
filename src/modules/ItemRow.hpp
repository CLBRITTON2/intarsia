#pragma once

#include "config/Config.hpp"
#include "render/RenderContext.hpp"

#include <d2d1.h>

#include <cstddef>
#include <optional>
#include <vector>

// Item boxes side by side: each is its content plus its style's padding, with its style's margin around it, centered
// across the row's height.
struct ItemRow {
    std::vector<D2D1_RECT_F> boxes; // from the row's origin
    D2D1_SIZE_F              size;
};

// One box per content, in the style at the same index.
ItemRow ArrangeItems(const std::vector<D2D1_SIZE_F>& contents, const std::vector<const ItemStyle*>& styles);

// The box under point, measured from the row's origin, or nothing in a margin.
std::optional<size_t> ItemAt(const ItemRow& row, D2D1_POINT_2F point);

// Where a box's content starts once the row is drawn at origin.
D2D1_POINT_2F ItemContentOrigin(const D2D1_RECT_F& box, const ItemStyle& style, D2D1_POINT_2F origin);

// The box filled with the style's background, or its hover background while hovered, at origin, unless that fill is
// transparent, rounded by the style's radius up to half its height.
void DrawItemBackground(const RenderContext& render, const D2D1_RECT_F& box, const ItemStyle& style, bool hovered, D2D1_POINT_2F origin);

// The style's text color, or its hover color while hovered.
const D2D1_COLOR_F& ItemColor(const ItemStyle& style, bool hovered);

// The item under the cursor, held between a module's hovers.
class HoveredItem {
  public:
    // Returns whether item differs from the one held before.
    bool                  Set(std::optional<size_t> item);
    bool                  Is(size_t index) const;
    std::optional<size_t> Get() const;

  private:
    std::optional<size_t> _item;
};

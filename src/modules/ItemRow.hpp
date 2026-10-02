#pragma once

#include "config/Config.hpp"
#include "modules/AnimatedValue.hpp"
#include "render/RenderContext.hpp"

#include <d2d1.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// Item boxes one after another along the bar's axis: each is its content plus its style's padding, with its style's
// margin around it, centered across the row.
struct ItemRow {
    std::vector<D2D1_RECT_F> boxes; // from the row's origin
    D2D1_SIZE_F              size;
    Axis                     axis;
};

// Nothing arranged yet.
ItemRow EmptyItemRow();

// One box per content, in the style at the same index.
ItemRow ArrangeItems(const std::vector<D2D1_SIZE_F>& contents, const std::vector<const ItemStyle*>& styles, Axis axis);

// How far an item's text sits in from each side of its module's box across the bar: its padding and margin.
float ItemTextInset(const ItemStyle& style, Axis axis);

// The box under point, measured from the row's origin, or nothing in a margin.
std::optional<size_t> ItemAt(const ItemRow& row, D2D1_POINT_2F point);

// Where a box's content starts once the row is drawn at origin.
D2D1_POINT_2F ItemContentOrigin(const D2D1_RECT_F& box, const ItemStyle& style, D2D1_POINT_2F origin);

// The box filled with fill at origin, unless fill is transparent, with shadows, rounded by the style's radius up to half
// its shorter side.
void DrawItemBackground(const RenderContext& render, const D2D1_RECT_F& box, const ItemStyle& style, const D2D1_COLOR_F& fill, const std::vector<InsetShadow>& shadows,
                        D2D1_POINT_2F origin);

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

// The colors and shadows each item of a row shows, each fading into its style's (its hover colors under the cursor)
// over that style's transitions, the shadows over the background's. Items are told apart by index alone, so an item
// moving into another's place fades from that item's.
class ItemFades {
  public:
    // A new index starts at its style's colors, as an item that just appeared has nothing to fade from.
    void                     Follow(const std::vector<const ItemStyle*>& styles, const HoveredItem& hovered, std::chrono::steady_clock::time_point now);
    D2D1_COLOR_F             Background(size_t index, std::chrono::steady_clock::time_point now) const;
    D2D1_COLOR_F             Text(size_t index, std::chrono::steady_clock::time_point now) const;
    std::vector<InsetShadow> Shadows(size_t index, std::chrono::steady_clock::time_point now) const;
    // A module's MillisecondsUntilChange: 0 while an item fades, so the bar draws every frame, and none after.
    std::optional<std::uint32_t> Wait(std::chrono::steady_clock::time_point now) const;

  private:
    struct Fade {
        ColorFade  background;
        ColorFade  text;
        ShadowFade shadows;
    };
    std::vector<Fade> _fades;
};

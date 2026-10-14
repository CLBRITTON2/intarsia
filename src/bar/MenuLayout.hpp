#pragma once

#include "bar/Tooltip.hpp"
#include "config/Config.hpp"

#include <d2d1_1.h>
#include <dwrite.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <optional>
#include <vector>

// One row of a shown menu, in DIPs from the menu's top left.
struct MenuRow {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> text; // null for a separator
    D2D1_RECT_F                               box;
    D2D1_POINT_2F                             textOrigin;
    const ItemStyle*                          style;
    std::optional<size_t>                     entry; // the entry picking the row selects, null for a failure's row or a separator
};

enum class MenuStep : std::uint8_t {
    Up,
    Down
};

// How far a menu's rows have scrolled through the part of its box that shows them, in DIPs. Rows lay out as if
// unscrolled, from the viewport's top.
struct MenuScroll {
    D2D1_RECT_F viewport;    // the box inside its border and padding
    float       contentDips; // the rows' height, margins included
    float       offsetDips;  // how far the rows have moved up, 0 at the top
};

// A menu's rows placed down its box and stretched to the widest, and the window that holds them.
struct MenuLayout {
    std::vector<MenuRow> rows;
    SIZE                 size;   // the window, in pixels, never larger than its monitor
    MenuScroll           scroll; // at the top
    D2D1_RECT_F          gutter; // beside the viewport, where the track takes clicks
    D2D1_RECT_F          track;
};

// The widest row's text may lay out in a menu on monitorRect before its ellipsis, capped by style's max-width.
float MenuTextWidth(const MenuStyle& style, const ItemStyle& row, const RECT& monitorRect, std::uint32_t dpi);

// rows, each sized to its text with its box at the origin, stacked inside style's box. Past max-height or the monitor's
// height the window stops growing, the rows scroll and the scroll bar takes its gutter.
MenuLayout LayOutMenu(std::vector<MenuRow> rows, const MenuStyle& style, const RECT& monitorRect, std::uint32_t dpi);

// scroll at offsetDips, kept between the top and the offset that brings the last row to the viewport's bottom.
MenuScroll ScrolledTo(const MenuScroll& scroll, float offsetDips);

// scroll moved the least that shows all of row, which lays out unscrolled.
MenuScroll ScrolledToShow(const MenuScroll& scroll, const D2D1_RECT_F& row);

// The scroll bar's thumb along track, sized by how much of the rows the viewport shows, or nullopt while they all fit.
std::optional<D2D1_RECT_F> ThumbRect(const MenuScroll& scroll, const D2D1_RECT_F& track);

// scroll at the offset that puts the thumb's top at thumbTopDips, as dragging it does.
MenuScroll ScrolledToThumb(const MenuScroll& scroll, const D2D1_RECT_F& track, float thumbTopDips);

// The entry whose row shows under point, or nullopt over the menu's box, a row scrolled out of view or a failure's row.
std::optional<size_t> EntryAt(const std::vector<MenuRow>& rows, const MenuScroll& scroll, D2D1_POINT_2F point);

// The entry an arrow key moves the highlight to among count entries, wrapping at either end. With none highlighted,
// Down goes to the first and Up to the last.
std::optional<size_t> StepHighlight(std::optional<size_t> current, size_t count, MenuStep step);

// Where an entry's tooltip goes: over the menu, centered on the row and against its side with more of the monitor
// below or above it.
TooltipAnchor RowTooltipAnchor(const RECT& row, const RECT& monitorRect, std::uint32_t dpi);

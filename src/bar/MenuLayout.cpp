#include "bar/MenuLayout.hpp"

#include "bar/Box.hpp"
#include "render/Axis.hpp"
#include "render/Graphics.hpp"

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>

namespace {

    // Short enough for hundreds of entries, long enough to grab.
    constexpr float kMinThumbDips = 16.0f;

    float           MaxOffset(const MenuScroll& scroll) {
        return std::max(0.0f, scroll.contentDips - (scroll.viewport.bottom - scroll.viewport.top));
    }

    float GutterDips(const ScrollbarStyle& scrollbar) {
        if (scrollbar.widthDips > 0.0f) {
            return scrollbar.widthDips + (2.0f * scrollbar.margin.xDips);
        }
        return 0.0f;
    }

    // The width the scroll bar's gutter adds to the window: all of it once the rows' height passes maxHeight, else none.
    float TakenGutterDips(float height, float maxHeight, float gutterDips) {
        if (height > maxHeight) {
            return gutterDips;
        }
        return 0.0f;
    }

    // The window's height: the rows' height, cut to maxHeight past which the rows scroll.
    float ShownHeightDips(float height, float maxHeight) {
        if (height > maxHeight) {
            return maxHeight;
        }
        return height;
    }

    // The smaller of a monitor's length and the style's cap, as a command can print text of any length and up to the
    // entry limit.
    float CappedDips(std::int32_t monitorPixels, std::uint32_t dpi, std::optional<float> capDips) {
        return std::min(PixelsToDips(static_cast<float>(monitorPixels), dpi), capDips.value_or(std::numeric_limits<float>::max()));
    }

    // The bar edge the row stands in for: Top, which puts the tooltip below the row, where the monitor has at least as
    // much room below it as above.
    Edge RowBarEdge(const RECT& row, const RECT& monitorRect) {
        if (monitorRect.bottom - row.bottom >= row.top - monitorRect.top) {
            return Edge::Top;
        }
        return Edge::Bottom;
    }

} // namespace

float MenuTextWidth(const MenuStyle& style, const ItemStyle& row, const RECT& monitorRect, std::uint32_t dpi) {
    const Insets insets = ContentInsets(style.box);
    // Leaves room for the scroll bar whether or not the rows overflow, so a cut row never grows once they do.
    return CappedDips(monitorRect.right - monitorRect.left, dpi, style.maxWidthDips) - GutterDips(style.scrollbar) - insets.left - insets.right -
        (2.0f * (row.margin.xDips + row.padding.xDips));
}

MenuLayout LayOutMenu(std::vector<MenuRow> rows, const MenuStyle& style, const RECT& monitorRect, std::uint32_t dpi) {
    const ScrollbarStyle& scrollbar     = style.scrollbar;
    const Insets          insets        = ContentInsets(style.box);
    const float           gutter        = GutterDips(scrollbar);
    const std::int32_t    monitorWidth  = monitorRect.right - monitorRect.left;
    const std::int32_t    monitorHeight = monitorRect.bottom - monitorRect.top;
    const float           maxHeight     = CappedDips(monitorHeight, dpi, style.maxHeightDips);

    float                 inner = 0.0f;
    for (const MenuRow& row : rows) {
        inner = std::max(inner, row.box.right + (2.0f * row.style->margin.xDips));
    }
    float bottom = insets.top;
    for (MenuRow& row : rows) {
        const Spacing& margin  = row.style->margin;
        const Spacing& padding = row.style->padding;
        const float    top     = bottom + margin.yDips;
        row.box                = D2D1::RectF(insets.left + margin.xDips, top, insets.left + inner - margin.xDips, top + row.box.bottom);
        row.textOrigin         = D2D1::Point2F(row.box.left + padding.xDips, row.box.top + padding.yDips);
        bottom                 = row.box.bottom + margin.yDips;
    }

    const float       content = bottom - insets.top;
    const float       height  = content + insets.top + insets.bottom;
    const SIZE        size{.cx = std::min(DipsToWholePixels(inner + insets.left + insets.right + TakenGutterDips(height, maxHeight, gutter), dpi), monitorWidth),
                           .cy = std::min(DipsToWholePixels(ShownHeightDips(height, maxHeight), dpi), monitorHeight)};
    const D2D1_RECT_F viewport = D2D1::RectF(insets.left, insets.top, insets.left + inner, PixelsToDips(static_cast<float>(size.cy), dpi) - insets.bottom);
    return MenuLayout{
        .rows = std::move(rows),
        .size = size,
        .scroll =
            MenuScroll{
                .viewport    = viewport,
                .contentDips = content,
                .offsetDips  = 0.0f,
            },
        .gutter = D2D1::RectF(viewport.right, viewport.top, viewport.right + gutter, viewport.bottom),
        .track  = D2D1::RectF(viewport.right + scrollbar.margin.xDips, viewport.top + scrollbar.margin.yDips, viewport.right + scrollbar.margin.xDips + scrollbar.widthDips,
                              viewport.bottom - scrollbar.margin.yDips),
    };
}

MenuScroll ScrolledTo(const MenuScroll& scroll, float offsetDips) {
    return MenuScroll{
        .viewport    = scroll.viewport,
        .contentDips = scroll.contentDips,
        .offsetDips  = std::clamp(offsetDips, 0.0f, MaxOffset(scroll)),
    };
}

MenuScroll ScrolledToShow(const MenuScroll& scroll, const D2D1_RECT_F& row) {
    const float above = row.top - scroll.viewport.top;
    const float below = row.bottom - scroll.viewport.bottom;
    // A row taller than the viewport shows its top.
    return ScrolledTo(scroll, std::min(std::max(scroll.offsetDips, below), above));
}

std::optional<D2D1_RECT_F> ThumbRect(const MenuScroll& scroll, const D2D1_RECT_F& track) {
    const float maxOffset = MaxOffset(scroll);
    if (maxOffset <= 0.0f) {
        return std::nullopt;
    }
    const float trackLength = track.bottom - track.top;
    const float shown       = (scroll.viewport.bottom - scroll.viewport.top) / scroll.contentDips;
    const float thumbLength = std::min(trackLength, std::max(kMinThumbDips, trackLength * shown));
    const float top         = track.top + ((trackLength - thumbLength) * scroll.offsetDips / maxOffset);
    return D2D1::RectF(track.left, top, track.right, top + thumbLength);
}

MenuScroll ScrolledToThumb(const MenuScroll& scroll, const D2D1_RECT_F& track, float thumbTopDips) {
    const std::optional<D2D1_RECT_F> thumb = ThumbRect(scroll, track);
    if (!thumb.has_value()) {
        return scroll;
    }
    const float travel = (track.bottom - track.top) - (thumb->bottom - thumb->top);
    if (travel <= 0.0f) {
        return scroll;
    }
    return ScrolledTo(scroll, (thumbTopDips - track.top) / travel * MaxOffset(scroll));
}

std::optional<size_t> EntryAt(const std::vector<MenuRow>& rows, const MenuScroll& scroll, D2D1_POINT_2F point) {
    if (!Contains(scroll.viewport, point)) {
        return std::nullopt;
    }
    const D2D1_POINT_2F unscrolled = D2D1::Point2F(point.x, point.y + scroll.offsetDips);
    const auto          row = std::ranges::find_if(rows, [unscrolled](const MenuRow& candidate) { return candidate.entry.has_value() && Contains(candidate.box, unscrolled); });
    if (row == rows.end()) {
        return std::nullopt;
    }
    return row->entry;
}

std::optional<size_t> StepHighlight(std::optional<size_t> current, size_t count, MenuStep step) {
    if (count == 0) {
        return std::nullopt;
    }
    if (!current.has_value()) {
        if (step == MenuStep::Down) {
            return 0;
        }
        return count - 1;
    }
    if (step == MenuStep::Down) {
        return (*current + 1) % count;
    }
    if (*current == 0) {
        return count - 1;
    }
    return *current - 1;
}

// The tooltip's rect treats the row as a bar on the monitor's top or bottom edge, placing it below or above the row.
TooltipAnchor RowTooltipAnchor(const RECT& row, const RECT& monitorRect, std::uint32_t dpi) {
    return TooltipAnchor{
        .target      = row,
        .barRect     = row,
        .monitorRect = monitorRect,
        .edge        = RowBarEdge(row, monitorRect),
        .dpi         = dpi,
    };
}

#include "modules/ItemRow.hpp"

#include "render/Axis.hpp"
#include "render/Box.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {

    struct ItemColors {
        D2D1_COLOR_F background;
        D2D1_COLOR_F text;
    };

    // The colors an item fades to: its hover colors under the cursor.
    ItemColors TargetColors(const ItemStyle& style, bool hovered) {
        if (hovered) {
            return ItemColors{.background = style.hoverBackground, .text = style.hoverColor};
        }
        return ItemColors{.background = style.background, .text = style.color};
    }

} // namespace

ItemRow EmptyItemRow() {
    return ItemRow{
        .boxes = {},
        .size = {.width = 0.0f, .height = 0.0f},
        .axis = Axis::Horizontal,
    };
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
        const ItemStyle& style = *styles[index];
        const float boxMain = MainLength(contents[index], axis) + (2.0f * MainOf(style.padding, axis));
        const float boxCross = CrossLength(contents[index], axis) + (2.0f * CrossOf(style.padding, axis));
        start += MainOf(style.margin, axis);
        boxes.push_back(MainCrossRect(axis, start, boxMain, (cross - boxCross) / 2.0f, boxCross));
        start += boxMain + MainOf(style.margin, axis);
    }
    return ItemRow{
        .boxes = std::move(boxes),
        .size = MainCrossSize(axis, start, cross),
        .axis = axis,
    };
}

float ItemTextInset(const ItemStyle& style, Axis axis) {
    return CrossOf(style.padding, axis) + CrossOf(style.margin, axis);
}

std::optional<size_t> ItemAt(const ItemRow& row, D2D1_POINT_2F point) {
    const auto box = std::ranges::find_if(row.boxes, [point](const D2D1_RECT_F& candidate) { return Contains(candidate, point); });
    if (box == row.boxes.end()) {
        return std::nullopt;
    }
    return static_cast<size_t>(box - row.boxes.begin());
}

std::optional<size_t> ItemUnderCursor(const ItemRow& row, std::optional<D2D1_POINT_2F> cursor) {
    if (!cursor.has_value()) {
        return std::nullopt;
    }
    return ItemAt(row, *cursor);
}

D2D1_POINT_2F ItemContentOrigin(const D2D1_RECT_F& box, const ItemStyle& style, D2D1_POINT_2F origin) {
    return D2D1::Point2F(origin.x + box.left + style.padding.xDips, origin.y + box.top + style.padding.yDips);
}

void DrawItemBackground(const RenderContext& render, const D2D1_RECT_F& box, const ItemStyle& style, const D2D1_COLOR_F& fill, const std::vector<InsetShadow>& shadows,
                        D2D1_POINT_2F origin) {
    const D2D1_RECT_F placed = Offset(box, origin);
    const BoxStyle boxStyle{
        .background = style.background,
        .border = {},
        .radiusDips = style.radiusDips,
        .padding = style.padding,
        .shadows = shadows,
    };
    DrawBox(render.context, *render.brushes, *render.boxes, placed, boxStyle, fill);
}

bool HoveredItem::Set(std::optional<size_t> item) {
    const bool changed = item != _item;
    _item = item;
    return changed;
}

bool HoveredItem::Is(size_t index) const {
    return _item == index;
}

std::optional<size_t> HoveredItem::Get() const {
    return _item;
}

void ItemFades::Follow(const std::vector<const ItemStyle*>& styles, const HoveredItem& hover, std::chrono::steady_clock::time_point now) {
    if (_fades.size() > styles.size()) {
        _fades.erase(_fades.begin() + static_cast<std::ptrdiff_t>(styles.size()), _fades.end());
    }
    for (size_t index = 0; index < styles.size(); ++index) {
        const ItemStyle& style = *styles[index];
        const ItemColors colors = TargetColors(style, hover.Is(index));
        if (index == _fades.size()) {
            _fades.push_back(Fade{
                .background = ColorFade(colors.background),
                .text = ColorFade(colors.text),
                .shadows = ShadowFade(style.shadows),
            });
            continue;
        }
        _fades[index].background.Retarget(colors.background, style.backgroundTransition, now);
        _fades[index].text.Retarget(colors.text, style.colorTransition, now);
        _fades[index].shadows.Retarget(style.shadows, style.backgroundTransition, now);
    }
}

D2D1_COLOR_F ItemFades::Background(size_t index, std::chrono::steady_clock::time_point now) const {
    return _fades.at(index).background.Color(now);
}

D2D1_COLOR_F ItemFades::Text(size_t index, std::chrono::steady_clock::time_point now) const {
    return _fades.at(index).text.Color(now);
}

std::vector<InsetShadow> ItemFades::Shadows(size_t index, std::chrono::steady_clock::time_point now) const {
    return _fades.at(index).shadows.Shadows(now);
}

std::optional<std::uint32_t> ItemFades::Wait(std::chrono::steady_clock::time_point now) const {
    if (std::ranges::any_of(_fades, [now](const Fade& fade) { return fade.background.Running(now) || fade.text.Running(now) || fade.shadows.Running(now); })) {
        return 0;
    }
    return std::nullopt;
}

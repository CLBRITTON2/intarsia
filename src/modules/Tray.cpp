#include "modules/Tray.hpp"

#include "render/Icon.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <utility>

Tray::Tray(TrayConfig config, ITray& tray, ModuleStyle style) : _config(std::move(config)), _tray(tray), _style(std::move(style)), _toggle(EmptyIconText()) {}

bool Tray::Layout(const RenderContext& render) {
    const auto  now     = std::chrono::steady_clock::now();
    const float reveal  = _reveal.Value(now);
    const float angle   = _style.icon.rotationDegrees + ((_style.expandedIconRotationDegrees - _style.icon.rotationDegrees) * _turn.Value(now));
    bool        changed = !_laidOut || reveal != _revealShown || angle != _angleShown;
    _revealShown        = reveal;
    _angleShown         = angle;

    // Folding keeps the icons until they are out of sight.
    const std::vector<TrayIcon> shown = reveal > 0.0f ? ShownIcons(_tray.Icons()) : std::vector<TrayIcon>{};
    changed                           = changed || shown.size() != _slots.size();
    std::vector<Slot> slots;
    slots.reserve(shown.size());
    for (size_t index = 0; index < shown.size(); ++index) {
        // A bitmap already made from the same pixels is kept: making one costs an upload to the GPU.
        const auto kept   = std::ranges::find_if(_slots, [&](const Slot& slot) { return slot.icon.pixels == shown[index].pixels; });
        auto       bitmap = kept != _slots.end() ? kept->bitmap : CreateIconBitmap(render.context, *shown[index].pixels);
        // changed is already true when the counts differ, so _slots[index] is only read while it exists.
        changed = changed || _slots[index].icon.pixels != shown[index].pixels;
        slots.push_back(Slot{.icon = shown[index], .bitmap = std::move(bitmap)});
    }
    _slots = std::move(slots);

    if (!_laidOut) {
        _toggle = LayOutIconText(render, _config.icon, L"");
    }
    _laidOut = true;

    const float              icon = _style.icon.sizeDips;
    std::vector<D2D1_SIZE_F> contents(_slots.size(), D2D1_SIZE_F{.width = icon, .height = icon});
    contents.push_back(_toggle.size);
    _row = ArrangeItems(contents, std::vector<const ItemStyle*>(contents.size(), &_style.item));
    return changed;
}

D2D1_SIZE_F Tray::Size() const {
    return D2D1_SIZE_F{.width = _row.size.width - ToggleShift(), .height = _row.size.height};
}

void Tray::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const ItemStyle& style = _style.item;
    const float      icon  = _style.icon.sizeDips;
    if (!_slots.empty()) {
        // The icons stay where the unfolded row puts them, and the box's edge uncovers them.
        render.context->PushAxisAlignedClip(D2D1::RectF(origin.x, origin.y, origin.x + RevealedWidth(), origin.y + _row.size.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t index = 0; index < _slots.size(); ++index) {
            DrawItemBackground(render, _row.boxes[index], style, _hovered == index ? style.hoverBackground : style.background, origin);
            const D2D1_POINT_2F start = ItemContentOrigin(_row.boxes[index], style, origin);
            render.context->DrawBitmap(_slots[index].bitmap.Get(), D2D1::RectF(start.x, start.y, start.x + icon, start.y + icon), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        render.context->PopAxisAlignedClip();
    }

    const size_t        toggle  = _slots.size();
    const D2D1_POINT_2F shifted = D2D1::Point2F(origin.x - ToggleShift(), origin.y);
    DrawItemBackground(render, _row.boxes[toggle], style, _hovered == toggle ? style.hoverBackground : style.background, shifted);
    const D2D1_POINT_2F start  = ItemContentOrigin(_row.boxes[toggle], style, shifted);
    const D2D1_POINT_2F center = D2D1::Point2F(start.x + (_toggle.size.width / 2.0f), start.y + (_toggle.size.height / 2.0f));
    D2D1_MATRIX_3X2_F   transform{};
    render.context->GetTransform(&transform);
    render.context->SetTransform(D2D1::Matrix3x2F::Rotation(_angleShown, center) * *D2D1::Matrix3x2F::ReinterpretBaseType(&transform));
    DrawIconText(render, start, _toggle, render.foreground);
    render.context->SetTransform(transform);
}

std::optional<std::uint32_t> Tray::MillisecondsUntilChange() const {
    const auto now = std::chrono::steady_clock::now();
    if (_reveal.Running(now) || _turn.Running(now)) {
        return 0;
    }
    return std::nullopt;
}

void Tray::Click(D2D1_POINT_2F point) {
    if (ToggleAt(point)) {
        const auto now = std::chrono::steady_clock::now();
        _expanded      = !_expanded;
        _reveal.Retarget(_expanded ? 1.0f : 0.0f, _style.transition, now);
        _turn.Retarget(_expanded ? 1.0f : 0.0f, _style.icon.transition, now);
        // The items move, so what was under the cursor no longer is until it moves again.
        _hovered = std::nullopt;
    } else if (const Slot* slot = SlotAt(point)) {
        _tray.Press(slot->icon, TrayButton::Left);
    }
}

void Tray::RightClick(D2D1_POINT_2F point) {
    if (const Slot* slot = SlotAt(point)) {
        _tray.Press(slot->icon, TrayButton::Right);
    }
}

void Tray::Scroll(ScrollDirection) {}

bool Tray::Hover(std::optional<D2D1_POINT_2F> point) {
    const std::optional<size_t> hovered = point.has_value() ? ItemUnder(*point) : std::nullopt;
    const bool                  changed = hovered != _hovered;
    _hovered                            = hovered;
    return changed;
}

const ModuleActions& Tray::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> Tray::TooltipText() {
    if (!_hovered.has_value() || *_hovered >= _slots.size() || _slots[*_hovered].icon.tooltip.empty()) {
        return std::nullopt;
    }
    return _slots[*_hovered].icon.tooltip;
}

std::optional<D2D1_RECT_F> Tray::TooltipItem() const {
    if (!_hovered.has_value() || *_hovered >= _slots.size()) {
        return std::nullopt;
    }
    return _row.boxes[*_hovered];
}

// The icons end where the toggle's margin begins.
float Tray::IconsWidth() const {
    return _row.boxes.empty() ? 0.0f : _row.boxes.back().left - _style.item.margin.xDips;
}

float Tray::RevealedWidth() const {
    return IconsWidth() * _revealShown;
}

float Tray::ToggleShift() const {
    return IconsWidth() - RevealedWidth();
}

D2D1_POINT_2F Tray::RowPoint(D2D1_POINT_2F point) const {
    return point.x < RevealedWidth() ? point : D2D1::Point2F(point.x + ToggleShift(), point.y);
}

std::optional<size_t> Tray::ItemUnder(D2D1_POINT_2F point) const {
    return ItemAt(_row, RowPoint(point));
}

const Tray::Slot* Tray::SlotAt(D2D1_POINT_2F point) const {
    const std::optional<size_t> index = ItemUnder(point);
    return index.has_value() && *index < _slots.size() ? &_slots[*index] : nullptr;
}

bool Tray::ToggleAt(D2D1_POINT_2F point) const {
    return ItemUnder(point) == _slots.size();
}

std::vector<TrayIcon> ShownIcons(const std::vector<TrayIcon>& icons) {
    std::vector<TrayIcon> shown;
    std::ranges::copy_if(icons, std::back_inserter(shown),
                         [](const TrayIcon& icon) { return !icon.hidden && icon.pixels != nullptr && icon.pixels->width > 0 && icon.pixels->height > 0; });
    return shown;
}

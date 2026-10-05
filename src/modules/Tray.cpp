#include "modules/Tray.hpp"

#include "render/Axis.hpp"
#include "render/Graphics.hpp"
#include "render/Icon.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <utility>

namespace {

    // Icons change as their apps modify them, so the same icon is the same owner and id.
    bool SameIcon(const TrayIcon& first, const TrayIcon& second) {
        return first.owner == second.owner && first.id == second.id;
    }

    bool SamePlace(const TrayIconRect& first, const TrayIconRect& second) {
        return first.owner == second.owner && first.id == second.id && EqualRect(&first.rect, &second.rect) != FALSE;
    }

} // namespace

Tray::Tray(TrayConfig config, ITray& tray, ModuleStyle style) : _config(std::move(config)), _tray(tray), _style(std::move(style)), _fold(_style), _toggle(EmptyIconText()) {}

Tray::~Tray() {
    _tray.ReportIconRects(this, {});
}

bool Tray::Layout(const RenderContext& render) {
    if (!LayOutItems(render)) {
        return false;
    }
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return true;
}

bool Tray::LayOutItems(const RenderContext& render) {
    const FoldPosition position = _fold.At(std::chrono::steady_clock::now());
    const float        revealed = position.revealed;
    bool               changed  = !_laidOut || revealed != _revealedShown || position.angleDegrees != _angleShown;
    _revealedShown              = revealed;
    _angleShown                 = position.angleDegrees;

    // Folded, the icons wait out of sight as last shown, so unfolding uploads only those that are new or redrawn since.
    const size_t count = _slots.size();
    if (revealed > 0.0f) {
        const std::vector<TrayIcon> shown = ShownIcons(_tray.Icons());
        if (!std::ranges::equal(shown, _slots, {}, {}, &Slot::icon)) {
            changed = changed || shown.size() != _slots.size();
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
        }
    }

    // Every icon is the same size, so the row only moves when their count does.
    if (_laidOut && _slots.size() == count) {
        return changed;
    }
    if (!_laidOut) {
        _toggle = LayOutIconText(render, _config.icon, L"");
    }
    _laidOut = true;

    const float              icon = _style.icon.sizeDips;
    std::vector<D2D1_SIZE_F> contents(_slots.size(), D2D1_SIZE_F{.width = icon, .height = icon});
    contents.push_back(_toggle.size);
    _row = ArrangeItems(contents, ItemStyles(), render.axis);
    return changed;
}

std::vector<const ItemStyle*> Tray::ItemStyles() const {
    std::vector<const ItemStyle*> styles(_slots.size(), &_style.item);
    styles.push_back(&_style.toggle);
    return styles;
}

D2D1_SIZE_F Tray::Size() const {
    return MainCrossSize(_row.axis, MainLength(_row.size, _row.axis) - ToggleShift(), CrossLength(_row.size, _row.axis));
}

void Tray::Place(const ModulePlacement& placement) {
    std::vector<TrayIconRect> rects;
    const float               revealed = RevealedLength();
    const D2D1_POINT_2F       origin   = placement.contentOrigin;
    for (size_t index = 0; index < _slots.size() && MainEnd(_row.boxes[index], _row.axis) <= revealed; ++index) {
        const D2D1_RECT_F& box = _row.boxes[index];
        rects.push_back(TrayIconRect{
            .owner = _slots[index].icon.owner,
            .id    = _slots[index].icon.id,
            .rect  = DipsToScreen(placement.windowRect, Offset(box, origin), placement.dpi),
        });
    }
    if (!std::ranges::equal(rects, _placed, SamePlace)) {
        _placed = rects;
        _tray.ReportIconRects(this, std::move(rects));
    }
}

void Tray::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    const ItemStyle&                            style = _style.item;
    const float                                 icon  = _style.icon.sizeDips;
    const std::chrono::steady_clock::time_point now   = std::chrono::steady_clock::now();
    if (_revealedShown > 0.0f) {
        // The icons stay where the unfolded row puts them, and the box's edge uncovers them.
        const D2D1_SIZE_F revealed = MainCrossSize(_row.axis, RevealedLength(), CrossLength(_row.size, _row.axis));
        render.context->PushAxisAlignedClip(D2D1::RectF(origin.x, origin.y, origin.x + revealed.width, origin.y + revealed.height), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        for (size_t index = 0; index < _slots.size(); ++index) {
            DrawItemBackground(render, _row.boxes[index], style, _fades.Background(index, now), _fades.Shadows(index, now), origin);
            const D2D1_POINT_2F start = ItemContentOrigin(_row.boxes[index], style, origin);
            render.context->DrawBitmap(_slots[index].bitmap.Get(), D2D1::RectF(start.x, start.y, start.x + icon, start.y + icon), 1.0f, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        }
        render.context->PopAxisAlignedClip();
    }

    const ItemStyle&    toggleStyle = _style.toggle;
    const size_t        toggle      = _slots.size();
    const D2D1_POINT_2F shifted     = MovedAlong(origin, -ToggleShift(), _row.axis);
    DrawItemBackground(render, _row.boxes[toggle], toggleStyle, _fades.Background(toggle, now), _fades.Shadows(toggle, now), shifted);
    DrawTurnedIconText(render, ItemContentOrigin(_row.boxes[toggle], toggleStyle, shifted), _toggle, _angleShown, render.accent, render.foreground);
}

std::optional<std::uint32_t> Tray::MillisecondsUntilChange() const {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if (_fold.Running(now)) {
        return 0;
    }
    return _fades.Wait(now);
}

void Tray::Click(D2D1_POINT_2F point) {
    if (ToggleAt(point)) {
        _fold.Toggle(std::chrono::steady_clock::now());
    } else if (const Slot* slot = SlotAt(point)) {
        _tray.Press(slot->icon, TrayButton::Left);
    }
}

void Tray::RightClick(D2D1_POINT_2F point) {
    if (const Slot* slot = SlotAt(point)) {
        _tray.Press(slot->icon, TrayButton::Right);
    }
}

void Tray::MiddleClick(D2D1_POINT_2F point) {
    if (const Slot* slot = SlotAt(point)) {
        _tray.Press(slot->icon, TrayButton::Middle);
    }
}

bool Tray::Hover(std::optional<D2D1_POINT_2F> point) {
    const bool  changed = _hovered.Set(point.has_value() ? ItemUnder(*point) : std::nullopt);
    const Slot* slot    = point.has_value() ? SlotAt(*point) : nullptr;
    if (_popup.has_value() && (slot == nullptr || !SameIcon(slot->icon, *_popup))) {
        _tray.ClosePopup(*_popup);
        _popup.reset();
    }
    if (slot != nullptr) {
        _tray.Move(slot->icon);
    }
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return changed;
}

const ModuleActions& Tray::Actions() const {
    return kNoActions;
}

std::optional<std::wstring> Tray::TooltipText() {
    const std::optional<size_t> hovered = _hovered.Get();
    if (!hovered.has_value() || *hovered >= _slots.size()) {
        return std::nullopt;
    }
    const TrayIcon& icon = _slots[*hovered].icon;
    if (DrawsOwnPopup(icon)) {
        if (!_popup.has_value()) {
            _tray.OpenPopup(icon);
            _popup = icon;
        }
        return std::nullopt;
    }
    if (icon.tooltip.empty()) {
        return std::nullopt;
    }
    return icon.tooltip;
}

std::optional<D2D1_RECT_F> Tray::TooltipItemBox() const {
    const std::optional<size_t> hovered = _hovered.Get();
    if (!hovered.has_value() || *hovered >= _slots.size()) {
        return std::nullopt;
    }
    return _row.boxes[*hovered];
}

// The icons end where the toggle's margin begins.
float Tray::IconsLength() const {
    return _row.boxes.empty() ? 0.0f : MainStart(_row.boxes.back(), _row.axis) - MainOf(_style.toggle.margin, _row.axis);
}

float Tray::RevealedLength() const {
    return IconsLength() * _revealedShown;
}

float Tray::ToggleShift() const {
    return IconsLength() - RevealedLength();
}

D2D1_POINT_2F Tray::RowPoint(D2D1_POINT_2F point) const {
    return MainOf(point, _row.axis) < RevealedLength() ? point : MovedAlong(point, ToggleShift(), _row.axis);
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
    std::ranges::copy_if(icons, std::back_inserter(shown), [](const TrayIcon& icon) { return !icon.hidden && icon.pixels != nullptr; });
    return shown;
}

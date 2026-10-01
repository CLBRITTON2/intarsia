#include "bar/Menu.hpp"

#include "bar/Bar.hpp"
#include "bar/Box.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t kWindowClass[] = L"IntarsiaMenu";

    // Short enough for hundreds of entries, long enough to grab.
    constexpr float kMinThumbDips = 16.0f;

    bool            Contains(const D2D1_RECT_F& rect, D2D1_POINT_2F point) {
        return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
    }

    float MaxOffset(const MenuScroll& scroll) {
        return std::max(0.0f, scroll.contentDips - (scroll.viewport.bottom - scroll.viewport.top));
    }

    POINT ClientPoint(LPARAM lParam) {
        return POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)};
    }

} // namespace

MenuScroll ScrolledTo(const MenuScroll& scroll, float offsetDips) {
    return MenuScroll{.viewport = scroll.viewport, .contentDips = scroll.contentDips, .offsetDips = std::clamp(offsetDips, 0.0f, MaxOffset(scroll))};
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
    return D2D1_RECT_F{.left = track.left, .top = top, .right = track.right, .bottom = top + thumbLength};
}

MenuScroll ScrolledToThumb(const MenuScroll& scroll, const D2D1_RECT_F& track, float thumbTopDips) {
    const std::optional<D2D1_RECT_F> thumb = ThumbRect(scroll, track);
    if (!thumb.has_value()) {
        return scroll;
    }
    const float travel = (track.bottom - track.top) - (thumb->bottom - thumb->top);
    return travel <= 0.0f ? scroll : ScrolledTo(scroll, (thumbTopDips - track.top) / travel * MaxOffset(scroll));
}

std::optional<size_t> EntryAt(const std::vector<MenuRow>& rows, const MenuScroll& scroll, D2D1_POINT_2F point) {
    if (!Contains(scroll.viewport, point)) {
        return std::nullopt;
    }
    const D2D1_POINT_2F unscrolled = D2D1::Point2F(point.x, point.y + scroll.offsetDips);
    const auto          row = std::ranges::find_if(rows, [unscrolled](const MenuRow& candidate) { return candidate.entry.has_value() && Contains(candidate.box, unscrolled); });
    return row == rows.end() ? std::nullopt : row->entry;
}

TooltipAnchor RowTooltipAnchor(const RECT& row, const RECT& monitor, std::uint32_t dpi) {
    // The tooltip's rect treats the row as a bar on the monitor's top or bottom edge, placing it below or above the row.
    const Edge edge = monitor.bottom - row.bottom >= row.top - monitor.top ? Edge::Top : Edge::Bottom;
    return TooltipAnchor{.module = row, .bar = row, .monitor = monitor, .edge = edge, .dpi = dpi};
}

std::optional<size_t> StepHighlight(std::optional<size_t> current, size_t count, MenuStep step) {
    if (count == 0) {
        return std::nullopt;
    }
    if (!current.has_value()) {
        return step == MenuStep::Down ? 0 : count - 1;
    }
    if (step == MenuStep::Down) {
        return (*current + 1) % count;
    }
    return *current == 0 ? count - 1 : *current - 1;
}

void Menu::RegisterWindowClass(HINSTANCE instance) {
    ::RegisterWindowClass(instance, kWindowClass, OwnedWindowProc<Menu>, LoadCursorW(nullptr, IDC_ARROW), "menu");
}

Menu::Menu(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat, Launcher& launcher, AppState& state, Tooltip& tooltip,
           std::function<void(const DeviceLostError&)> onDeviceLost) :
    _graphics(graphics), _config(config), _textFormat(textFormat), _launcher(launcher), _state(state), _tooltip(tooltip), _onDeviceLost(std::move(onDeviceLost)),
    _window(CreatePopupWindow(instance, kWindowClass, L"intarsia menu", WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, this, "menu")) {}

LRESULT Menu::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return Dispatch(window, message, wParam, lParam);
    } catch (const DeviceLostError& error) {
        Hide();
        _onDeviceLost(error);
        return 0;
    }
}

LRESULT Menu::Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ACTIVATE:
            if (LOWORD(wParam) == WA_INACTIVE) {
                Hide();
            }
            // Gives the menu the keyboard focus once it is active.
            return DefWindowProcW(window, message, wParam, lParam);
        case WM_MOUSEMOVE: {
            const D2D1_POINT_2F point = ClientDips(ClientPoint(lParam));
            if (_thumbGrabDips.has_value()) {
                Scroll(ScrolledToThumb(_scroll, _track, point.y - *_thumbGrabDips));
            } else {
                const std::optional<size_t> entry = EntryAt(_rows, _scroll, point);
                if (entry != _highlight) {
                    RestartHover();
                }
                Highlight(entry);
            }
            return 0;
        }
        case WM_MOUSEHOVER: ShowTooltip(); return 0;
        case WM_MOUSELEAVE: _tooltip.Hide(); return 0;
        case WM_LBUTTONDOWN: {
            const D2D1_POINT_2F point = ClientDips(ClientPoint(lParam));
            if (Contains(_gutter, point)) {
                GrabThumb(point);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (_thumbGrabDips.has_value()) {
                // Ends the drag through WM_CAPTURECHANGED.
                ReleaseCapture();
                return 0;
            }
            const std::optional<size_t> entry = EntryAt(_rows, _scroll, ClientDips(ClientPoint(lParam)));
            if (entry.has_value()) {
                Pick(*entry);
            }
            return 0;
        }
        case WM_CAPTURECHANGED: _thumbGrabDips.reset(); return 0;
        case WM_MOUSEWHEEL: Wheel(GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
        case WM_KEYDOWN:
            switch (wParam) {
                case VK_UP: Step(MenuStep::Up); return 0;
                case VK_DOWN: Step(MenuStep::Down); return 0;
                case VK_RETURN:
                    if (_highlight.has_value()) {
                        Pick(*_highlight);
                    }
                    return 0;
                case VK_ESCAPE: Hide(); return 0;
                default: return DefWindowProcW(window, message, wParam, lParam);
            }
        // taskkill sends WM_CLOSE to every top-level window, and DefWindowProcW would leave the bar holding a dead
        // handle: the bar that owns the menu destroys it.
        case WM_CLOSE: return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

// The row's box is sized to its text here, and Show places it and stretches it to the widest row.
MenuRow Menu::LayOutRow(std::wstring_view text, const ItemStyle& style, std::optional<size_t> entry, float maxWidthDips, IDWriteInlineObject* ellipsis) const {
    const float               textWidth = std::max(maxWidthDips, 1.0f);
    ComPtr<IDWriteTextLayout> layout;
    ThrowIfFailed(_graphics.dwrite->CreateTextLayout(text.data(), static_cast<std::uint32_t>(text.size()), _textFormat, textWidth, std::numeric_limits<float>::max(), &layout),
                  "IDWriteFactory::CreateTextLayout(menu)");
    ThrowIfFailed(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping(menu)");
    const DWRITE_TRIMMING trimming{.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER, .delimiter = 0, .delimiterCount = 0};
    ThrowIfFailed(layout->SetTrimming(&trimming, ellipsis), "IDWriteTextLayout::SetTrimming(menu)");
    ThrowIfFailed(layout->SetFontWeight(style.weight, DWRITE_TEXT_RANGE{.startPosition = 0, .length = static_cast<std::uint32_t>(text.size())}),
                  "IDWriteTextLayout::SetFontWeight(menu)");
    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics(menu)");
    const float width  = std::min(metrics.widthIncludingTrailingWhitespace, textWidth) + (2.0f * style.padding.xDips);
    const float height = metrics.height + (2.0f * style.padding.yDips);
    return MenuRow{.text = layout, .box = D2D1_RECT_F{.left = 0, .top = 0, .right = width, .bottom = height}, .textOrigin = D2D1::Point2F(0, 0), .style = &style, .entry = entry};
}

void Menu::Show(const MenuListing& listing, const Command& onSelect, const TooltipAnchor& anchor) {
    // Capped by the style and the monitor, as a command can print text of any length and up to the entry limit.
    const MenuStyle&      style         = _config.style.menu;
    const ScrollbarStyle& scrollbar     = style.scrollbar;
    const float           insetX        = style.box.padding.xDips + style.box.borderWidthDips;
    const float           insetY        = style.box.padding.yDips + style.box.borderWidthDips;
    const float           gutter        = scrollbar.widthDips > 0.0f ? scrollbar.widthDips + (2.0f * scrollbar.margin.xDips) : 0.0f;
    const std::int32_t    monitorWidth  = anchor.monitor.right - anchor.monitor.left;
    const std::int32_t    monitorHeight = anchor.monitor.bottom - anchor.monitor.top;
    const float           pixelsToDips  = USER_DEFAULT_SCREEN_DPI / static_cast<float>(anchor.dpi);
    const float           maxWidth      = std::min(static_cast<float>(monitorWidth) * pixelsToDips, style.maxWidthDips.value_or(std::numeric_limits<float>::max()));
    const float           maxHeight     = std::min(static_cast<float>(monitorHeight) * pixelsToDips, style.maxHeightDips.value_or(std::numeric_limits<float>::max()));
    // Leaves room for the scroll bar whether or not the rows overflow, so a cut row never grows once they do.
    const auto textWidth = [maxWidth, insetX, gutter](const ItemStyle& row) { return maxWidth - gutter - (2.0f * (insetX + row.margin.xDips + row.padding.xDips)); };

    ComPtr<IDWriteInlineObject> ellipsis;
    ThrowIfFailed(_graphics.dwrite->CreateEllipsisTrimmingSign(_textFormat, &ellipsis), "IDWriteFactory::CreateEllipsisTrimmingSign(menu)");
    std::vector<MenuEntry> entries;
    std::vector<MenuRow>   rows;
    if (const auto* failure = std::get_if<MenuFailure>(&listing)) {
        rows.push_back(LayOutRow(ToWide(failure->reason), style.item, std::nullopt, textWidth(style.item), ellipsis.Get()));
    } else {
        for (const MenuItem& item : std::get<std::vector<MenuItem>>(listing)) {
            if (const auto* entry = std::get_if<MenuEntry>(&item)) {
                const ItemStyle& row = entry->active ? style.active : style.item;
                rows.push_back(LayOutRow(ToWide(entry->text), row, entries.size(), textWidth(row), ellipsis.Get()));
                entries.push_back(*entry);
            } else {
                // Laid out as an empty entry for its height, then drawn as nothing.
                MenuRow separator = LayOutRow(L"", style.item, std::nullopt, textWidth(style.item), ellipsis.Get());
                separator.text.Reset();
                rows.push_back(std::move(separator));
            }
        }
    }

    float inner = 0.0f;
    for (const MenuRow& row : rows) {
        inner = std::max(inner, row.box.right + (2.0f * row.style->margin.xDips));
    }
    float bottom = insetY;
    for (MenuRow& row : rows) {
        const Spacing& margin  = row.style->margin;
        const Spacing& padding = row.style->padding;
        const float    top     = bottom + margin.yDips;
        row.box                = D2D1::RectF(insetX + margin.xDips, top, insetX + inner - margin.xDips, top + row.box.bottom);
        row.textOrigin         = D2D1::Point2F(row.box.left + padding.xDips, row.box.top + padding.yDips);
        bottom                 = row.box.bottom + margin.yDips;
    }

    const float       content  = bottom - insetY;
    const bool        overflow = content + (2.0f * insetY) > maxHeight;
    const SIZE        size{.cx = std::min(DipsToWholePixels(inner + (2.0f * insetX) + (overflow ? gutter : 0.0f), anchor.dpi), monitorWidth),
                           .cy = std::min(DipsToWholePixels(std::min(content + (2.0f * insetY), maxHeight), anchor.dpi), monitorHeight)};
    const D2D1_RECT_F viewport{.left = insetX, .top = insetY, .right = insetX + inner, .bottom = (static_cast<float>(size.cy) * pixelsToDips) - insetY};
    // A window takes one composition target at a time, so the old surface goes before the new one is made.
    ReleaseSurface();
    _entries   = std::move(entries);
    _rows      = std::move(rows);
    _highlight = std::nullopt;
    _onSelect  = onSelect;
    _dpi       = anchor.dpi;
    _scroll    = MenuScroll{.viewport = viewport, .contentDips = content, .offsetDips = 0.0f};
    _gutter    = D2D1_RECT_F{.left = viewport.right, .top = viewport.top, .right = viewport.right + gutter, .bottom = viewport.bottom};
    _track     = D2D1_RECT_F{.left   = viewport.right + scrollbar.margin.xDips,
                             .top    = viewport.top + scrollbar.margin.yDips,
                             .right  = viewport.right + scrollbar.margin.xDips + scrollbar.widthDips,
                             .bottom = viewport.bottom - scrollbar.margin.yDips};
    _thumbGrabDips.reset();
    _surface = std::make_unique<Surface>(_graphics, _window.get(), static_cast<std::uint32_t>(size.cx), static_cast<std::uint32_t>(size.cy), anchor.dpi);
    _brushes = std::make_unique<BrushCache>(_surface->Context());
    _shown   = true;
    Draw();

    _rect    = TooltipRect(anchor, size);
    _monitor = anchor.monitor;
    if (!SetWindowPos(_window.get(), HWND_TOPMOST, _rect.left, _rect.top, size.cx, size.cy, SWP_SHOWWINDOW)) {
        ThrowLastError("SetWindowPos(menu)");
    }
    // Refused when other input came after the click that opened the menu: it then shows without the keyboard, and hides
    // on the next click elsewhere once a click on it has activated it.
    if (!SetForegroundWindow(_window.get())) {
        Log(LogLevel::Warning, "menu", "foreground_refused", {});
    }
}

void Menu::Hide() {
    // Hiding the active menu deactivates it, which calls Hide again from within ShowWindow.
    if (!_shown) {
        return;
    }
    _shown = false;
    _tooltip.Hide();
    ShowWindow(_window.get(), SW_HIDE);
    ReleaseSurface();
    _rows.clear();
    _entries.clear();
    _highlight = std::nullopt;
    _thumbGrabDips.reset();
}

bool Menu::Shown() const {
    return _shown;
}

void Menu::Draw() {
    ID2D1DeviceContext* context = _surface->Context();
    const MenuStyle&    style   = _config.style.menu;
    const D2D1_SIZE_F   area    = context->GetSize();
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawBox(context, *_brushes, D2D1::RectF(0, 0, area.width, area.height), style.box, style.box.background);
    // Whole pixels, so scrolled text stays as sharp as unscrolled text.
    const float scale  = static_cast<float>(_dpi) / USER_DEFAULT_SCREEN_DPI;
    const float offset = std::round(_scroll.offsetDips * scale) / scale;
    context->PushAxisAlignedClip(_scroll.viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    context->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -offset));
    for (const MenuRow& row : _rows) {
        if (row.text == nullptr || row.box.bottom - offset < _scroll.viewport.top || row.box.top - offset > _scroll.viewport.bottom) {
            continue;
        }
        const ItemStyle& item        = *row.style;
        const bool       highlighted = row.entry.has_value() && row.entry == _highlight;
        const BoxStyle box{.background = item.background, .borderColor = D2D1::ColorF(0, 0, 0, 0), .borderWidthDips = 0.0f, .radiusDips = item.radiusDips, .padding = item.padding};
        DrawBox(context, *_brushes, row.box, box, highlighted ? item.hoverBackground : item.background);
        context->DrawTextLayout(row.textOrigin, row.text.Get(), _brushes->Get(highlighted ? item.hoverColor : item.color));
    }
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->PopAxisAlignedClip();
    const ScrollbarStyle&            scrollbar = style.scrollbar;
    const std::optional<D2D1_RECT_F> thumb     = ThumbRect(_scroll, _track);
    if (thumb.has_value() && scrollbar.widthDips > 0.0f) {
        const BoxStyle box{.background = scrollbar.background, .borderColor = D2D1::ColorF(0, 0, 0, 0), .borderWidthDips = 0.0f, .radiusDips = scrollbar.radiusDips, .padding = {}};
        DrawBox(context, *_brushes, *thumb, box, scrollbar.background);
    }
    _surface->EndDrawAndPresent();
}

void Menu::Highlight(std::optional<size_t> entry) {
    if (!_shown || entry == _highlight) {
        return;
    }
    _highlight = entry;
    Draw();
}

void Menu::Pick(size_t entry) {
    Command command = _onSelect;
    command.arguments.push_back(ToWide(_entries[entry].id));
    Hide();
    _launcher.Launch(command);
}

void Menu::Scroll(const MenuScroll& scroll) {
    if (!_shown || scroll.offsetDips == _scroll.offsetDips) {
        return;
    }
    // It would no longer sit level with its row.
    _tooltip.Hide();
    _scroll = scroll;
    Draw();
}

void Menu::Wheel(std::int16_t delta) {
    if (!_shown || _rows.empty()) {
        return;
    }
    std::uint32_t lines = 0;
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0)) {
        ThrowLastError("SystemParametersInfoW(SPI_GETWHEELSCROLLLINES)");
    }
    const float notches  = static_cast<float>(delta) / WHEEL_DELTA;
    const float rowDips  = _scroll.contentDips / static_cast<float>(_rows.size());
    const float pageDips = _scroll.viewport.bottom - _scroll.viewport.top;
    const float stepDips = lines == WHEEL_PAGESCROLL ? pageDips : std::min(pageDips, static_cast<float>(lines) * rowDips);
    Scroll(ScrolledTo(_scroll, _scroll.offsetDips - (notches * stepDips)));
    // The rows moved under a cursor that did not.
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        ThrowLastError("GetCursorPos(menu)");
    }
    if (!ScreenToClient(_window.get(), &cursor)) {
        ThrowLastError("ScreenToClient(menu)");
    }
    Highlight(EntryAt(_rows, _scroll, ClientDips(cursor)));
}

void Menu::Step(MenuStep step) {
    if (!_shown) {
        return;
    }
    _tooltip.Hide();
    const std::optional<size_t> entry = StepHighlight(_highlight, _entries.size(), step);
    const auto                  row   = std::ranges::find(_rows, entry, &MenuRow::entry);
    if (entry.has_value() && row != _rows.end()) {
        _scroll = ScrolledToShow(_scroll, row->box);
    }
    _highlight = entry;
    Draw();
}

void Menu::GrabThumb(D2D1_POINT_2F point) {
    const std::optional<D2D1_RECT_F> thumb = ThumbRect(_scroll, _track);
    if (!thumb.has_value()) {
        return;
    }
    const float length = thumb->bottom - thumb->top;
    _thumbGrabDips     = point.y >= thumb->top && point.y < thumb->bottom ? point.y - thumb->top : length / 2.0f;
    SetCapture(_window.get());
    Scroll(ScrolledToThumb(_scroll, _track, point.y - *_thumbGrabDips));
}

void Menu::RestartHover() {
    _tooltip.Hide();
    TRACKMOUSEEVENT track{.cbSize = sizeof(TRACKMOUSEEVENT), .dwFlags = TME_HOVER | TME_LEAVE, .hwndTrack = _window.get(), .dwHoverTime = HOVER_DEFAULT};
    if (!TrackMouseEvent(&track)) {
        ThrowLastError("TrackMouseEvent(menu)");
    }
}

void Menu::ShowTooltip() {
    if (!_shown || !_highlight.has_value() || !_entries[*_highlight].tooltip.has_value()) {
        return;
    }
    const auto row = std::ranges::find(_rows, _highlight, &MenuRow::entry);
    if (row == _rows.end()) {
        return;
    }
    const D2D1_RECT_F shown = D2D1::RectF(row->box.left, row->box.top - _scroll.offsetDips, row->box.right, row->box.bottom - _scroll.offsetDips);
    _tooltip.Show(ToWide(*_entries[*_highlight].tooltip), RowTooltipAnchor(DipsToScreen(_rect, shown, _dpi), _monitor, _dpi));
}

D2D1_POINT_2F Menu::ClientDips(POINT client) const {
    const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(_dpi);
    return D2D1::Point2F(static_cast<float>(client.x) * scale, static_cast<float>(client.y) * scale);
}

void Menu::ReleaseSurface() {
    _brushes.reset();
    _surface.reset();
}

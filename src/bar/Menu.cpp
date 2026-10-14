#include "bar/Menu.hpp"

#include "bar/Box.hpp"
#include "bar/PixelGrid.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"
#include "platform/WindowProc.hpp"
#include "render/Axis.hpp"
#include "render/Graphics.hpp"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t kWindowClass[] = L"IntarsiaMenu";

    // The colors a row draws in.
    struct RowColors {
        D2D1_COLOR_F text;
        D2D1_COLOR_F background;
    };

    const ItemStyle& EntryRowStyle(const MenuStyle& style, const MenuEntry& entry) {
        if (entry.active) {
            return style.active;
        }
        return style.item;
    }

    RowColors RowColorsOf(const ItemStyle& style, bool highlighted) {
        if (highlighted) {
            return RowColors{.text = style.hoverColor, .background = style.hoverBackground};
        }
        return RowColors{.text = style.color, .background = style.background};
    }

    // How far one wheel notch scrolls: a page for WHEEL_PAGESCROLL, else lines rows, never more than a page.
    float WheelStepDips(std::uint32_t lines, float pageDips, float rowDips) {
        if (lines == WHEEL_PAGESCROLL) {
            return pageDips;
        }
        return std::min(pageDips, static_cast<float>(lines) * rowDips);
    }

    // How far below the thumb's top the cursor at pointY holds it: where it grabbed the thumb, or its middle when it
    // missed.
    float ThumbGrabDips(float pointY, const D2D1_RECT_F& thumb) {
        if (pointY >= thumb.top && pointY < thumb.bottom) {
            return pointY - thumb.top;
        }
        return (thumb.bottom - thumb.top) / 2.0f;
    }

} // namespace

void Menu::RegisterWindowClass(HINSTANCE instance) {
    ::RegisterWindowClass(instance, kWindowClass, OwnedWindowProc<Menu>, LoadCursorW(nullptr, IDC_ARROW), "menu");
}

Menu::Menu(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat, Launcher& launcher, WindowProcError& windowProcError,
           Tooltip& tooltip, std::function<void(const DeviceLostError&)> onDeviceLost) :
    _graphics(graphics), _config(config), _textFormat(textFormat), _launcher(launcher), _windowProcError(windowProcError), _tooltip(tooltip),
    _onDeviceLost(std::move(onDeviceLost)),
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
            const D2D1_POINT_2F point = ClientDips(ClientPoint(lParam), _dpi);
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
            const D2D1_POINT_2F point = ClientDips(ClientPoint(lParam), _dpi);
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
            const std::optional<size_t> entry = EntryAt(_rows, _scroll, ClientDips(ClientPoint(lParam), _dpi));
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

// The row's box is sized to its text here, and LayOutMenu places it and stretches it to the widest row.
MenuRow Menu::LayOutRow(std::wstring_view text, const ItemStyle& style, std::optional<size_t> entry, float maxWidthDips, IDWriteInlineObject* ellipsis) const {
    const float               textWidth = std::max(maxWidthDips, 1.0f);
    ComPtr<IDWriteTextLayout> layout;
    ThrowIfFailed(_graphics.dwrite->CreateTextLayout(text.data(), static_cast<std::uint32_t>(text.size()), _textFormat, textWidth, std::numeric_limits<float>::max(), &layout),
                  "IDWriteFactory::CreateTextLayout(menu)");
    ThrowIfFailed(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping(menu)");
    const DWRITE_TRIMMING trimming{
        .granularity    = DWRITE_TRIMMING_GRANULARITY_CHARACTER,
        .delimiter      = 0,
        .delimiterCount = 0,
    };
    ThrowIfFailed(layout->SetTrimming(&trimming, ellipsis), "IDWriteTextLayout::SetTrimming(menu)");
    ThrowIfFailed(layout->SetFontWeight(style.weight, DWRITE_TEXT_RANGE{.startPosition = 0, .length = static_cast<std::uint32_t>(text.size())}),
                  "IDWriteTextLayout::SetFontWeight(menu)");
    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics(menu)");
    const float width  = std::min(metrics.widthIncludingTrailingWhitespace, textWidth) + (2.0f * style.padding.xDips);
    const float height = metrics.height + (2.0f * style.padding.yDips);
    return MenuRow{
        .text = layout,
        .box =
            D2D1_RECT_F{
                .left   = 0,
                .top    = 0,
                .right  = width,
                .bottom = height,
            },
        .textOrigin = D2D1::Point2F(0, 0),
        .style      = &style,
        .entry      = entry,
    };
}

void Menu::Show(const MenuListing& listing, const Command& onSelect, const TooltipAnchor& anchor) {
    const MenuStyle&            style     = _config.style.menu;
    const auto                  textWidth = [&style, &anchor](const ItemStyle& row) { return MenuTextWidth(style, row, anchor.monitorRect, anchor.dpi); };

    ComPtr<IDWriteInlineObject> ellipsis;
    ThrowIfFailed(_graphics.dwrite->CreateEllipsisTrimmingSign(_textFormat, &ellipsis), "IDWriteFactory::CreateEllipsisTrimmingSign(menu)");
    std::vector<MenuEntry> entries;
    std::vector<MenuRow>   rows;
    if (const auto* failure = std::get_if<MenuFailure>(&listing)) {
        rows.push_back(LayOutRow(ToWide(failure->reason), style.item, std::nullopt, textWidth(style.item), ellipsis.Get()));
    } else {
        for (const MenuItem& item : std::get<std::vector<MenuItem>>(listing)) {
            if (const auto* entry = std::get_if<MenuEntry>(&item)) {
                const ItemStyle& row = EntryRowStyle(style, *entry);
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

    MenuLayout layout = LayOutMenu(std::move(rows), style, anchor.monitorRect, anchor.dpi);
    const SIZE size   = layout.size;
    // A window takes one composition target at a time, so the old surface goes before the new one is made.
    _canvas.reset();
    _entries   = std::move(entries);
    _rows      = std::move(layout.rows);
    _highlight = std::nullopt;
    _onSelect  = onSelect;
    _dpi       = anchor.dpi;
    _scroll    = layout.scroll;
    _gutter    = layout.gutter;
    _track     = layout.track;
    _thumbGrabDips.reset();
    _canvas = std::make_unique<Canvas>(_graphics, _window.get(), static_cast<std::uint32_t>(size.cx), static_cast<std::uint32_t>(size.cy), anchor.dpi);
    _shown  = true;
    Draw();

    _rect        = TooltipRect(anchor, size);
    _monitorRect = anchor.monitorRect;
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
    _canvas.reset();
    _rows.clear();
    _entries.clear();
    _highlight = std::nullopt;
    _thumbGrabDips.reset();
}

bool Menu::Shown() const {
    return _shown;
}

void Menu::Draw() {
    ID2D1DeviceContext* context = _canvas->surface.Context();
    const MenuStyle&    style   = _config.style.menu;
    const D2D1_SIZE_F   area    = context->GetSize();
    const D2D1_RECT_F   whole   = D2D1::RectF(0, 0, area.width, area.height);
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawBox(context, _canvas->brushes, _canvas->boxes, whole, style.box, style.box.background);
    const PixelGrid grid = PixelGridOf(context);
    _canvas->surface.ShowBackdrops(BackdropOutlines(style.backdrop, grid, {whole}, style.box), style.backdrop);
    // Whole pixels, so scrolled text stays as sharp as unscrolled text.
    const float offset = SnapLength(grid, _scroll.offsetDips);
    context->PushAxisAlignedClip(_scroll.viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    context->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -offset));
    for (const MenuRow& row : _rows) {
        if (row.text == nullptr || row.box.bottom - offset < _scroll.viewport.top || row.box.top - offset > _scroll.viewport.bottom) {
            continue;
        }
        const ItemStyle& itemStyle = *row.style;
        const RowColors  colors    = RowColorsOf(itemStyle, row.entry.has_value() && row.entry == _highlight);
        const BoxStyle   box{
              .background = itemStyle.background,
              .border     = {},
              .radiusDips = itemStyle.radiusDips,
              .padding    = itemStyle.padding,
              .shadows    = itemStyle.shadows,
        };
        DrawBox(context, _canvas->brushes, _canvas->boxes, row.box, box, colors.background);
        context->DrawTextLayout(row.textOrigin, row.text.Get(), _canvas->brushes.Get(colors.text));
    }
    context->SetTransform(D2D1::Matrix3x2F::Identity());
    context->PopAxisAlignedClip();
    const ScrollbarStyle&            scrollbar = style.scrollbar;
    const std::optional<D2D1_RECT_F> thumb     = ThumbRect(_scroll, _track);
    if (thumb.has_value() && scrollbar.widthDips > 0.0f) {
        const BoxStyle box{
            .background = scrollbar.background,
            .border     = {},
            .radiusDips = scrollbar.radiusDips,
            .padding    = {},
            .shadows    = {},
        };
        DrawBox(context, _canvas->brushes, _canvas->boxes, *thumb, box, scrollbar.background);
    }
    _canvas->surface.EndDrawAndPresent();
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
    Scroll(ScrolledTo(_scroll, _scroll.offsetDips - (notches * WheelStepDips(lines, pageDips, rowDips))));
    // The rows moved under a cursor that did not.
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        ThrowLastError("GetCursorPos(menu)");
    }
    if (!ScreenToClient(_window.get(), &cursor)) {
        ThrowLastError("ScreenToClient(menu)");
    }
    Highlight(EntryAt(_rows, _scroll, ClientDips(cursor, _dpi)));
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
    _thumbGrabDips = ThumbGrabDips(point.y, *thumb);
    SetCapture(_window.get());
    Scroll(ScrolledToThumb(_scroll, _track, point.y - *_thumbGrabDips));
}

void Menu::RestartHover() {
    _tooltip.Hide();
    TRACKMOUSEEVENT track{
        .cbSize      = sizeof(TRACKMOUSEEVENT),
        .dwFlags     = TME_HOVER | TME_LEAVE,
        .hwndTrack   = _window.get(),
        .dwHoverTime = HOVER_DEFAULT,
    };
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
    const D2D1_RECT_F shown = Offset(row->box, D2D1::Point2F(0.0f, -_scroll.offsetDips));
    _tooltip.Show(ToWide(*_entries[*_highlight].tooltip), RowTooltipAnchor(DipsToScreen(_rect, shown, _dpi), _monitorRect, _dpi));
}

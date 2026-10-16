#include "bar/Menu.hpp"

#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Process.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"
#include "render/Axis.hpp"
#include "render/Box.hpp"
#include "render/Graphics.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

using Microsoft::WRL::ComPtr;

namespace {

    // Null when the item has no tooltip.
    std::optional<std::string> TooltipOf(const nlohmann::json& item) {
        const auto tooltip = item.find("tooltip");
        if (tooltip == item.end()) {
            return std::nullopt;
        }
        if (!tooltip->is_string()) {
            throw OutputError("printed a JSON item tooltip that is not a string");
        }
        return tooltip->get<std::string>();
    }

    MenuItem ReadItem(const nlohmann::json& item) {
        if (!item.is_object()) {
            throw OutputError("printed a JSON item that is not an object");
        }
        if (item.contains("separator")) {
            if (item.size() != 1 || item.at("separator") != true) {
                throw OutputError("printed a JSON separator other than {\"separator\":true}");
            }
            return MenuSeparator{};
        }
        for (const auto& [key, field] : item.items()) {
            if (key != "id" && key != "text" && key != "active" && key != "tooltip") {
                throw OutputError("printed a JSON item field other than id, text, active and tooltip");
            }
        }
        const auto id   = item.find("id");
        const auto text = item.find("text");
        if (id == item.end() || !id->is_string() || text == item.end() || !text->is_string()) {
            throw OutputError("printed a JSON item without an id string and a text string");
        }
        const auto active = item.find("active");
        if (active != item.end() && !active->is_boolean()) {
            throw OutputError("printed a JSON item active that is not true or false");
        }
        MenuEntry entry{
            .id      = id->get<std::string>(),
            .text    = text->get<std::string>(),
            .active  = active != item.end() && active->get<bool>(),
            .tooltip = TooltipOf(item),
        };
        RequireDrawable(entry.id, "id", L"");
        RequireDrawable(entry.text, "text", L"");
        if (entry.tooltip.has_value()) {
            RequireDrawable(*entry.tooltip, "tooltip", L"\n");
        }
        if (entry.id.empty() || entry.id.front() == '-' || entry.id.front() == '/') {
            throw OutputError("printed a JSON item id that is empty or starts with - or /");
        }
        return entry;
    }

    // Runs on the lister's thread, so every failure becomes a listing instead of an exception.
    MenuListing ListEntries(const Command& command, std::chrono::seconds timeout, HANDLE stopEvent) {
        const auto output = RunOrFailure([&] { return CaptureOutput(command, timeout, kMaxCommandOutputBytes, stopEvent); });
        if (const auto* failure = std::get_if<RunFailure>(&output)) {
            return MenuFailure{.reason = failure->reason, .detail = failure->detail};
        }
        try {
            return ParseMenuOutput(std::get<std::string>(output));
        } catch (const OutputError& error) { return MenuFailure{.reason = error.what(), .detail = error.what()}; }
    }

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

std::vector<MenuItem> ParseMenuOutput(std::string_view output) {
    const nlohmann::json value = nlohmann::json::parse(output.begin(), output.end(), nullptr, false);
    if (value.is_discarded()) {
        throw OutputError("printed invalid JSON");
    }
    const auto items = value.find("items");
    if (!value.is_object() || value.size() != 1 || items == value.end()) {
        throw OutputError("printed JSON that is not an object with only an items list");
    }
    if (!items->is_array()) {
        throw OutputError("printed JSON items that is not a list");
    }
    if (items->size() > kMaxMenuEntries) {
        throw OutputError(std::format("printed more than {} items", kMaxMenuEntries));
    }
    std::vector<MenuItem> read;
    read.reserve(items->size());
    for (const nlohmann::json& item : *items) {
        read.push_back(ReadItem(item));
    }
    return read;
}

bool HasEntries(const std::vector<MenuItem>& items) {
    return std::ranges::any_of(items, [](const MenuItem& item) { return std::holds_alternative<MenuEntry>(item); });
}

MenuLister::MenuLister(HWND target, std::uint32_t message) : _target(target), _message(message), _command{}, _timeout{} {}

void MenuLister::List(const Command& command, std::chrono::seconds timeout) {
    // The old thread is joined before the new one starts, so only the new one can fill _ready, or read _command and
    // _timeout, from here on.
    _thread.reset();
    {
        const std::scoped_lock lock(_mutex);
        _ready.reset();
    }
    _command = command;
    _timeout = timeout;
    _thread  = std::make_unique<StoppableThread>("menu lister", [this](HANDLE stopEvent) { Run(stopEvent); });
}

std::optional<MenuListing> MenuLister::Take() {
    const std::scoped_lock lock(_mutex);
    return std::exchange(_ready, std::nullopt);
}

void MenuLister::Run(HANDLE stopEvent) {
    MenuListing listing = ListEntries(_command, _timeout, stopEvent);
    // A listing List or the destructor cut short says nothing about the command.
    if (WaitForSingleObject(stopEvent, 0) == WAIT_OBJECT_0) {
        return;
    }
    {
        const std::scoped_lock lock(_mutex);
        _ready = std::move(listing);
    }
    if (!PostMessageW(_target, _message, 0, 0)) {
        Log(LogLevel::Warning, "menu", "post_failed", {{.key = "error", .value = std::to_string(GetLastError())}});
    }
}

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

void Menu::RegisterWindowClass(HINSTANCE instance) {
    ::RegisterWindowClass(instance, kWindowClass, OwnedWindowProc<Menu>, LoadCursorW(nullptr, IDC_ARROW), "menu");
}

Menu::Menu(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat, Launcher& launcher, WindowProcError& windowProcError,
           Tooltip& tooltip, const std::vector<WallpaperBlur>& wallpapers, std::function<void(const DeviceLostError&)> onDeviceLost) :
    _graphics(graphics), _config(config), _textFormat(textFormat), _launcher(launcher), _windowProcError(windowProcError), _tooltip(tooltip), _wallpapers(wallpapers),
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
                Scroll(ScrolledToThumb(_layout.scroll, _layout.track, point.y - *_thumbGrabDips));
            } else {
                const std::optional<size_t> entry = EntryAt(_layout.rows, _layout.scroll, point);
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
            if (Contains(_layout.gutter, point)) {
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
            const std::optional<size_t> entry = EntryAt(_layout.rows, _layout.scroll, ClientDips(ClientPoint(lParam), _dpi));
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

    _layout         = LayOutMenu(std::move(rows), style, anchor.monitorRect, anchor.dpi);
    const SIZE size = _layout.size;
    // A window takes one composition target at a time, so the old surface goes before the new one is made.
    _canvas.reset();
    _entries   = std::move(entries);
    _highlight = std::nullopt;
    _onSelect  = onSelect;
    _dpi       = anchor.dpi;
    _thumbGrabDips.reset();
    _canvas = std::make_unique<Canvas>(_graphics, _window.get(), static_cast<std::uint32_t>(size.cx), static_cast<std::uint32_t>(size.cy), anchor.dpi);
    _shown  = true;
    // Before drawing, which places the wallpaper's blur by it.
    _rect        = TooltipRect(anchor, size);
    _monitorRect = anchor.monitorRect;
    Draw();

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
    _layout.rows.clear();
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
    DrawBackdrops(_canvas->surface, _wallpapers, style.backdrop, {whole}, style.box, _rect);
    DrawBox(context, _canvas->brushes, _canvas->boxes, whole, style.box, style.box.background);
    const PixelGrid grid = PixelGridOf(context);
    // Whole pixels, so scrolled text stays as sharp as unscrolled text.
    const float offset = SnapLength(grid, _layout.scroll.offsetDips);
    context->PushAxisAlignedClip(_layout.scroll.viewport, D2D1_ANTIALIAS_MODE_ALIASED);
    context->SetTransform(D2D1::Matrix3x2F::Translation(0.0f, -offset));
    for (const MenuRow& row : _layout.rows) {
        if (row.text == nullptr || row.box.bottom - offset < _layout.scroll.viewport.top || row.box.top - offset > _layout.scroll.viewport.bottom) {
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
    const std::optional<D2D1_RECT_F> thumb     = ThumbRect(_layout.scroll, _layout.track);
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
    if (!_shown || scroll.offsetDips == _layout.scroll.offsetDips) {
        return;
    }
    // It would no longer sit level with its row.
    _tooltip.Hide();
    _layout.scroll = scroll;
    Draw();
}

void Menu::Wheel(std::int16_t delta) {
    if (!_shown || _layout.rows.empty()) {
        return;
    }
    std::uint32_t lines = 0;
    if (!SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0)) {
        ThrowLastError("SystemParametersInfoW(SPI_GETWHEELSCROLLLINES)");
    }
    const float notches  = static_cast<float>(delta) / WHEEL_DELTA;
    const float rowDips  = _layout.scroll.contentDips / static_cast<float>(_layout.rows.size());
    const float pageDips = _layout.scroll.viewport.bottom - _layout.scroll.viewport.top;
    Scroll(ScrolledTo(_layout.scroll, _layout.scroll.offsetDips - (notches * WheelStepDips(lines, pageDips, rowDips))));
    // The rows moved under a cursor that did not.
    POINT cursor{};
    if (!GetCursorPos(&cursor)) {
        ThrowLastError("GetCursorPos(menu)");
    }
    if (!ScreenToClient(_window.get(), &cursor)) {
        ThrowLastError("ScreenToClient(menu)");
    }
    Highlight(EntryAt(_layout.rows, _layout.scroll, ClientDips(cursor, _dpi)));
}

void Menu::Step(MenuStep step) {
    if (!_shown) {
        return;
    }
    _tooltip.Hide();
    const std::optional<size_t> entry = StepHighlight(_highlight, _entries.size(), step);
    const auto                  row   = std::ranges::find(_layout.rows, entry, &MenuRow::entry);
    if (entry.has_value() && row != _layout.rows.end()) {
        _layout.scroll = ScrolledToShow(_layout.scroll, row->box);
    }
    _highlight = entry;
    Draw();
}

void Menu::GrabThumb(D2D1_POINT_2F point) {
    const std::optional<D2D1_RECT_F> thumb = ThumbRect(_layout.scroll, _layout.track);
    if (!thumb.has_value()) {
        return;
    }
    _thumbGrabDips = ThumbGrabDips(point.y, *thumb);
    SetCapture(_window.get());
    Scroll(ScrolledToThumb(_layout.scroll, _layout.track, point.y - *_thumbGrabDips));
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
    const auto row = std::ranges::find(_layout.rows, _highlight, &MenuRow::entry);
    if (row == _layout.rows.end()) {
        return;
    }
    const D2D1_RECT_F shown = Offset(row->box, D2D1::Point2F(0.0f, -_layout.scroll.offsetDips));
    _tooltip.Show(ToWide(*_entries[*_highlight].tooltip), RowTooltipAnchor(DipsToScreen(_rect, shown, _dpi), _monitorRect, _dpi));
}

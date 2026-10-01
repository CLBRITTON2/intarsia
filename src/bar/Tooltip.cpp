#include "bar/Tooltip.hpp"

#include "bar/Box.hpp"
#include "platform/Errors.hpp"
#include "platform/Win32Window.hpp"
#include "render/Graphics.hpp"

#include <cstdint>
#include <algorithm>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t kWindowClass[] = L"IntarsiaTooltip";

    // Where a span of length starting at start goes to stay within low to high. Show caps length at the monitor's.
    std::int32_t KeepWithin(std::int32_t start, std::int32_t length, std::int32_t low, std::int32_t high) {
        return std::max(low, std::min(start, high - length));
    }

    // The tooltip only shows what Show drew. WM_CLOSE (taskkill sends it to every top-level window) is ignored: the
    // bar that owns the tooltip destroys it, and DefWindowProcW would leave the bar holding a dead handle.
    LRESULT CALLBACK TooltipWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_CLOSE) {
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

} // namespace

RECT TooltipRect(const TooltipAnchor& anchor, SIZE size) {
    const RECT&  module  = anchor.module;
    const RECT&  bar     = anchor.bar;
    const RECT&  monitor = anchor.monitor;
    std::int32_t left    = 0;
    std::int32_t top     = 0;
    switch (anchor.edge) {
        case Edge::Top:
        case Edge::Bottom:
            left = KeepWithin(((module.left + module.right) / 2) - (size.cx / 2), size.cx, monitor.left, monitor.right);
            top  = anchor.edge == Edge::Top ? bar.bottom : bar.top - size.cy;
            break;
        case Edge::Left:
        case Edge::Right:
            top  = KeepWithin(((module.top + module.bottom) / 2) - (size.cy / 2), size.cy, monitor.top, monitor.bottom);
            left = KeepWithin(anchor.edge == Edge::Left ? bar.right : bar.left - size.cx, size.cx, monitor.left, monitor.right);
            break;
    }
    return RECT{.left = left, .top = top, .right = left + size.cx, .bottom = top + size.cy};
}

void Tooltip::RegisterWindowClass(HINSTANCE instance) {
    ::RegisterWindowClass(instance, kWindowClass, TooltipWindowProc, LoadCursorW(nullptr, IDC_ARROW), "tooltip");
}

Tooltip::Tooltip(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat) :
    _graphics(graphics), _config(config), _textFormat(textFormat),
    _window(CreatePopupWindow(instance, kWindowClass, L"intarsia tooltip", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, nullptr, "tooltip")) {}

Tooltip::~Tooltip() {
    DestroyWindow(_window);
}

void Tooltip::Show(std::wstring_view text, const TooltipAnchor& anchor) {
    // Wrapped at the monitor's width and cut at its height: text from a command or a window title can be any length,
    // and a surface past the GPU's texture size limit cannot be created.
    const BoxStyle&           style         = _config.style.tooltip.box;
    const Insets              insets        = ContentInsets(style);
    const std::int32_t        monitorWidth  = anchor.monitor.right - anchor.monitor.left;
    const std::int32_t        monitorHeight = anchor.monitor.bottom - anchor.monitor.top;
    const float               widthDips     = PixelsToDips(static_cast<float>(monitorWidth), anchor.dpi);
    ComPtr<IDWriteTextLayout> layout;
    ThrowIfFailed(_graphics.dwrite->CreateTextLayout(text.data(), static_cast<std::uint32_t>(text.size()), _textFormat, std::max(widthDips - insets.left - insets.right, 1.0f),
                                                     std::numeric_limits<float>::max(), &layout),
                  "IDWriteFactory::CreateTextLayout(tooltip)");
    ThrowIfFailed(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK), "IDWriteTextLayout::SetWordWrapping(tooltip)");
    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics(tooltip)");

    const SIZE size{.cx = std::min(DipsToWholePixels(metrics.widthIncludingTrailingWhitespace + insets.left + insets.right, anchor.dpi), monitorWidth),
                    .cy = std::min(DipsToWholePixels(metrics.height + insets.top + insets.bottom, anchor.dpi), monitorHeight)};
    // A window takes one composition target at a time, so the old surface goes before the new one is made.
    ReleaseSurface();
    _surface = std::make_unique<Surface>(_graphics, _window, static_cast<std::uint32_t>(size.cx), static_cast<std::uint32_t>(size.cy), anchor.dpi);
    _brushes = std::make_unique<BrushCache>(_surface->Context());

    ID2D1DeviceContext* context = _surface->Context();
    const D2D1_SIZE_F   area    = context->GetSize();
    const D2D1_RECT_F   box     = D2D1::RectF(0, 0, area.width, area.height);
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawBox(context, *_brushes, box, style, style.background);
    context->DrawTextLayout(D2D1::Point2F(insets.left, insets.top), layout.Get(), _brushes->Get(_config.style.tooltip.color));
    _surface->ShowBackdrops(BackdropOutlines(_config.style.tooltip.backdrop, PixelGridOf(context), {box}, style), _config.style.tooltip.backdrop);
    _surface->EndDrawAndPresent();

    const RECT rect = TooltipRect(anchor, size);
    if (!SetWindowPos(_window, HWND_TOPMOST, rect.left, rect.top, size.cx, size.cy, SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
        ThrowLastError("SetWindowPos(tooltip)");
    }
}

// The buffers are sized to the text, up to the whole monitor, so they go with it instead of waiting for the next hover.
void Tooltip::Hide() {
    ShowWindow(_window, SW_HIDE);
    ReleaseSurface();
}

void Tooltip::ReleaseSurface() {
    _brushes.reset();
    _surface.reset();
}

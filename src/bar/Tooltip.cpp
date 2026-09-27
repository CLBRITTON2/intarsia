#include "bar/Tooltip.hpp"

#include "platform/Errors.hpp"

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t kWindowClass[] = L"IntarsiaTooltip";

    // Where a span of length starting at start goes to stay within low to high, pinned to low when it cannot fit.
    std::int32_t KeepWithin(std::int32_t start, std::int32_t length, std::int32_t low, std::int32_t high) {
        return std::max(low, std::min(start, high - length));
    }

    // Rounded up so the text is never clipped by a partial pixel.
    std::int32_t DipsToWholePixels(float dips, std::uint32_t dpi) {
        return static_cast<std::int32_t>(std::ceil(dips * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
    }

    // The tooltip only shows what Show drew. WM_CLOSE (taskkill sends it to every top-level window) is ignored: the
    // bar that owns the tooltip destroys it, and DefWindowProcW would leave the bar holding a dead handle.
    LRESULT CALLBACK TooltipWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_CLOSE) {
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    HWND CreateTooltipWindow(HINSTANCE instance) {
        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST | WS_EX_NOREDIRECTIONBITMAP, kWindowClass, L"intarsia tooltip", WS_POPUP, 0, 0, 0, 0,
                                      nullptr, nullptr, instance, nullptr);
        if (window == nullptr) {
            ThrowLastError("CreateWindowExW(tooltip)");
        }
        return window;
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
            left = anchor.edge == Edge::Left ? bar.right : bar.left - size.cx;
            break;
    }
    return RECT{.left = left, .top = top, .right = left + size.cx, .bottom = top + size.cy};
}

void Tooltip::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = TooltipWindowProc;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0) {
        ThrowLastError("RegisterClassExW(tooltip)");
    }
}

Tooltip::Tooltip(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, IDWriteTextFormat* textFormat) :
    _graphics(graphics), _config(config), _textFormat(textFormat), _window(CreateTooltipWindow(instance)) {}

Tooltip::~Tooltip() {
    DestroyWindow(_window);
}

// Padding is the bar's spacing and the border its border, so the tooltip reads as part of the bar.
void Tooltip::Show(std::wstring_view text, const TooltipAnchor& anchor) {
    // Wrapped at the monitor's width and cut at its height: text from a command or a window title can be any length,
    // and a surface past the GPU's texture size limit cannot be created.
    const float               inset         = _config.bar.spacingDips + _config.bar.borderDips;
    const std::int32_t        monitorWidth  = anchor.monitor.right - anchor.monitor.left;
    const std::int32_t        monitorHeight = anchor.monitor.bottom - anchor.monitor.top;
    const float               widthDips     = static_cast<float>(monitorWidth) * USER_DEFAULT_SCREEN_DPI / static_cast<float>(anchor.dpi);
    ComPtr<IDWriteTextLayout> layout;
    ThrowIfFailed(_graphics.dwrite->CreateTextLayout(text.data(), static_cast<std::uint32_t>(text.size()), _textFormat, std::max(widthDips - (2 * inset), 1.0f),
                                                     std::numeric_limits<float>::max(), &layout),
                  "IDWriteFactory::CreateTextLayout(tooltip)");
    ThrowIfFailed(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_EMERGENCY_BREAK), "IDWriteTextLayout::SetWordWrapping(tooltip)");
    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics(tooltip)");

    const SIZE size{.cx = std::min(DipsToWholePixels(metrics.widthIncludingTrailingWhitespace + (2 * inset), anchor.dpi), monitorWidth),
                    .cy = std::min(DipsToWholePixels(metrics.height + (2 * inset), anchor.dpi), monitorHeight)};
    // A window takes one composition target at a time, so the old surface goes before the new one is made.
    ReleaseSurface();
    _surface         = std::make_unique<Surface>(_graphics, _window, static_cast<std::uint32_t>(size.cx), static_cast<std::uint32_t>(size.cy), anchor.dpi);
    _backgroundBrush = CreateBrush(_surface->Context(), _config.colors.background);
    _borderBrush     = CreateBrush(_surface->Context(), _config.colors.border);
    _foregroundBrush = CreateBrush(_surface->Context(), _config.colors.foreground);

    ID2D1DeviceContext* context = _surface->Context();
    const D2D1_SIZE_F   area    = context->GetSize();
    const float         border  = _config.bar.borderDips;
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    context->FillRectangle(D2D1::RectF(0, 0, area.width, area.height), _backgroundBrush.Get());
    if (border > 0.0f) {
        // A stroke is centered on its rectangle, so the rectangle is inset by half the width to keep it inside.
        context->DrawRectangle(D2D1::RectF(border / 2, border / 2, area.width - (border / 2), area.height - (border / 2)), _borderBrush.Get(), border);
    }
    context->DrawTextLayout(D2D1::Point2F(inset, inset), layout.Get(), _foregroundBrush.Get());
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
    _foregroundBrush.Reset();
    _borderBrush.Reset();
    _backgroundBrush.Reset();
    _surface.reset();
}

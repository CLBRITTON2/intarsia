#include "bar/Tooltip.hpp"

#include "platform/Errors.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t kWindowClass[] = L"IntarsiaTooltip";

    // Where a span of length starting at start goes to stay within low to high, pinned to low when it cannot fit.
    LONG KeepWithin(LONG start, LONG length, LONG low, LONG high) {
        return std::max(low, std::min(start, high - length));
    }

    // Rounded up so the text is never clipped by a partial pixel.
    LONG DipsToWholePixels(float dips, UINT dpi) {
        return static_cast<LONG>(std::ceil(dips * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
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
    const RECT& module  = anchor.module;
    const RECT& bar     = anchor.bar;
    const RECT& monitor = anchor.monitor;
    LONG        left    = 0;
    LONG        top     = 0;
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

// Messages go straight to DefWindowProcW: the tooltip only shows what Show drew.
void Tooltip::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = DefWindowProcW;
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
    ComPtr<IDWriteTextLayout> layout;
    ThrowIfFailed(_graphics.dwrite->CreateTextLayout(text.data(), static_cast<UINT32>(text.size()), _textFormat, std::numeric_limits<float>::max(),
                                                     std::numeric_limits<float>::max(), &layout),
                  "IDWriteFactory::CreateTextLayout(tooltip)");
    ThrowIfFailed(layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping(tooltip)");
    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics(tooltip)");

    const float inset = _config.bar.spacingDips + _config.bar.borderDips;
    const SIZE size{.cx = DipsToWholePixels(metrics.widthIncludingTrailingWhitespace + (2 * inset), anchor.dpi), .cy = DipsToWholePixels(metrics.height + (2 * inset), anchor.dpi)};
    if (_surface == nullptr) {
        _surface         = std::make_unique<Surface>(_graphics, _window, static_cast<UINT>(size.cx), static_cast<UINT>(size.cy), anchor.dpi);
        _backgroundBrush = CreateBrush(_surface->Context(), _config.colors.background);
        _borderBrush     = CreateBrush(_surface->Context(), _config.colors.border);
        _foregroundBrush = CreateBrush(_surface->Context(), _config.colors.foreground);
    } else {
        _surface->Resize(static_cast<UINT>(size.cx), static_cast<UINT>(size.cy), anchor.dpi);
    }

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

void Tooltip::Hide() {
    ShowWindow(_window, SW_HIDE);
}

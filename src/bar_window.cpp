#include "bar_window.h"

#include "clock.h"
#include "errors.h"
#include "theme.h"
#include "win32_cast.h"

#include <shellapi.h>

#include <cmath>
#include <format>
#include <stdexcept>
#include <string>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t  kWindowClass[]  = L"IntarsiaBar";
    constexpr UINT     kAppBarCallback = WM_APP + 1;
    constexpr UINT_PTR kClockTimer     = 1;

    int                DipsToPixels(float dips, UINT dpi) {
        return static_cast<int>(std::lround(dips * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
    }

    // The border sits on the side facing the rest of the desktop.
    D2D1_RECT_F BorderRect(const D2D1_SIZE_F& size, Edge edge) {
        switch (edge) {
            case Edge::Top: return D2D1::RectF(0, size.height - theme::kBorderDips, size.width, size.height);
            case Edge::Bottom: return D2D1::RectF(0, 0, size.width, theme::kBorderDips);
            case Edge::Left: return D2D1::RectF(size.width - theme::kBorderDips, 0, size.width, size.height);
            case Edge::Right: return D2D1::RectF(0, 0, theme::kBorderDips, size.height);
        }
        throw std::invalid_argument(std::format("unknown edge {}", static_cast<int>(edge)));
    }

    HWND CreateBarWindow(HINSTANCE instance, void* owner) {
        // No activation so clicking the bar never steals focus from the tiled window.
        HWND window =
            CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, kWindowClass, L"intarsia", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, instance, owner);
        if (window == nullptr) {
            ThrowLastError("CreateWindowExW");
        }
        return window;
    }

    ComPtr<ID2D1SolidColorBrush> CreateBrush(ID2D1DeviceContext* context, const D2D1_COLOR_F& color) {
        ComPtr<ID2D1SolidColorBrush> brush;
        ThrowIfFailed(context->CreateSolidColorBrush(color, &brush), "ID2D1DeviceContext::CreateSolidColorBrush");
        return brush;
    }

} // namespace

void BarWindow::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = WindowProc;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0) {
        ThrowLastError("RegisterClassExW");
    }
}

BarWindow::BarWindow(HINSTANCE instance, const GraphicsDevice& graphics, const BarPlacement& placement, AppState& state) :
    graphics_(graphics), state_(state), placement_(placement), window_(CreateBarWindow(instance, this)) {
    ++state_.liveBars;
    RequireFontFamily(graphics_.dwrite.Get(), theme::kFontFamily);
    ThrowIfFailed(graphics_.dwrite->CreateTextFormat(theme::kFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     theme::kTextSizeDips, L"en-us", &textFormat_),
                  "IDWriteFactory::CreateTextFormat");
    ThrowIfFailed(textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER), "IDWriteTextFormat::SetTextAlignment");
    ThrowIfFailed(textFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER), "IDWriteTextFormat::SetParagraphAlignment");

    ThrowIfFailed(graphics_.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_), "ID2D1Device::CreateDeviceContext");
    backgroundBrush_ = CreateBrush(context_.Get(), theme::kBackground);
    borderBrush_     = CreateBrush(context_.Get(), theme::kBorder);
    foregroundBrush_ = CreateBrush(context_.Get(), theme::kForeground);
    iconBrush_       = CreateBrush(context_.Get(), theme::kAccentAlt);

    ThrowIfFailed(graphics_.composition->CreateTargetForHwnd(window_, TRUE, &compositionTarget_), "IDCompositionDevice::CreateTargetForHwnd");
    ThrowIfFailed(graphics_.composition->CreateVisual(&visual_), "IDCompositionDevice::CreateVisual");
    ThrowIfFailed(compositionTarget_->SetRoot(visual_.Get()), "IDCompositionTarget::SetRoot");

    RegisterAppBar(window_, kAppBarCallback);
    Dock();
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    Render();
    ScheduleClockTick();
}

BarWindow::~BarWindow() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
}

LRESULT CALLBACK BarWindow::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<BarWindow*>(IntToPointer<CREATESTRUCTW>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    auto* self = IntToPointer<BarWindow>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcW(window, message, wParam, lParam);
    }

    try {
        return self->HandleMessage(window, message, wParam, lParam);
    } catch (...) {
        self->state_.error = std::current_exception();
        PostQuitMessage(1);
        return 0;
    }
}

// Uses the handle it is given: messages sent during CreateWindowExW arrive before window_ is assigned.
LRESULT BarWindow::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            // CreateWindowExW sends a zero size before the constructor has built the swap chain.
            if (swapChain_ != nullptr && LOWORD(lParam) != 0 && HIWORD(lParam) != 0) {
                ResizeSwapChain(LOWORD(lParam), HIWORD(lParam));
                Render();
            }
            return 0;
        case WM_DPICHANGED:
            placement_.dpi = HIWORD(wParam);
            Dock();
            return 0;
        case WM_DISPLAYCHANGE: Dock(); return 0;
        case WM_PAINT:
            Render();
            ValidateRect(window, nullptr);
            return 0;
        case WM_TIMER:
            if (wParam == kClockTimer) {
                Render();
                ScheduleClockTick();
            }
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_ACTIVATE: NotifyAppBarActivated(window); return 0;
        case WM_WINDOWPOSCHANGED: NotifyAppBarMoved(window); return DefWindowProcW(window, message, wParam, lParam);
        case kAppBarCallback:
            if (wParam == ABN_POSCHANGED) {
                Dock();
            }
            return 0;
        case WM_CLOSE: DestroyWindow(window); return 0;
        case WM_DESTROY:
            KillTimer(window, kClockTimer);
            UnregisterAppBar(window);
            // Later messages such as WM_NCDESTROY go straight to DefWindowProcW.
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            window_ = nullptr;
            if (--state_.liveBars == 0) {
                PostQuitMessage(0);
            }
            return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

void BarWindow::Dock() {
    const int  thickness = DipsToPixels(theme::kThicknessDips, placement_.dpi);
    const RECT rect      = ReserveAppBarRect(window_, placement_.monitorRect, placement_.edge, thickness);
    if (!MoveWindow(window_, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE)) {
        ThrowLastError("MoveWindow");
    }
}

void BarWindow::CreateSwapChain(UINT width, UINT height) {
    DXGI_SWAP_CHAIN_DESC1 description{};
    description.Width            = width;
    description.Height           = height;
    description.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    description.SampleDesc.Count = 1;
    description.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    description.BufferCount      = 2;
    description.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    description.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;

    ThrowIfFailed(graphics_.dxgiFactory->CreateSwapChainForComposition(graphics_.d3d.Get(), &description, nullptr, &swapChain_), "IDXGIFactory2::CreateSwapChainForComposition");
    ThrowIfFailed(visual_->SetContent(swapChain_.Get()), "IDCompositionVisual::SetContent");
    ThrowIfFailed(graphics_.composition->Commit(), "IDCompositionDevice::Commit");
    BindRenderTarget();
}

void BarWindow::ResizeSwapChain(UINT width, UINT height) {
    // ResizeBuffers fails while D2D still holds a bitmap over the old back buffer.
    context_->SetTarget(nullptr);
    ThrowIfFailed(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "IDXGISwapChain1::ResizeBuffers");
    BindRenderTarget();
}

void BarWindow::BindRenderTarget() {
    ComPtr<IDXGISurface> surface;
    ThrowIfFailed(swapChain_->GetBuffer(0, IID_PPV_ARGS(&surface)), "IDXGISwapChain1::GetBuffer");

    const auto                    dpi        = static_cast<float>(placement_.dpi);
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                                       D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    ComPtr<ID2D1Bitmap1>          target;
    ThrowIfFailed(context_->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target), "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");
    context_->SetTarget(target.Get());
    context_->SetDpi(dpi, dpi);
}

void BarWindow::Render() {
    if (swapChain_ == nullptr) {
        return;
    }

    SYSTEMTIME now{};
    GetLocalTime(&now);
    const std::wstring        icon(kClockIcon);
    const std::wstring        text = icon + L"  " + FormatClock(now);

    const D2D1_SIZE_F         size = context_->GetSize();
    ComPtr<IDWriteTextLayout> layout;
    ThrowIfFailed(graphics_.dwrite->CreateTextLayout(text.c_str(), static_cast<UINT32>(text.size()), textFormat_.Get(), size.width, size.height, &layout),
                  "IDWriteFactory::CreateTextLayout");
    const DWRITE_TEXT_RANGE iconRange{.startPosition = 0, .length = static_cast<UINT32>(icon.size())};
    ThrowIfFailed(layout->SetFontSize(theme::kIconSizeDips, iconRange), "IDWriteTextLayout::SetFontSize");
    ThrowIfFailed(layout->SetDrawingEffect(iconBrush_.Get(), iconRange), "IDWriteTextLayout::SetDrawingEffect");

    context_->BeginDraw();
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    context_->FillRectangle(D2D1::RectF(0, 0, size.width, size.height), backgroundBrush_.Get());
    context_->FillRectangle(BorderRect(size, placement_.edge), borderBrush_.Get());
    context_->DrawTextLayout(D2D1::Point2F(0, 0), layout.Get(), foregroundBrush_.Get());
    ThrowIfFailed(context_->EndDraw(), "ID2D1DeviceContext::EndDraw");

    ThrowIfFailed(swapChain_->Present(1, 0), "IDXGISwapChain1::Present");
}

void BarWindow::ScheduleClockTick() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    if (SetTimer(window_, kClockTimer, MillisecondsUntilNextMinute(now), nullptr) == 0) {
        ThrowLastError("SetTimer");
    }
}

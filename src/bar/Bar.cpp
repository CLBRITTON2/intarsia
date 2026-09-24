#include "bar/Bar.hpp"

#include "bar/Layout.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Win32Cast.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

using Microsoft::WRL::ComPtr;

namespace {

    constexpr wchar_t  kWindowClass[]  = L"IntarsiaBar";
    constexpr UINT     kAppBarCallback = WM_APP + 1;
    constexpr UINT_PTR kChangeTimer    = 1;

    int                DipsToPixels(float dips, UINT dpi) {
        return static_cast<int>(std::lround(dips * static_cast<float>(dpi) / USER_DEFAULT_SCREEN_DPI));
    }

    bool IsHorizontal(Edge edge) {
        return edge == Edge::Top || edge == Edge::Bottom;
    }

    float MainLength(D2D1_SIZE_F size, Edge edge) {
        return IsHorizontal(edge) ? size.width : size.height;
    }

    // Places a module at offset along the main axis, centered across the bar's thickness.
    D2D1_POINT_2F ModuleOrigin(Edge edge, float offset, D2D1_SIZE_F module, D2D1_SIZE_F bar) {
        if (IsHorizontal(edge)) {
            return D2D1::Point2F(offset, (bar.height - module.height) / 2.0f);
        }
        return D2D1::Point2F((bar.width - module.width) / 2.0f, offset);
    }

    // The border sits on the side facing the rest of the desktop.
    D2D1_RECT_F BorderRect(const D2D1_SIZE_F& size, Edge edge, float border) {
        switch (edge) {
            case Edge::Top: return D2D1::RectF(0, size.height - border, size.width, size.height);
            case Edge::Bottom: return D2D1::RectF(0, 0, size.width, border);
            case Edge::Left: return D2D1::RectF(size.width - border, 0, size.width, size.height);
            case Edge::Right: return D2D1::RectF(0, 0, border, size.height);
        }
        throw std::invalid_argument(std::format("unknown edge {}", static_cast<int>(edge)));
    }

    std::optional<UINT> EarliestChange(const Sections<std::unique_ptr<IModule>>& modules) {
        std::optional<UINT> earliest;
        for (const auto* section : {&modules.left, &modules.center, &modules.right}) {
            for (const std::unique_ptr<IModule>& module : *section) {
                const std::optional<UINT> wait = module->MillisecondsUntilChange();
                if (wait.has_value() && (!earliest.has_value() || *wait < *earliest)) {
                    earliest = wait;
                }
            }
        }
        return earliest;
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

void Bar::RegisterWindowClass(HINSTANCE instance) {
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

Bar::Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, const BarPlacement& placement, AppState& state) :
    graphics_(graphics), config_(config), state_(state), placement_(placement), modules_(CreateModules(config, sources)), window_(CreateBarWindow(instance, this)) {
    RequireFontFamily(graphics_.dwrite.Get(), config_.font.family.c_str());
    ThrowIfFailed(graphics_.dwrite->CreateTextFormat(config_.font.family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     config_.font.textSizeDips, L"en-us", &textFormat_),
                  "IDWriteFactory::CreateTextFormat");

    ThrowIfFailed(graphics_.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context_), "ID2D1Device::CreateDeviceContext");
    backgroundBrush_ = CreateBrush(context_.Get(), config_.colors.background);
    borderBrush_     = CreateBrush(context_.Get(), config_.colors.border);
    foregroundBrush_ = CreateBrush(context_.Get(), config_.colors.foreground);
    accentBrush_     = CreateBrush(context_.Get(), config_.colors.accent);
    alertBrush_      = CreateBrush(context_.Get(), config_.colors.alert);

    ThrowIfFailed(graphics_.composition->CreateTargetForHwnd(window_, TRUE, &compositionTarget_), "IDCompositionDevice::CreateTargetForHwnd");
    ThrowIfFailed(graphics_.composition->CreateVisual(&visual_), "IDCompositionDevice::CreateVisual");
    ThrowIfFailed(compositionTarget_->SetRoot(visual_.Get()), "IDCompositionTarget::SetRoot");

    RegisterAppBar(window_, kAppBarCallback);
    Dock();
    RECT client{};
    if (!GetClientRect(window_, &client)) {
        ThrowLastError("GetClientRect");
    }
    CreateSwapChain(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom));
    ShowWindow(window_, SW_SHOWNOACTIVATE);
    Render();
}

Bar::~Bar() {
    if (window_ != nullptr) {
        DestroyWindow(window_);
    }
}

LRESULT CALLBACK Bar::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* self = static_cast<Bar*>(IntToPointer<CREATESTRUCTW>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    auto* self = IntToPointer<Bar>(GetWindowLongPtrW(window, GWLP_USERDATA));
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
LRESULT Bar::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
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
            if (wParam == kChangeTimer) {
                Render();
            }
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_LBUTTONUP: Click(lParam); return 0;
        case WM_ACTIVATE: NotifyAppBarActivated(window); return 0;
        case WM_WINDOWPOSCHANGED: NotifyAppBarMoved(window); return DefWindowProcW(window, message, wParam, lParam);
        case kAppBarCallback:
            if (wParam == ABN_POSCHANGED) {
                Dock();
            }
            return 0;
        // Closing any bar (taskkill, Alt+F4) quits the app. A config reload destroys bars without WM_CLOSE.
        case WM_CLOSE: PostQuitMessage(0); return 0;
        case WM_DESTROY:
            KillTimer(window, kChangeTimer);
            UnregisterAppBar(window);
            // Later messages such as WM_NCDESTROY go straight to DefWindowProcW.
            SetWindowLongPtrW(window, GWLP_USERDATA, 0);
            window_ = nullptr;
            return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

void Bar::Dock() {
    const int  thickness = DipsToPixels(config_.bar.thicknessDips, placement_.dpi);
    const RECT rect      = ReserveAppBarRect(window_, placement_.monitorRect, config_.bar.edge, thickness);
    if (!MoveWindow(window_, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE)) {
        ThrowLastError("MoveWindow");
    }
}

void Bar::CreateSwapChain(UINT width, UINT height) {
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

void Bar::ResizeSwapChain(UINT width, UINT height) {
    // ResizeBuffers fails while D2D still holds a bitmap over the old back buffer.
    context_->SetTarget(nullptr);
    ThrowIfFailed(swapChain_->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "IDXGISwapChain1::ResizeBuffers");
    BindRenderTarget();
}

void Bar::BindRenderTarget() {
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

RenderContext Bar::FrameContext() const {
    return RenderContext{.context      = context_.Get(),
                         .dwrite       = graphics_.dwrite.Get(),
                         .textFormat   = textFormat_.Get(),
                         .foreground   = foregroundBrush_.Get(),
                         .accent       = accentBrush_.Get(),
                         .alert        = alertBrush_.Get(),
                         .iconSizeDips = config_.font.iconSizeDips};
}

void Bar::Render() {
    if (swapChain_ == nullptr) {
        return;
    }

    const RenderContext         render  = FrameContext();
    const Edge                  edge    = config_.bar.edge;
    const D2D1_SIZE_F           barSize = context_->GetSize();
    const Sections<D2D1_SIZE_F> sizes   = MapSections(modules_, [&render](const std::unique_ptr<IModule>& module) {
        module->Layout(render);
        return module->Size();
    });
    const Sections<float>       offsets =
        ArrangeSections(MapSections(sizes, [edge](D2D1_SIZE_F size) { return MainLength(size, edge); }), MainLength(barSize, edge), config_.bar.spacingDips);

    context_->BeginDraw();
    context_->Clear(D2D1::ColorF(0, 0, 0, 0));
    context_->FillRectangle(D2D1::RectF(0, 0, barSize.width, barSize.height), backgroundBrush_.Get());
    context_->FillRectangle(BorderRect(barSize, edge, config_.bar.borderDips), borderBrush_.Get());
    hitBoxes_.clear();
    for (const auto& boxes : {DrawSection(render, modules_.left, sizes.left, offsets.left, barSize), DrawSection(render, modules_.center, sizes.center, offsets.center, barSize),
                              DrawSection(render, modules_.right, sizes.right, offsets.right, barSize)}) {
        hitBoxes_.insert(hitBoxes_.end(), boxes.begin(), boxes.end());
    }
    ThrowIfFailed(context_->EndDraw(), "ID2D1DeviceContext::EndDraw");

    ThrowIfFailed(swapChain_->Present(1, 0), "IDXGISwapChain1::Present");
    ScheduleNextChange();
}

std::vector<Bar::HitBox> Bar::DrawSection(const RenderContext& render, const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<D2D1_SIZE_F>& sizes,
                                          const std::vector<float>& offsets, D2D1_SIZE_F barSize) const {
    std::vector<HitBox> boxes;
    boxes.reserve(modules.size());
    for (size_t index = 0; index < modules.size(); ++index) {
        const D2D1_POINT_2F origin = ModuleOrigin(config_.bar.edge, offsets[index], sizes[index], barSize);
        modules[index]->Draw(render, origin);
        boxes.push_back(HitBox{.module = modules[index].get(), .bounds = D2D1::RectF(origin.x, origin.y, origin.x + sizes[index].width, origin.y + sizes[index].height)});
    }
    return boxes;
}

// Mouse messages carry client pixels, and modules lay out in DIPs.
void Bar::Click(LPARAM position) {
    const float         scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(placement_.dpi);
    const D2D1_POINT_2F point = D2D1::Point2F(static_cast<float>(GET_X_LPARAM(position)) * scale, static_cast<float>(GET_Y_LPARAM(position)) * scale);
    for (const HitBox& box : hitBoxes_) {
        if (point.x >= box.bounds.left && point.x < box.bounds.right && point.y >= box.bounds.top && point.y < box.bounds.bottom) {
            box.module->Click(D2D1::Point2F(point.x - box.bounds.left, point.y - box.bounds.top));
            return;
        }
    }
}

void Bar::ScheduleNextChange() {
    const std::optional<UINT> wait = EarliestChange(modules_);
    if (!wait.has_value()) {
        KillTimer(window_, kChangeTimer);
        return;
    }
    if (SetTimer(window_, kChangeTimer, *wait, nullptr) == 0) {
        ThrowLastError("SetTimer");
    }
}

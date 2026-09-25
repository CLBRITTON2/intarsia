#include "bar/Bar.hpp"

#include "bar/Layout.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/WindowProc.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>
#include <string>

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
        ForEachInSections(modules, [&earliest](const std::unique_ptr<IModule>& module) {
            const std::optional<UINT> wait = module->MillisecondsUntilChange();
            if (wait.has_value() && (!earliest.has_value() || *wait < *earliest)) {
                earliest = wait;
            }
        });
        return earliest;
    }

    // The command an input runs, or nullopt when the config binds none to it.
    const std::optional<Command>& CommandFor(const ModuleActions& actions, ModuleInput input) {
        switch (input) {
            case ModuleInput::LeftClick: return actions.click;
            case ModuleInput::RightClick: return actions.rightClick;
            case ModuleInput::ScrollUp: return actions.scrollUp;
            case ModuleInput::ScrollDown: return actions.scrollDown;
        }
        throw std::invalid_argument(std::format("unknown module input {}", static_cast<int>(input)));
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

bool BarPlacement::operator==(const BarPlacement& other) const {
    return monitor == other.monitor && EqualRect(&monitorRect, &other.monitorRect) && dpi == other.dpi;
}

void Bar::RegisterWindowClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize        = sizeof(windowClass);
    windowClass.lpfnWndProc   = OwnedWindowProc<Bar>;
    windowClass.hInstance     = instance;
    windowClass.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kWindowClass;
    if (RegisterClassExW(&windowClass) == 0) {
        ThrowLastError("RegisterClassExW");
    }
}

Bar::Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, const BarPlacement& placement, AppState& state,
         HWND control, UINT deviceLostMessage) :
    _graphics(graphics), _config(config), _launcher(launcher), _state(state), _control(control), _deviceLostMessage(deviceLostMessage), _deviceLost(false), _presented(false),
    _placement(placement), _modules(CreateModules(config, sources)), _window(CreateBarWindow(instance, this)) {
    RequireFontFamily(_graphics.dwrite.Get(), _config.font.family.c_str());
    ThrowIfFailed(_graphics.dwrite->CreateTextFormat(_config.font.family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     _config.font.textSizeDips, L"en-us", &_textFormat),
                  "IDWriteFactory::CreateTextFormat");

    ThrowIfFailed(_graphics.d2d->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &_context), "ID2D1Device::CreateDeviceContext");
    _backgroundBrush = CreateBrush(_context.Get(), _config.colors.background);
    _borderBrush     = CreateBrush(_context.Get(), _config.colors.border);
    _foregroundBrush = CreateBrush(_context.Get(), _config.colors.foreground);
    _accentBrush     = CreateBrush(_context.Get(), _config.colors.accent);
    _alertBrush      = CreateBrush(_context.Get(), _config.colors.alert);

    ThrowIfFailed(_graphics.composition->CreateTargetForHwnd(_window, TRUE, &_compositionTarget), "IDCompositionDevice::CreateTargetForHwnd");
    ThrowIfFailed(_graphics.composition->CreateVisual(&_visual), "IDCompositionDevice::CreateVisual");
    ThrowIfFailed(_compositionTarget->SetRoot(_visual.Get()), "IDCompositionTarget::SetRoot");

    RegisterAppBar(_window, kAppBarCallback);
    Dock();
    RECT client{};
    if (!GetClientRect(_window, &client)) {
        ThrowLastError("GetClientRect");
    }
    CreateSwapChain(static_cast<UINT>(client.right), static_cast<UINT>(client.bottom));
    ShowWindow(_window, SW_SHOWNOACTIVATE);
    Render();
}

Bar::~Bar() {
    if (_window != nullptr) {
        DestroyWindow(_window);
    }
}

// Uses the handle it is given: messages sent during CreateWindowExW arrive before _window is assigned.
LRESULT Bar::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            // CreateWindowExW sends a zero size before the constructor has built the swap chain.
            if (_swapChain != nullptr && LOWORD(lParam) != 0 && HIWORD(lParam) != 0) {
                ResizeSwapChain(LOWORD(lParam), HIWORD(lParam));
                Render();
            }
            return 0;
        case WM_DPICHANGED:
            _placement.dpi = HIWORD(wParam);
            Dock();
            return 0;
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
        case WM_LBUTTONUP: HandleInput(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)}, ModuleInput::LeftClick); return 0;
        case WM_RBUTTONUP: HandleInput(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)}, ModuleInput::RightClick); return 0;
        case WM_MOUSEWHEEL: {
            // Wheel messages carry screen coordinates, unlike button messages.
            POINT point{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)};
            if (!ScreenToClient(window, &point)) {
                ThrowLastError("ScreenToClient");
            }
            HandleInput(point, GET_WHEEL_DELTA_WPARAM(wParam) > 0 ? ModuleInput::ScrollUp : ModuleInput::ScrollDown);
            return 0;
        }
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
            _window = nullptr;
            return 0;
        default: return DefWindowProcW(window, message, wParam, lParam);
    }
}

void Bar::Dock() {
    const int  thickness = DipsToPixels(_config.bar.thicknessDips, _placement.dpi);
    const RECT rect      = ReserveAppBarRect(_window, _placement.monitorRect, _config.bar.edge, thickness);
    if (!MoveWindow(_window, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE)) {
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

    ThrowIfFailed(_graphics.dxgiFactory->CreateSwapChainForComposition(_graphics.d3d.Get(), &description, nullptr, &_swapChain), "IDXGIFactory2::CreateSwapChainForComposition");
    ThrowIfFailed(_visual->SetContent(_swapChain.Get()), "IDCompositionVisual::SetContent");
    ThrowIfFailed(_graphics.composition->Commit(), "IDCompositionDevice::Commit");
    BindRenderTarget();
}

void Bar::ResizeSwapChain(UINT width, UINT height) {
    // ResizeBuffers fails while D2D still holds a bitmap over the old back buffer.
    _context->SetTarget(nullptr);
    if (RequireDevice(_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0), "IDXGISwapChain1::ResizeBuffers")) {
        BindRenderTarget();
    }
}

void Bar::BindRenderTarget() {
    ComPtr<IDXGISurface> surface;
    ThrowIfFailed(_swapChain->GetBuffer(0, IID_PPV_ARGS(&surface)), "IDXGISwapChain1::GetBuffer");

    const auto                    dpi        = static_cast<float>(_placement.dpi);
    const D2D1_BITMAP_PROPERTIES1 properties = D2D1::BitmapProperties1(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
                                                                       D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), dpi, dpi);
    ComPtr<ID2D1Bitmap1>          target;
    ThrowIfFailed(_context->CreateBitmapFromDxgiSurface(surface.Get(), &properties, &target), "ID2D1DeviceContext::CreateBitmapFromDxgiSurface");
    _context->SetTarget(target.Get());
    _context->SetDpi(dpi, dpi);
}

RenderContext Bar::FrameContext() const {
    return RenderContext{.context      = _context.Get(),
                         .dwrite       = _graphics.dwrite.Get(),
                         .textFormat   = _textFormat.Get(),
                         .foreground   = _foregroundBrush.Get(),
                         .accent       = _accentBrush.Get(),
                         .alert        = _alertBrush.Get(),
                         .iconSizeDips = _config.font.iconSizeDips};
}

bool Bar::DeviceLost() const {
    return _deviceLost;
}

bool Bar::HasPresented() const {
    return _presented;
}

void Bar::Render() {
    if (_swapChain == nullptr || _deviceLost) {
        return;
    }

    const RenderContext         render  = FrameContext();
    const Edge                  edge    = _config.bar.edge;
    const D2D1_SIZE_F           barSize = _context->GetSize();
    const Sections<D2D1_SIZE_F> sizes   = MapSections(_modules, [&render](const std::unique_ptr<IModule>& module) {
        module->Layout(render);
        return module->Size();
    });
    const Sections<float>       offsets =
        ArrangeSections(MapSections(sizes, [edge](D2D1_SIZE_F size) { return MainLength(size, edge); }), MainLength(barSize, edge), _config.bar.spacingDips);

    _context->BeginDraw();
    _context->Clear(D2D1::ColorF(0, 0, 0, 0));
    _context->FillRectangle(D2D1::RectF(0, 0, barSize.width, barSize.height), _backgroundBrush.Get());
    _context->FillRectangle(BorderRect(barSize, edge, _config.bar.borderDips), _borderBrush.Get());
    _hitBoxes.clear();
    for (const auto& boxes : {DrawSection(render, _modules.left, sizes.left, offsets.left, barSize), DrawSection(render, _modules.center, sizes.center, offsets.center, barSize),
                              DrawSection(render, _modules.right, sizes.right, offsets.right, barSize)}) {
        _hitBoxes.insert(_hitBoxes.end(), boxes.begin(), boxes.end());
    }
    if (!RequireDevice(_context->EndDraw(), "ID2D1DeviceContext::EndDraw") || !RequireDevice(_swapChain->Present(1, 0), "IDXGISwapChain1::Present")) {
        return;
    }
    _presented = true;
    ScheduleNextChange();
}

// A driver update, a driver crash or a GPU being removed all lose the device, and nothing drawn on it works again.
bool Bar::RequireDevice(HRESULT result, std::string_view call) {
    if (result != DXGI_ERROR_DEVICE_REMOVED && result != DXGI_ERROR_DEVICE_RESET && result != D2DERR_RECREATE_TARGET) {
        ThrowIfFailed(result, call);
        return true;
    }
    Log(LogLevel::Warning, "bar", "device_lost",
        {{.key = "call", .value = std::string(call)}, {.key = "result", .value = std::format("0x{:08X}", static_cast<unsigned long>(result))}});
    _deviceLost = true;
    KillTimer(_window, kChangeTimer);
    if (!PostMessageW(_control, _deviceLostMessage, 0, 0)) {
        ThrowLastError("PostMessageW(device lost)");
    }
    return false;
}

std::vector<Bar::HitBox> Bar::DrawSection(const RenderContext& render, const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<D2D1_SIZE_F>& sizes,
                                          const std::vector<float>& offsets, D2D1_SIZE_F barSize) const {
    std::vector<HitBox> boxes;
    boxes.reserve(modules.size());
    for (size_t index = 0; index < modules.size(); ++index) {
        const D2D1_POINT_2F origin = ModuleOrigin(_config.bar.edge, offsets[index], sizes[index], barSize);
        modules[index]->Draw(render, origin);
        boxes.push_back(HitBox{.module = modules[index].get(), .bounds = D2D1::RectF(origin.x, origin.y, origin.x + sizes[index].width, origin.y + sizes[index].height)});
    }
    return boxes;
}

// Mouse positions are client pixels, and modules lay out in DIPs.
void Bar::HandleInput(POINT client, ModuleInput input) {
    const float         scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(_placement.dpi);
    const D2D1_POINT_2F point = D2D1::Point2F(static_cast<float>(client.x) * scale, static_cast<float>(client.y) * scale);
    for (const HitBox& box : _hitBoxes) {
        if (point.x < box.bounds.left || point.x >= box.bounds.right || point.y < box.bounds.top || point.y >= box.bounds.bottom) {
            continue;
        }
        const std::optional<Command>& command = CommandFor(box.module->Actions(), input);
        if (command.has_value()) {
            _launcher.Launch(*command);
        } else if (input == ModuleInput::LeftClick) {
            box.module->Click(D2D1::Point2F(point.x - box.bounds.left, point.y - box.bounds.top));
        }
        return;
    }
}

void Bar::ScheduleNextChange() {
    const std::optional<UINT> wait = EarliestChange(_modules);
    if (!wait.has_value()) {
        KillTimer(_window, kChangeTimer);
        return;
    }
    if (SetTimer(_window, kChangeTimer, *wait, nullptr) == 0) {
        ThrowLastError("SetTimer");
    }
}

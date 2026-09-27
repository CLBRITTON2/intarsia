#include "bar/Bar.hpp"

#include "bar/Layout.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/WindowProc.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <cstdint>
#include <algorithm>
#include <format>
#include <stdexcept>
#include <string>

namespace {

    constexpr wchar_t       kWindowClass[]  = L"IntarsiaBar";
    constexpr std::uint32_t kAppBarCallback = WM_APP + 1;
    constexpr UINT_PTR      kChangeTimer    = 1;

    bool                    IsHorizontal(Edge edge) {
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
        throw std::invalid_argument(std::format("unknown edge {}", static_cast<std::int32_t>(edge)));
    }

    std::optional<std::uint32_t> EarliestChange(const Sections<std::unique_ptr<IModule>>& modules) {
        std::optional<std::uint32_t> earliest;
        ForEachInSections(modules, [&earliest](const std::unique_ptr<IModule>& module) {
            const std::optional<std::uint32_t> wait = module->MillisecondsUntilChange();
            if (wait.has_value() && (!earliest.has_value() || *wait < *earliest)) {
                earliest = wait;
            }
        });
        return earliest;
    }

    // The action an input runs, or nullopt when the config binds none to it.
    const std::optional<ModuleAction>& ActionFor(const ModuleActions& actions, ModuleInput input) {
        switch (input) {
            case ModuleInput::LeftClick: return actions.click;
            case ModuleInput::RightClick: return actions.rightClick;
            case ModuleInput::ScrollUp: return actions.scrollUp;
            case ModuleInput::ScrollDown: return actions.scrollDown;
        }
        throw std::invalid_argument(std::format("unknown module input {}", static_cast<std::int32_t>(input)));
    }

    HWND LayerZOrder(Layer layer) {
        switch (layer) {
            case Layer::Top: return HWND_TOPMOST;
            case Layer::Bottom: return HWND_BOTTOM;
        }
        throw std::invalid_argument(std::format("unknown layer {}", static_cast<std::int32_t>(layer)));
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
         HWND control, std::uint32_t deviceLostMessage) :
    _graphics(graphics), _config(config), _launcher(launcher), _windowManager(sources.windowManager), _state(state), _control(control), _deviceLostMessage(deviceLostMessage),
    _placement(placement), _modules(CreateModules(config, sources)), _window(CreateBarWindow(instance, this)) {
    // No destructor runs for a constructor that throws, and App survives a lost device, so the window and its appbar
    // strip would outlive the failed bar.
    try {
        Build(instance);
    } catch (...) {
        DestroyWindow(_window);
        throw;
    }
}

void Bar::Build(HINSTANCE instance) {
    RequireFontFamily(_graphics.dwrite.Get(), _config.font.family.c_str());
    ThrowIfFailed(_graphics.dwrite->CreateTextFormat(_config.font.family.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     _config.font.textSizeDips, L"en-us", &_textFormat),
                  "IDWriteFactory::CreateTextFormat");

    RegisterAppBar(_window, kAppBarCallback);
    Dock();
    PlaceInZOrder(LayerZOrder(_config.bar.layer));
    RECT client{};
    if (!GetClientRect(_window, &client)) {
        ThrowLastError("GetClientRect");
    }
    _surface         = std::make_unique<Surface>(_graphics, _window, static_cast<std::uint32_t>(client.right), static_cast<std::uint32_t>(client.bottom), _placement.dpi);
    _backgroundBrush = CreateBrush(_surface->Context(), _config.colors.background);
    _borderBrush     = CreateBrush(_surface->Context(), _config.colors.border);
    _foregroundBrush = CreateBrush(_surface->Context(), _config.colors.foreground);
    _accentBrush     = CreateBrush(_surface->Context(), _config.colors.accent);
    _alertBrush      = CreateBrush(_surface->Context(), _config.colors.alert);
    _tooltip         = std::make_unique<Tooltip>(instance, _graphics, _config, _textFormat.Get());
    ShowWindow(_window, SW_SHOWNOACTIVATE);
    Render();
}

Bar::~Bar() {
    if (_window != nullptr) {
        DestroyWindow(_window);
    }
}

LRESULT Bar::HandleMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    try {
        return Dispatch(window, message, wParam, lParam);
    } catch (const DeviceLostError& error) {
        ReportDeviceLost(error);
        return 0;
    }
}

// Uses the handle it is given: messages sent during CreateWindowExW arrive before _window is assigned.
LRESULT Bar::Dispatch(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            // CreateWindowExW and Dock send sizes before the constructor has built the surface.
            if (_surface != nullptr && !_deviceLost && LOWORD(lParam) != 0 && HIWORD(lParam) != 0) {
                _surface->Resize(LOWORD(lParam), HIWORD(lParam), _placement.dpi);
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
                Refresh();
            }
            return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEMOVE: PointAt(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)}); return 0;
        case WM_MOUSEHOVER: ShowTooltip(); return 0;
        case WM_MOUSELEAVE:
            _pointed = nullptr;
            _tooltip->Hide();
            return 0;
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
            } else if (wParam == ABN_FULLSCREENAPP && _config.bar.layer == Layer::Top) {
                // HWND_BOTTOM also drops topmost, so the fullscreen app covers the bar until it closes.
                PlaceInZOrder(lParam != 0 ? HWND_BOTTOM : HWND_TOPMOST);
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
    const std::int32_t thickness = DipsToPixels(_config.bar.thicknessDips, _placement.dpi);
    const RECT         rect      = ReserveAppBarRect(_window, _placement.monitorRect, _config.bar.edge, thickness);
    if (!MoveWindow(_window, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE)) {
        ThrowLastError("MoveWindow");
    }
}

void Bar::PlaceInZOrder(HWND insertAfter) {
    if (!SetWindowPos(_window, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
        ThrowLastError("SetWindowPos(z-order)");
    }
}

RenderContext Bar::FrameContext() const {
    return RenderContext{.context      = _surface->Context(),
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

// Catches a lost device itself because App calls it outside the window procedure.
void Bar::Render() {
    if (_surface == nullptr || _deviceLost) {
        return;
    }
    try {
        LayOutModules();
        Draw();
    } catch (const DeviceLostError& error) {
        ReportDeviceLost(error);
        return;
    }
    ScheduleNextChange();
}

void Bar::Refresh() {
    if (_surface == nullptr || _deviceLost) {
        return;
    }
    try {
        if (LayOutModules()) {
            Draw();
        }
    } catch (const DeviceLostError& error) {
        ReportDeviceLost(error);
        return;
    }
    ScheduleNextChange();
}

bool Bar::LayOutModules() {
    const RenderContext render  = FrameContext();
    bool                changed = false;
    // Every module lays out, so none is left holding stale content.
    ForEachInSections(_modules, [&render, &changed](const std::unique_ptr<IModule>& module) { changed = module->Layout(render) || changed; });
    return changed;
}

void Bar::Draw() {
    const RenderContext         render  = FrameContext();
    const Edge                  edge    = _config.bar.edge;
    const D2D1_SIZE_F           barSize = render.context->GetSize();
    const Sections<D2D1_SIZE_F> sizes   = MapSections(_modules, [](const std::unique_ptr<IModule>& module) { return module->Size(); });
    const Sections<float>       offsets =
        ArrangeSections(MapSections(sizes, [edge](D2D1_SIZE_F size) { return MainLength(size, edge); }), MainLength(barSize, edge), _config.bar.spacingDips);

    render.context->BeginDraw();
    render.context->Clear(D2D1::ColorF(0, 0, 0, 0));
    render.context->FillRectangle(D2D1::RectF(0, 0, barSize.width, barSize.height), _backgroundBrush.Get());
    render.context->FillRectangle(BorderRect(barSize, edge, _config.bar.borderDips), _borderBrush.Get());
    _hitBoxes.clear();
    for (const auto& boxes : {DrawSection(render, _modules.left, sizes.left, offsets.left, barSize), DrawSection(render, _modules.center, sizes.center, offsets.center, barSize),
                              DrawSection(render, _modules.right, sizes.right, offsets.right, barSize)}) {
        _hitBoxes.insert(_hitBoxes.end(), boxes.begin(), boxes.end());
    }
    _surface->EndDrawAndPresent();
    _presented = true;
}

void Bar::ReportDeviceLost(const DeviceLostError& error) {
    if (_deviceLost) {
        return;
    }
    Log(LogLevel::Warning, "bar", "device_lost", {{.key = "reason", .value = error.what()}});
    _deviceLost = true;
    KillTimer(_window, kChangeTimer);
    _tooltip->Hide();
    if (!PostMessageW(_control, _deviceLostMessage, 0, 0)) {
        ThrowLastError("PostMessageW(device lost)");
    }
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
const Bar::HitBox* Bar::BoxAt(POINT client) const {
    const float         scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(_placement.dpi);
    const D2D1_POINT_2F point = D2D1::Point2F(static_cast<float>(client.x) * scale, static_cast<float>(client.y) * scale);
    const auto          box   = std::ranges::find_if(_hitBoxes, [point](const HitBox& candidate) {
        return point.x >= candidate.bounds.left && point.x < candidate.bounds.right && point.y >= candidate.bounds.top && point.y < candidate.bounds.bottom;
    });
    return box == _hitBoxes.end() ? nullptr : &*box;
}

void Bar::HandleInput(POINT client, ModuleInput input) {
    _tooltip->Hide();
    const HitBox* box = BoxAt(client);
    if (box == nullptr) {
        return;
    }
    const std::optional<ModuleAction>& action = ActionFor(box->module->Actions(), input);
    if (const auto* command = action.has_value() ? std::get_if<Command>(&*action) : nullptr) {
        _launcher.Launch(*command);
    } else if (const auto* windowManagerCommand = action.has_value() ? std::get_if<WindowManagerCommand>(&*action) : nullptr) {
        _windowManager.RunCommand(windowManagerCommand->text);
    } else if (input == ModuleInput::LeftClick) {
        const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(_placement.dpi);
        box->module->Click(D2D1::Point2F((static_cast<float>(client.x) * scale) - box->bounds.left, (static_cast<float>(client.y) * scale) - box->bounds.top));
    }
}

void Bar::ScheduleNextChange() {
    const std::optional<std::uint32_t> wait = EarliestChange(_modules);
    if (!wait.has_value()) {
        KillTimer(_window, kChangeTimer);
        return;
    }
    if (SetTimer(_window, kChangeTimer, *wait, nullptr) == 0) {
        ThrowLastError("SetTimer");
    }
}

void Bar::PointAt(POINT client) {
    const HitBox* box    = BoxAt(client);
    IModule*      module = box == nullptr ? nullptr : box->module;
    if (module == _pointed) {
        return;
    }
    _pointed = module;
    _tooltip->Hide();
    TRACKMOUSEEVENT track{.cbSize = sizeof(TRACKMOUSEEVENT), .dwFlags = TME_HOVER | TME_LEAVE, .hwndTrack = _window, .dwHoverTime = HOVER_DEFAULT};
    if (!TrackMouseEvent(&track)) {
        ThrowLastError("TrackMouseEvent");
    }
}

void Bar::ShowTooltip() {
    const auto box = std::ranges::find(_hitBoxes, _pointed, &HitBox::module);
    if (_pointed == nullptr || box == _hitBoxes.end()) {
        return;
    }
    const std::optional<std::wstring> text = _pointed->TooltipText();
    if (!text.has_value() || text->empty()) {
        return;
    }
    RECT bar{};
    if (!GetWindowRect(_window, &bar)) {
        ThrowLastError("GetWindowRect");
    }
    const std::uint32_t dpi = _placement.dpi;
    const RECT          module{.left   = bar.left + DipsToPixels(box->bounds.left, dpi),
                               .top    = bar.top + DipsToPixels(box->bounds.top, dpi),
                               .right  = bar.left + DipsToPixels(box->bounds.right, dpi),
                               .bottom = bar.top + DipsToPixels(box->bounds.bottom, dpi)};
    _tooltip->Show(*text, TooltipAnchor{.module = module, .bar = bar, .monitor = _placement.monitorRect, .edge = _config.bar.edge, .dpi = dpi});
}

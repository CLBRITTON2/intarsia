#include "bar/Bar.hpp"

#include "bar/Layout.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"
#include "render/Axis.hpp"
#include "render/Box.hpp"
#include "render/FrameClock.hpp"
#include "render/Graphics.hpp"

#include <shellapi.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace {

    BarPlacement PlacementForMonitor(HMONITOR monitor) {
        std::uint32_t dpiX     = 0;
        std::uint32_t dpiY     = 0;
        const HRESULT dpiFound = GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
        if (FAILED(dpiFound)) {
            throw MonitorGoneError(std::format("GetDpiForMonitor failed with 0x{:08X} for a listed handle", static_cast<std::uint32_t>(dpiFound)));
        }
        return BarPlacement{
            .monitor     = monitor,
            .monitorRect = MonitorInfo(monitor).rcMonitor,
            .dpi         = dpiX,
        };
    }

    float EdgeLengthPixels(const RECT& monitorRect, Edge edge) {
        if (AxisOf(edge) == Axis::Horizontal) {
            return static_cast<float>(monitorRect.right - monitorRect.left);
        }
        return static_cast<float>(monitorRect.bottom - monitorRect.top);
    }

    RECT ReserveRect(HWND window, const RECT& monitorRect, Edge edge, std::int32_t thickness, AppBarRoute route) {
        switch (route) {
            case AppBarRoute::Shell: return ReserveAppBarRect(window, monitorRect, edge, thickness);
            case AppBarRoute::Taskbar: {
                HWND taskbar = ExplorerTaskbar(kTaskbarClass);
                // No Explorer, no one to reserve space with: SHAppBarMessage would find no taskbar either.
                if (taskbar == nullptr) {
                    Log(LogLevel::Warning, "bar", "taskbar_missing", {});
                    return DockedRect(monitorRect, edge, thickness);
                }
                return ReserveAppBarRectThroughTaskbar(window, monitorRect, edge, thickness, taskbar);
            }
        }
        throw std::invalid_argument(std::format("unknown appbar route {}", static_cast<std::int32_t>(route)));
    }

    // The whole monitor's length along the bar's edge, which picks the config's layout.
    float EdgeLengthDips(const BarPlacement& placement, Edge edge) {
        return PixelsToDips(EdgeLengthPixels(placement.monitorRect, edge), placement.dpi);
    }

    // Where SetWindowPos puts a bar of layer in the z-order.
    HWND LayerZOrder(Layer layer) {
        switch (layer) {
            case Layer::Top: return HWND_TOPMOST;
            case Layer::Bottom: return HWND_BOTTOM;
        }
        throw std::invalid_argument(std::format("unknown layer {}", static_cast<std::int32_t>(layer)));
    }

    // The bar window's rect on monitorRect, thickness pixels across, reserved with the shell through route when bar
    // reserves space. A shared bar stays a registered appbar, so the shell still tells it of fullscreen apps and of other
    // appbars moving.
    RECT BarRect(HWND window, const RECT& monitorRect, const BarConfig& bar, std::int32_t thickness, AppBarRoute route) {
        switch (bar.space) {
            case Space::Reserve: return ReserveRect(window, monitorRect, bar.edge, thickness, route);
            case Space::Share: return DockedRect(monitorRect, bar.edge, thickness);
        }
        throw std::invalid_argument(std::format("unknown space {}", static_cast<std::int32_t>(bar.space)));
    }

    constexpr wchar_t       kWindowClass[]    = L"IntarsiaBar";
    constexpr std::uint32_t kAppBarCallback   = WM_APP + 1;
    constexpr std::uint32_t kAnimationFrame   = WM_APP + 2;
    constexpr std::uint32_t kFrameClockFailed = WM_APP + 3;
    constexpr std::uint32_t kMenuListed       = WM_APP + 4;
    constexpr UINT_PTR      kChangeTimer      = 1;

    HWND                    CreateBarWindow(HINSTANCE instance, void* owner) {
        // No activation so clicking the bar never steals focus from the tiled window.
        return CreatePopupWindow(instance, kWindowClass, L"intarsia", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, owner, "bar");
    }

    // What a module's box is filled with: its background, or its hover under the cursor. A group's hover lights its
    // toggle alone, over the group's background, so a group's fill is the toggle's and clear at rest.
    D2D1_COLOR_F BoxFill(const ModuleEntry& entry, bool hovered) {
        if (hovered) {
            return entry.style.hover;
        }
        if (entry.group != nullptr) {
            return D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f);
        }
        return entry.style.box.background;
    }

    std::unordered_map<const IModule*, ColorFade> RestingBoxFades(const Sections<ModuleEntry>& modules) {
        std::unordered_map<const IModule*, ColorFade> fades;
        ForEachEntry(modules, [&fades](const ModuleEntry& entry) { fades.emplace(entry.module.get(), ColorFade(BoxFill(entry, false))); });
        return fades;
    }

    std::optional<std::uint32_t> EarliestChange(const Sections<ModuleEntry>& modules) {
        std::optional<std::uint32_t> earliest;
        ForEachEntry(modules, [&earliest](const ModuleEntry& entry) {
            const std::optional<std::uint32_t> wait = entry.module->MillisecondsUntilChange();
            if (wait.has_value() && (!earliest.has_value() || *wait < *earliest)) {
                earliest = wait;
            }
        });
        return earliest;
    }

    // A fullscreen app opening (lParam nonzero) drops the bar to the bottom, which also drops topmost, so the app covers
    // the bar until it closes.
    HWND FullscreenZOrder(LPARAM lParam) {
        if (lParam != 0) {
            return HWND_BOTTOM;
        }
        return HWND_TOPMOST;
    }

    ModuleInput WheelInput(WPARAM wParam) {
        if (GET_WHEEL_DELTA_WPARAM(wParam) > 0) {
            return ModuleInput::ScrollUp;
        }
        return ModuleInput::ScrollDown;
    }

    // For a scroll input alone.
    ScrollDirection ScrollDirectionOf(ModuleInput input) {
        if (input == ModuleInput::ScrollUp) {
            return ScrollDirection::Up;
        }
        return ScrollDirection::Down;
    }

    IModule* ModuleOf(const HitBox* box) {
        if (box == nullptr) {
            return nullptr;
        }
        return box->module;
    }

    // Against the item the text describes, such as one tray icon, or else the whole module.
    D2D1_RECT_F TooltipSpan(const std::optional<D2D1_RECT_F>& itemBox, const HitBox& box) {
        if (itemBox.has_value()) {
            return Offset(*itemBox, box.contentOrigin);
        }
        return box.bounds;
    }

} // namespace

bool BarPlacement::operator==(const BarPlacement& other) const {
    return monitor == other.monitor && EqualRect(&monitorRect, &other.monitorRect) && dpi == other.dpi;
}

std::vector<BarPlacement> PlacementsFor(const std::vector<HMONITOR>& monitors) {
    std::vector<BarPlacement> placements;
    placements.reserve(monitors.size());
    for (HMONITOR monitor : monitors) {
        placements.push_back(PlacementForMonitor(monitor));
    }
    return placements;
}

// A monitor swapped for another at the same place and scale changes only its description. A monitor that goes while it
// is read has changed too.
bool MonitorsDiffer(const std::vector<BarPlacement>& placements, const std::vector<MonitorDescription>& descriptions) {
    try {
        const std::vector<HMONITOR> monitors = ListMonitors();
        return PlacementsFor(monitors) != placements || DescribeMonitors(monitors) != descriptions;
    } catch (const MonitorGoneError&) { return true; }
}

void Bar::RegisterWindowClass(HINSTANCE instance) {
    ::RegisterWindowClass(instance, kWindowClass, OwnedWindowProc<Bar>, LoadCursorW(nullptr, IDC_ARROW), "bar");
}

Bar::Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, const BarPlacement& placement,
         AppBarRoute route, WindowProcError& windowProcError, HWND control, std::uint32_t deviceLostMessage) :
    _graphics(graphics), _config(config), _axis(AxisOf(config.bar.edge)), _launcher(launcher), _windowManager(sources.windowManager), _windowProcError(windowProcError),
    _control(control), _deviceLostMessage(deviceLostMessage), _placement(placement), _route(route),
    _modules(CreateModules(ModulesForEdgeLength(config, EdgeLengthDips(placement, config.bar.edge)), sources)), _taskbarCreated(TaskbarCreatedMessage()),
    _window(CreateBarWindow(instance, this)) {
    // No destructor runs for a constructor that throws, and App survives a lost device, so the window and its appbar
    // strip would outlive the failed bar.
    try {
        Build(instance);
    } catch (...) {
        // The clock's thread posts to the window, so it stops first.
        _frames.reset();
        DestroyWindow(_window);
        throw;
    }
}

void Bar::Build(HINSTANCE instance) {
    RequireFontFamily(_graphics.dwrite.Get(), _config.style.font.c_str());
    ForEachEntry(_modules, [this](const ModuleEntry& entry) { RequireFontFamily(_graphics.dwrite.Get(), entry.style.icon.font.c_str()); });
    ThrowIfFailed(_graphics.dwrite->CreateTextFormat(_config.style.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     _config.style.fontSizeDips, L"en-us", &_textFormat),
                  "IDWriteFactory::CreateTextFormat");

    const MonitorOutput output = OutputForMonitor(_placement.monitor);
    Log(LogLevel::Info, "bar", "placed",
        {{.key = "monitor", .value = RectText(_placement.monitorRect)},
         {.key = "dpi", .value = std::to_string(_placement.dpi)},
         {.key = "refresh_hz", .value = std::to_string(output.refreshHz)},
         {.key = "adapter", .value = ToUtf8(output.adapter)}});
    _frames = std::make_unique<FrameClock>(_window, kAnimationFrame, kFrameClockFailed, output.output);
    RegisterAppBar(_window, kAppBarCallback);
    Dock();
    PlaceInZOrder(LayerZOrder(_config.bar.layer));
    RECT client{};
    if (!GetClientRect(_window, &client)) {
        ThrowLastError("GetClientRect");
    }
    _canvas = std::make_unique<Canvas>(_graphics, _window, static_cast<std::uint32_t>(client.right), static_cast<std::uint32_t>(client.bottom), _placement.dpi);
    BuildContexts();
    _boxFades   = RestingBoxFades(_modules);
    _wallpapers = BlurWallpapers(_graphics, _config.style, _placement.monitorRect, _placement.dpi);
    _tooltip    = std::make_unique<Tooltip>(instance, _graphics, _config, _textFormat.Get(), _wallpapers);
    _menu       = std::make_unique<Menu>(instance, _graphics, _config, _textFormat.Get(), _launcher, _windowProcError, *_tooltip, _wallpapers,
                                         [this](const DeviceLostError& error) { ReportDeviceLost(error); });
    _lister     = std::make_unique<MenuLister>(_window, kMenuListed);
    ShowWindow(_window, SW_SHOWNOACTIVATE);
    Render();
}

Bar::~Bar() {
    // The clock's and the lister's threads post to the window, so they stop first.
    _frames.reset();
    _lister.reset();
    if (_window != nullptr) {
        DestroyWindow(_window);
    }
}

bool Bar::DeviceLost() const {
    return _deviceLost;
}

bool Bar::HasPresented() const {
    return _presented;
}

void Bar::ReloadWallpaper() {
    if (_deviceLost) {
        return;
    }
    try {
        _wallpapers = BlurWallpapers(_graphics, _config.style, _placement.monitorRect, _placement.dpi);
        Render();
    } catch (const DeviceLostError& error) { ReportDeviceLost(error); }
}

void Bar::ReportDeviceLost(const DeviceLostError& error) {
    if (_deviceLost) {
        return;
    }
    Log(LogLevel::Warning, "bar", "device_lost", {{.key = "reason", .value = error.what()}});
    _deviceLost = true;
    KillTimer(_window, kChangeTimer);
    _tooltip->Hide();
    _menu->Hide();
    if (!PostMessageW(_control, _deviceLostMessage, 0, 0)) {
        ThrowLastError("PostMessageW(device lost)");
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
    // Removed first: any app can broadcast it (the bar's own tray does) while Explorer still holds the registration, and
    // ABM_NEW refuses a window it holds.
    if (message == _taskbarCreated) {
        UnregisterAppBar(window);
        RegisterAppBar(window, kAppBarCallback);
        Dock();
        return 0;
    }
    switch (message) {
        case WM_SIZE: ResizeCanvas(); return 0;
        case WM_DPICHANGED:
            _placement.dpi = HIWORD(wParam);
            // The wallpaper's radius is in DIPs, so it blurs over other pixels.
            _wallpapers = BlurWallpapers(_graphics, _config.style, _placement.monitorRect, _placement.dpi);
            Dock();
            // Dock's WM_SIZE only comes when the bar's pixel size changed with the DPI.
            ResizeCanvas();
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
        case kAnimationFrame: {
            // A frame drawn just before a transition's end shows it short of its end, so the clock stops only after a
            // frame drawn once nothing ran.
            const bool settled = !_animating;
            Render();
            // Only after drawing: GetMessage takes posted messages before input, so a frame posted while the bar drew
            // would starve clicks until the animation ended.
            _frames->Acknowledge();
            if ((settled && !_animating) || _deviceLost) {
                _frames->Stop();
            }
            return 0;
        }
        case kFrameClockFailed: _frames->RethrowFailure(); return 0;
        case kMenuListed: ShowMenu(); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEMOVE: MoveCursor(ClientPoint(lParam)); return 0;
        case WM_MOUSEHOVER: ShowTooltip(); return 0;
        case WM_MOUSELEAVE:
            if (_hovered != nullptr) {
                _hovered->Hover(std::nullopt);
            }
            _hovered = nullptr;
            _cursor  = std::nullopt;
            FadeBoxesToHover();
            _tooltip->Hide();
            // Draws the box that was under the cursor without its hover.
            Redraw();
            return 0;
        case WM_LBUTTONUP: HandleInput(ClientPoint(lParam), ModuleInput::LeftClick); return 0;
        case WM_RBUTTONUP: HandleInput(ClientPoint(lParam), ModuleInput::RightClick); return 0;
        case WM_MBUTTONUP: HandleInput(ClientPoint(lParam), ModuleInput::MiddleClick); return 0;
        case WM_MOUSEWHEEL: {
            // Wheel messages carry screen coordinates, unlike button messages.
            POINT point{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)};
            if (!ScreenToClient(window, &point)) {
                ThrowLastError("ScreenToClient");
            }
            HandleInput(point, WheelInput(wParam));
            return 0;
        }
        case WM_ACTIVATE: NotifyAppBar(window, ABM_ACTIVATE); return 0;
        case WM_WINDOWPOSCHANGED: NotifyAppBar(window, ABM_WINDOWPOSCHANGED); return DefWindowProcW(window, message, wParam, lParam);
        case kAppBarCallback:
            if (wParam == ABN_POSCHANGED) {
                Dock();
            } else if (wParam == ABN_FULLSCREENAPP && _config.bar.layer == Layer::Top) {
                PlaceInZOrder(FullscreenZOrder(lParam));
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
    const RECT         barRect   = BarRect(_window, _placement.monitorRect, _config.bar, thickness, _route);
    if (!MoveWindow(_window, barRect.left, barRect.top, barRect.right - barRect.left, barRect.bottom - barRect.top, TRUE)) {
        ThrowLastError("MoveWindow");
    }
}

void Bar::ResizeCanvas() {
    // CreateWindowExW and Dock send sizes before the constructor has built the canvas.
    if (_canvas == nullptr || _deviceLost) {
        return;
    }
    RECT client{};
    if (!GetClientRect(_window, &client)) {
        ThrowLastError("GetClientRect");
    }
    if (client.right == 0 || client.bottom == 0) {
        return;
    }
    _canvas->surface.Resize(static_cast<std::uint32_t>(client.right), static_cast<std::uint32_t>(client.bottom), _placement.dpi);
    BuildContexts();
    Render();
}

void Bar::PlaceInZOrder(HWND insertAfter) {
    if (!SetWindowPos(_window, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
        ThrowLastError("SetWindowPos(z-order)");
    }
}

// Catches a lost device itself because App calls it outside the window procedure.
void Bar::Render() {
    if (_canvas == nullptr || _deviceLost) {
        return;
    }
    StampFrameTime();
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
    if (_canvas == nullptr || _deviceLost) {
        return;
    }
    StampFrameTime();
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

void Bar::Redraw() {
    if (_canvas == nullptr || _deviceLost) {
        return;
    }
    StampFrameTime();
    try {
        Draw();
    } catch (const DeviceLostError& error) {
        ReportDeviceLost(error);
        return;
    }
    ScheduleNextChange();
}

void Bar::StampFrameTime() {
    _frameTime = NextFrameTime();
    for (auto& [style, context] : _contexts) {
        context.frameTime = _frameTime;
    }
}

// While the clock runs, the latest refresh, never earlier than a frame already drawn (a frame drawn between the clock's
// start and its first blank reads the last animation's). Otherwise a change outside any animation, drawn now.
std::chrono::steady_clock::time_point Bar::NextFrameTime() const {
    if (_animating) {
        return std::max(_frames->LastVBlank(), _frameTime);
    }
    return std::chrono::steady_clock::now();
}

void Bar::ScheduleNextChange() {
    const std::optional<std::uint32_t> wait = NextChangeWait();
    _animating                              = wait == 0u;
    if (!_animating && _animationFrames.has_value()) {
        LogAnimationFrames(*_animationFrames, _placement.monitorRect);
        _animationFrames.reset();
    }
    if (!wait.has_value() || _animating) {
        KillTimer(_window, kChangeTimer);
        // A timer ticks at the system's 15.6 ms, off the display's refresh, so an animation follows the frame clock.
        if (_animating) {
            _frames->Start();
        }
        return;
    }
    StartTimer(_window, kChangeTimer, *wait, "bar change");
}

std::optional<std::uint32_t> Bar::NextChangeWait() const {
    if (BoxFading()) {
        return 0u;
    }
    return EarliestChange(_modules);
}

bool Bar::BoxFading() const {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    return std::ranges::any_of(_boxFades, [now](const auto& fade) { return fade.second.Running(now); });
}

void Bar::BuildContexts() {
    _contexts.clear();
    ForEachEntry(_modules, [this](const ModuleEntry& entry) { _contexts.emplace(&entry.style, ModuleContext(entry.style)); });
}

RenderContext Bar::ModuleContext(const ModuleStyle& style) const {
    BrushCache&               brushes = _canvas->brushes;
    std::vector<StateBrushes> classes;
    classes.reserve(style.classes.size());
    for (const ClassColors& declared : style.classes) {
        classes.push_back(StateBrushes{.text = brushes.Keep(declared.colors.text), .icon = brushes.Keep(declared.colors.icon)});
    }
    return RenderContext{
        .context      = _canvas->surface.Context(),
        .dwrite       = _graphics.dwrite.Get(),
        .textFormat   = _textFormat.Get(),
        .iconFont     = style.icon.font.c_str(),
        .iconSizeDips = style.icon.sizeDips,
        .iconGapDips  = style.icon.gapDips,
        .foreground   = brushes.Keep(style.color),
        .accent       = brushes.Keep(style.icon.color),
        .alert        = brushes.Keep(style.alertColor),
        .warning      = {.text = brushes.Keep(style.warning.text), .icon = brushes.Keep(style.warning.icon)},
        .critical     = {.text = brushes.Keep(style.critical.text), .icon = brushes.Keep(style.critical.icon)},
        .muted        = {.text = brushes.Keep(style.muted.text), .icon = brushes.Keep(style.muted.icon)},
        .classes      = std::move(classes),
        .brushes      = &brushes,
        .boxes        = &_canvas->boxes,
        .axis         = _axis,
        .boxCrossDips = BoxCrossLength(Geometry()) - (2.0f * CrossOf(style.margin, _axis)),
        .frameTime    = _frameTime,
    };
}

bool Bar::LayOutModules() {
    bool changed = false;
    // Every module lays out, so none is left holding stale content.
    ForEachEntry(_modules, [this, &changed](const ModuleEntry& entry) { changed = entry.module->Layout(_contexts.at(&entry.style)) || changed; });
    return changed;
}

BarGeometry Bar::Geometry() const {
    return BarGeometry{
        .axis    = _axis,
        .section = _config.style.section,
        .size    = _canvas->surface.Context()->GetSize(),
    };
}

PlacedBar Bar::PlaceModules() {
    PlacedBar placed      = PlaceBar(Geometry(), _modules);
    _hitBoxes             = HitBoxes(placed.entries);
    const RECT windowRect = WindowRect();
    for (const HitBox& box : _hitBoxes) {
        box.module->Place(ModulePlacement{
            .windowRect    = windowRect,
            .contentOrigin = box.contentOrigin,
            .dpi           = _placement.dpi,
        });
    }
    // Items move under a cursor that stays still, as a tray unfolds, and no WM_MOUSEMOVE says so.
    if (_cursor.has_value()) {
        HoverAt(*_cursor);
    }
    return placed;
}

void Bar::Draw() {
    Paint(PlaceModules());
}

void Bar::Paint(const PlacedBar& placed) {
    ID2D1DeviceContext* context = _canvas->surface.Context();
    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    const SectionStyle& section = _config.style.section;
    DrawBackdrops(_canvas->surface, _wallpapers, section.backdrop, SectionBoxes({&placed.layout.left, &placed.layout.center, &placed.layout.right}), section.box, WindowRect());
    DrawSection(placed.entries.left, placed.layout.left);
    DrawSection(placed.entries.center, placed.layout.center);
    DrawSection(placed.entries.right, placed.layout.right);
    const std::chrono::steady_clock::time_point presenting = std::chrono::steady_clock::now();
    _canvas->surface.EndDrawAndPresent();
    _presented = true;
    if (_animating) {
        const std::chrono::steady_clock::time_point presented = std::chrono::steady_clock::now();
        _animationFrames = WithFrame(_animationFrames, presented, presented - presenting, _canvas->surface.LastPresentCount(), _canvas->surface.LatestShown());
    }
}

void Bar::DrawSection(const std::vector<PlacedEntry>& entries, const SectionLayout& layout) {
    if (layout.length <= 0.0f) {
        return;
    }
    const BoxStyle& box = _config.style.section.box;
    DrawBox(_canvas->surface.Context(), _canvas->brushes, _canvas->boxes, SectionBounds(Geometry(), layout), box, box.background);

    for (const PlacedEntry& placed : entries) {
        if (placed.entry->group != nullptr) {
            DrawGroup(placed);
        } else {
            DrawModule(placed.box, placed.entry->style);
        }
    }
}

void Bar::DrawGroup(const PlacedEntry& placed) {
    ID2D1DeviceContext* context = _canvas->surface.Context();
    const ModuleStyle&  style   = placed.entry->style;
    DrawBox(context, _canvas->brushes, _canvas->boxes, placed.box.bounds, style.box, style.box.background);
    // The toggle's box at rest is the group's own, drawn once already with its border and shadows.
    const D2D1_COLOR_F toggleFill = _boxFades.at(placed.box.module).Color(_frameTime);
    if (toggleFill.a > 0.0f) {
        DrawBox(context, _canvas->brushes, _canvas->boxes, placed.toggleBounds, style.box, toggleFill);
    }
    placed.box.module->Draw(_contexts.at(&style), placed.box.contentOrigin);
    context->PushAxisAlignedClip(placed.shownBounds, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t member = 0; member < placed.members.size(); ++member) {
        DrawModule(placed.members[member], placed.entry->members[member].style);
    }
    context->PopAxisAlignedClip();
}

void Bar::DrawModule(const HitBox& box, const ModuleStyle& style) {
    DrawBox(_canvas->surface.Context(), _canvas->brushes, _canvas->boxes, box.bounds, style.box, _boxFades.at(box.module).Color(_frameTime));
    box.module->Draw(_contexts.at(&style), box.contentOrigin);
}

std::vector<D2D1_RECT_F> Bar::SectionBoxes(const std::vector<const SectionLayout*>& layouts) const {
    const BarGeometry        bar = Geometry();
    std::vector<D2D1_RECT_F> sections;
    for (const SectionLayout* layout : layouts) {
        if (layout->length > 0.0f) {
            sections.push_back(SectionBounds(bar, *layout));
        }
    }
    return sections;
}

RECT Bar::WindowRect() const {
    RECT windowRect{};
    if (!GetWindowRect(_window, &windowRect)) {
        ThrowLastError("GetWindowRect");
    }
    return windowRect;
}

const HitBox* Bar::BoxAt(POINT client) const {
    const D2D1_POINT_2F point = ClientDips(client, _placement.dpi);
    const auto          box   = std::ranges::find_if(_hitBoxes, [point](const HitBox& candidate) { return Contains(candidate.bounds, point); });
    if (box == _hitBoxes.end()) {
        return nullptr;
    }
    return &*box;
}

D2D1_POINT_2F Bar::ContentPoint(POINT client, const HitBox& box) const {
    const D2D1_POINT_2F point = ClientDips(client, _placement.dpi);
    return D2D1::Point2F(point.x - box.contentOrigin.x, point.y - box.contentOrigin.y);
}

void Bar::HandleInput(POINT client, ModuleInput input) {
    _tooltip->Hide();
    const bool                     menuWasShown = _menu->Shown();
    const std::optional<MenuOwner> menuOwner    = std::exchange(_menuOwner, std::nullopt);
    _menu->Hide();
    const HitBox* box = BoxAt(client);
    if (box == nullptr) {
        return;
    }
    const std::optional<ModuleAction>& action = ActionFor(box->module->Actions(), input);
    if (action.has_value()) {
        if (const auto* command = std::get_if<Command>(&*action)) {
            _launcher.Launch(*command);
        } else if (const auto* windowManagerCommand = std::get_if<WindowManagerCommand>(&*action)) {
            _windowManager.RunCommand(windowManagerCommand->text);
        } else if (const auto* menu = std::get_if<MenuAction>(&*action)) {
            if (!menuWasShown || !menuOwner.has_value() || menuOwner->module != box->module) {
                _menuOwner = MenuOwner{.module = box->module, .onSelect = menu->onSelect};
                _lister->List(menu->command, menu->timeout);
            }
        }
    } else if (input == ModuleInput::LeftClick) {
        box->module->Click(ContentPoint(client, *box));
        // A click can change what the module itself shows, such as the tray folding away its icons.
        Refresh();
    } else if (input == ModuleInput::RightClick) {
        box->module->RightClick(ContentPoint(client, *box));
        Refresh();
    } else if (input == ModuleInput::MiddleClick) {
        box->module->MiddleClick(ContentPoint(client, *box));
        Refresh();
    } else if (input == ModuleInput::ScrollUp || input == ModuleInput::ScrollDown) {
        box->module->Scroll(ScrollDirectionOf(input));
    }
}

void Bar::MoveCursor(POINT client) {
    _cursor = client;
    if (HoverAt(client)) {
        Redraw();
    }
}

bool Bar::HoverAt(POINT client) {
    const HitBox* box    = BoxAt(client);
    IModule*      module = ModuleOf(box);
    if (module == _hovered) {
        // Within one module the cursor can still move to another of its items, which can have its own tooltip.
        if (module != nullptr && module->Hover(ContentPoint(client, *box))) {
            RestartHover();
            return true;
        }
        return false;
    }
    if (_hovered != nullptr) {
        _hovered->Hover(std::nullopt);
    }
    _hovered = module;
    if (module != nullptr) {
        module->Hover(ContentPoint(client, *box));
    }
    FadeBoxesToHover();
    RestartHover();
    return true;
}

void Bar::FadeBoxesToHover() {
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    ForEachEntry(_modules, [this, now](const ModuleEntry& entry) {
        _boxFades.at(entry.module.get()).Retarget(BoxFill(entry, entry.module.get() == _hovered), entry.style.backgroundTransition, now);
    });
}

void Bar::RestartHover() {
    _tooltip->Hide();
    TRACKMOUSEEVENT track{
        .cbSize      = sizeof(TRACKMOUSEEVENT),
        .dwFlags     = TME_HOVER | TME_LEAVE,
        .hwndTrack   = _window,
        .dwHoverTime = HOVER_DEFAULT,
    };
    if (!TrackMouseEvent(&track)) {
        ThrowLastError("TrackMouseEvent");
    }
}

void Bar::ShowTooltip() {
    const auto box = std::ranges::find(_hitBoxes, _hovered, &HitBox::module);
    if (_hovered == nullptr || box == _hitBoxes.end()) {
        return;
    }
    const std::optional<std::wstring> text = _hovered->TooltipText();
    if (!text.has_value() || text->empty()) {
        return;
    }
    _tooltip->Show(*text, AnchorFor(TooltipSpan(_hovered->TooltipItemBox(), *box)));
}

TooltipAnchor Bar::AnchorFor(const D2D1_RECT_F& span) const {
    const RECT          barRect = WindowRect();
    const std::uint32_t dpi     = _placement.dpi;
    return TooltipAnchor{
        .target      = DipsToScreen(barRect, span, dpi),
        .barRect     = barRect,
        .monitorRect = _placement.monitorRect,
        .edge        = _config.bar.edge,
        .dpi         = dpi,
    };
}

void Bar::ShowMenu() {
    const std::optional<MenuListing> listing = _lister->Take();
    if (!listing.has_value() || !_menuOwner.has_value() || _deviceLost) {
        return;
    }
    // A group folding can take the module off the bar while its command runs.
    const auto box = std::ranges::find(_hitBoxes, _menuOwner->module, &HitBox::module);
    if (box == _hitBoxes.end()) {
        return;
    }
    if (const auto* failure = std::get_if<MenuFailure>(&*listing)) {
        Log(LogLevel::Warning, "bar", "menu_failed", {{.key = "reason", .value = failure->detail}});
    } else if (!HasEntries(std::get<std::vector<MenuItem>>(*listing))) {
        Log(LogLevel::Info, "bar", "menu_empty", {});
        return;
    }
    _menu->Show(*listing, _menuOwner->onSelect, AnchorFor(box->bounds));
}

#include "bar/Bar.hpp"

#include "bar/Box.hpp"
#include "bar/Layout.hpp"
#include "modules/ModuleFactory.hpp"
#include "platform/AppBar.hpp"
#include "platform/Errors.hpp"
#include "platform/Log.hpp"
#include "platform/Text.hpp"
#include "platform/Win32Window.hpp"
#include "platform/WindowProc.hpp"
#include "render/Axis.hpp"
#include "tray/ShellNotifyIcon.hpp"

#include <shellapi.h>
#include <windowsx.h>

#include <cstdint>
#include <algorithm>
#include <chrono>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

    constexpr wchar_t       kWindowClass[]    = L"IntarsiaBar";
    constexpr std::uint32_t kAppBarCallback   = WM_APP + 1;
    constexpr std::uint32_t kAnimationFrame   = WM_APP + 2;
    constexpr std::uint32_t kFrameClockFailed = WM_APP + 3;
    constexpr std::uint32_t kMenuListed       = WM_APP + 4;
    constexpr UINT_PTR      kChangeTimer      = 1;

    // Where a module's content starts in its box: past the padding along the bar, centered across it.
    D2D1_POINT_2F ContentOrigin(Axis axis, const D2D1_RECT_F& box, float padding, D2D1_SIZE_F content) {
        if (axis == Axis::Horizontal) {
            return D2D1::Point2F(box.left + padding, box.top + ((box.bottom - box.top - content.height) / 2.0f));
        }
        return D2D1::Point2F(box.left + ((box.right - box.left - content.width) / 2.0f), box.top + padding);
    }

    // Every entry of every section, each group's members right after it.
    template <typename Visit>
    void ForEachEntry(const Sections<ModuleEntry>& modules, Visit visit) {
        ForEachInSections(modules, [&visit](const ModuleEntry& entry) {
            visit(entry);
            for (const ModuleEntry& member : entry.members) {
                visit(member);
            }
        });
    }

    D2D1_RECT_F Intersection(const D2D1_RECT_F& first, const D2D1_RECT_F& second) {
        return D2D1::RectF(std::max(first.left, second.left), std::max(first.top, second.top), std::min(first.right, second.right), std::min(first.bottom, second.bottom));
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

    std::string MillisecondsText(std::chrono::steady_clock::duration duration) {
        return std::format("{:.1f}", std::chrono::duration<double, std::milli>(duration).count());
    }

    // The first frame is the one the animation started on, so the intervals between frames number one fewer.
    void LogAnimationFrames(const FrameStats& stats, const RECT& monitorRect) {
        const std::chrono::steady_clock::duration length = stats.last - stats.first;
        const std::chrono::steady_clock::duration mean   = stats.frames > 1 ? length / (stats.frames - 1) : std::chrono::steady_clock::duration::zero();
        Log(LogLevel::Info, "bar", "animation_frames",
            {{.key = "monitor", .value = RectText(monitorRect)},
             {.key = "frames", .value = std::to_string(stats.frames)},
             {.key = "length_ms", .value = MillisecondsText(length)},
             {.key = "mean_interval_ms", .value = MillisecondsText(mean)},
             {.key = "longest_interval_ms", .value = MillisecondsText(stats.longestInterval)},
             {.key = "longest_present_ms", .value = MillisecondsText(stats.longestPresent)}});
    }

    // The action an input runs, or nullopt when the config binds none to it.
    const std::optional<ModuleAction>& ActionFor(const ModuleActions& actions, ModuleInput input) {
        switch (input) {
            case ModuleInput::LeftClick: return actions.click;
            case ModuleInput::RightClick: return actions.rightClick;
            case ModuleInput::MiddleClick: return actions.middleClick;
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

    // The whole monitor's length along the bar's edge, which picks the config's layout.
    float EdgeLengthDips(const BarPlacement& placement, Edge edge) {
        const RECT& rect   = placement.monitorRect;
        const float pixels = static_cast<float>(AxisOf(edge) == Axis::Horizontal ? rect.right - rect.left : rect.bottom - rect.top);
        return pixels * static_cast<float>(USER_DEFAULT_SCREEN_DPI) / static_cast<float>(placement.dpi);
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

    // A shared bar stays a registered appbar, so the shell still tells it of fullscreen apps and of other appbars moving.
    RECT BarRect(HWND window, const RECT& monitorRect, const BarConfig& bar, std::int32_t thickness, AppBarRoute route) {
        switch (bar.space) {
            case Space::Reserve: return ReserveRect(window, monitorRect, bar.edge, thickness, route);
            case Space::Share: return DockedRect(monitorRect, bar.edge, thickness);
        }
        throw std::invalid_argument(std::format("unknown space {}", static_cast<std::int32_t>(bar.space)));
    }

    HWND CreateBarWindow(HINSTANCE instance, void* owner) {
        // No activation so clicking the bar never steals focus from the tiled window.
        return CreatePopupWindow(instance, kWindowClass, L"intarsia", WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_NOREDIRECTIONBITMAP, owner, "bar");
    }

} // namespace

bool BarPlacement::operator==(const BarPlacement& other) const {
    return monitor == other.monitor && EqualRect(&monitorRect, &other.monitorRect) && dpi == other.dpi;
}

void Bar::RegisterWindowClass(HINSTANCE instance) {
    ::RegisterWindowClass(instance, kWindowClass, OwnedWindowProc<Bar>, LoadCursorW(nullptr, IDC_ARROW), "bar");
}

Bar::Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, const BarPlacement& placement,
         AppBarRoute route, AppState& state, HWND control, std::uint32_t deviceLostMessage) :
    _graphics(graphics), _config(config), _launcher(launcher), _windowManager(sources.windowManager), _state(state), _control(control), _deviceLostMessage(deviceLostMessage),
    _placement(placement), _route(route), _modules(CreateModules(ModulesForEdgeLength(config, EdgeLengthDips(placement, config.bar.edge)), sources)),
    _taskbarCreated(TaskbarCreatedMessage()), _window(CreateBarWindow(instance, this)) {
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
    _surface = std::make_unique<Surface>(_graphics, _window, static_cast<std::uint32_t>(client.right), static_cast<std::uint32_t>(client.bottom), _placement.dpi);
    _brushes = std::make_unique<BrushCache>(_surface->Context());
    _tooltip = std::make_unique<Tooltip>(instance, _graphics, _config, _textFormat.Get());
    _menu =
        std::make_unique<Menu>(instance, _graphics, _config, _textFormat.Get(), _launcher, _state, *_tooltip, [this](const DeviceLostError& error) { ReportDeviceLost(error); });
    _lister = std::make_unique<MenuLister>(_window, kMenuListed);
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
        case kAnimationFrame:
            Refresh();
            // Only after drawing: GetMessage takes posted messages before input, so a frame posted while the bar drew
            // would starve clicks until the animation ended.
            _frames->Acknowledge();
            if (!_animating || _deviceLost) {
                _frames->Stop();
            }
            return 0;
        case kFrameClockFailed: _frames->RethrowFailure(); return 0;
        case kMenuListed: ShowMenu(); return 0;
        case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
        case WM_MOUSEMOVE: PointAt(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)}); return 0;
        case WM_MOUSEHOVER: ShowTooltip(); return 0;
        case WM_MOUSELEAVE:
            if (_pointed != nullptr) {
                _pointed->Hover(std::nullopt);
            }
            _pointed = nullptr;
            _cursor  = std::nullopt;
            _tooltip->Hide();
            // Draws the box that was under the cursor without its hover.
            Redraw();
            return 0;
        case WM_LBUTTONUP: HandleInput(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)}, ModuleInput::LeftClick); return 0;
        case WM_RBUTTONUP: HandleInput(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)}, ModuleInput::RightClick); return 0;
        case WM_MBUTTONUP: HandleInput(POINT{.x = GET_X_LPARAM(lParam), .y = GET_Y_LPARAM(lParam)}, ModuleInput::MiddleClick); return 0;
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
    const RECT         rect      = BarRect(_window, _placement.monitorRect, _config.bar, thickness, _route);
    if (!MoveWindow(_window, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, TRUE)) {
        ThrowLastError("MoveWindow");
    }
}

void Bar::PlaceInZOrder(HWND insertAfter) {
    if (!SetWindowPos(_window, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE)) {
        ThrowLastError("SetWindowPos(z-order)");
    }
}

RenderContext Bar::ModuleContext(const ModuleStyle& style) const {
    std::vector<StateBrushes> classes;
    classes.reserve(style.classes.size());
    for (const ClassColors& declared : style.classes) {
        classes.push_back(StateBrushes{.text = _brushes->Get(declared.colors.text), .icon = _brushes->Get(declared.colors.icon)});
    }
    return RenderContext{.context      = _surface->Context(),
                         .dwrite       = _graphics.dwrite.Get(),
                         .textFormat   = _textFormat.Get(),
                         .iconFont     = style.icon.font.c_str(),
                         .iconSizeDips = style.icon.sizeDips,
                         .iconGapDips  = style.icon.gapDips,
                         .foreground   = _brushes->Get(style.color),
                         .accent       = _brushes->Get(style.icon.color),
                         .alert        = _brushes->Get(style.alertColor),
                         .warning      = {.text = _brushes->Get(style.warning.text), .icon = _brushes->Get(style.warning.icon)},
                         .critical     = {.text = _brushes->Get(style.critical.text), .icon = _brushes->Get(style.critical.icon)},
                         .muted        = {.text = _brushes->Get(style.muted.text), .icon = _brushes->Get(style.muted.icon)},
                         .classes      = std::move(classes),
                         .brushes      = _brushes.get(),
                         .axis         = AxisOf(_config.bar.edge),
                         .boxCrossDips = BoxCrossLength()};
}

// A module box spans the bar's thickness less the sections' margin and padding on both sides.
float Bar::BoxCrossLength() const {
    const Axis          axis      = AxisOf(_config.bar.edge);
    const SectionStyle& section   = _config.style.section;
    const float         thickness = CrossLength(_surface->Context()->GetSize(), axis);
    return thickness - (2.0f * CrossOf(section.margin, axis)) - (2.0f * CrossOf(section.box.padding, axis));
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

void Bar::Redraw() {
    if (_surface == nullptr || _deviceLost) {
        return;
    }
    try {
        Draw();
    } catch (const DeviceLostError& error) { ReportDeviceLost(error); }
}

bool Bar::LayOutModules() {
    bool changed = false;
    // Every module lays out, so none is left holding stale content.
    ForEachEntry(_modules, [this, &changed](const ModuleEntry& entry) { changed = entry.module->Layout(ModuleContext(entry.style)) || changed; });
    return changed;
}

// Each module's box along the bar: what it draws plus its padding on both sides. A module showing nothing, such as a
// battery module with no battery, takes no room at all.
std::vector<float> Bar::BoxLengths(const std::vector<ModuleEntry>& entries) const {
    std::vector<float> lengths;
    lengths.reserve(entries.size());
    for (const ModuleEntry& entry : entries) {
        if (entry.group != nullptr) {
            lengths.push_back(ArrangeMembers(entry, BoxLengths(entry.members)).length);
            continue;
        }
        const float content = MainLength(entry.module->Size(), AxisOf(_config.bar.edge));
        lengths.push_back(content > 0.0f ? content + (2.0f * MainOf(entry.style.box.padding, AxisOf(_config.bar.edge))) : 0.0f);
    }
    return lengths;
}

GroupLayout Bar::ArrangeMembers(const ModuleEntry& entry, const std::vector<float>& memberLengths) const {
    const Axis axis = AxisOf(_config.bar.edge);
    return ArrangeGroup(MainLength(entry.module->Size(), axis), memberLengths, entry.group->Revealed(), MainOf(entry.style.box.padding, axis));
}

void Bar::Draw() {
    ID2D1DeviceContext*         context = _surface->Context();
    const Sections<float>       lengths{.left = BoxLengths(_modules.left), .center = BoxLengths(_modules.center), .right = BoxLengths(_modules.right)};
    const Axis                  axis    = AxisOf(_config.bar.edge);
    const SectionStyle&         section = _config.style.section;
    const BarLayout             layout  = ArrangeBar(lengths, MainLength(context->GetSize(), axis), MainOf(section.margin, axis), MainOf(section.box.padding, axis));
    const Sections<PlacedEntry> placed{.left   = PlaceSection(_modules.left, lengths.left, layout.left),
                                       .center = PlaceSection(_modules.center, lengths.center, layout.center),
                                       .right  = PlaceSection(_modules.right, lengths.right, layout.right)};
    RECT                        window{};
    if (!GetWindowRect(_window, &window)) {
        ThrowLastError("GetWindowRect");
    }
    _hitBoxes.clear();
    ForEachInSections(placed, [this](const PlacedEntry& entry) {
        // Only what the fold uncovers of a member takes the cursor.
        for (const HitBox& member : entry.members) {
            const D2D1_RECT_F shown = Intersection(member.bounds, entry.shown);
            if (shown.right > shown.left && shown.bottom > shown.top) {
                _hitBoxes.push_back(HitBox{.module = member.module, .bounds = shown, .contentOrigin = member.contentOrigin});
            }
        }
        _hitBoxes.push_back(entry.box);
    });
    for (const HitBox& box : _hitBoxes) {
        box.module->Place(ModulePlacement{.window = window, .contentOrigin = box.contentOrigin, .dpi = _placement.dpi});
    }
    // Items move under a cursor that stays still, as a tray unfolds, and no WM_MOUSEMOVE says so.
    if (_cursor.has_value()) {
        Point(*_cursor);
    }

    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawSection(placed.left, layout.left);
    DrawSection(placed.center, layout.center);
    DrawSection(placed.right, layout.right);
    const std::chrono::steady_clock::time_point presenting = std::chrono::steady_clock::now();
    _surface->EndDrawAndPresent();
    _presented = true;
    if (_animating) {
        const std::chrono::steady_clock::time_point presented = std::chrono::steady_clock::now();
        _animationFrames                                      = WithFrame(_animationFrames, presented, presented - presenting);
    }
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

// The entry's box from start along the bar, spanning across it as every module's box does.
Bar::HitBox Bar::PlaceBox(const ModuleEntry& entry, float start, float length) const {
    const Axis          axis     = AxisOf(_config.bar.edge);
    const SectionStyle& style    = _config.style.section;
    const float         boxStart = CrossOf(style.margin, axis) + CrossOf(style.box.padding, axis);
    const D2D1_RECT_F   box      = MainCrossRect(axis, start, length, boxStart, BoxCrossLength());
    return HitBox{.module = entry.module.get(), .bounds = box, .contentOrigin = ContentOrigin(axis, box, MainOf(entry.style.box.padding, axis), entry.module->Size())};
}

std::vector<Bar::PlacedEntry> Bar::PlaceSection(const std::vector<ModuleEntry>& entries, const std::vector<float>& lengths, const SectionLayout& layout) const {
    std::vector<PlacedEntry> placed;
    if (layout.length <= 0.0f) {
        return placed;
    }
    placed.reserve(entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        const ModuleEntry& entry = entries[index];
        const float        start = layout.offsets[index];
        PlacedEntry        one{.entry = &entry, .box = PlaceBox(entry, start, lengths[index]), .toggle = {}, .members = {}, .shown = {}};
        if (entry.group != nullptr) {
            const std::vector<float> memberLengths = BoxLengths(entry.members);
            const GroupLayout        group         = ArrangeMembers(entry, memberLengths);
            one.toggle                             = PlaceBox(entry, start, group.toggleLength).bounds;
            one.shown                              = PlaceBox(entry, start + group.shownStart, group.shownLength).bounds;
            for (size_t member = 0; member < entry.members.size(); ++member) {
                one.members.push_back(PlaceBox(entry.members[member], start + group.offsets[member], memberLengths[member]));
            }
        }
        placed.push_back(std::move(one));
    }
    return placed;
}

void Bar::DrawModule(const HitBox& box, const ModuleStyle& style) {
    DrawBox(_surface->Context(), *_brushes, box.bounds, style.box, box.module == _pointed ? style.hover : style.box.background);
    box.module->Draw(ModuleContext(style), box.contentOrigin);
}

void Bar::DrawGroup(const PlacedEntry& placed) {
    ID2D1DeviceContext* context = _surface->Context();
    const ModuleStyle&  style   = placed.entry->style;
    DrawBox(context, *_brushes, placed.box.bounds, style.box, style.box.background);
    if (placed.box.module == _pointed) {
        DrawBox(context, *_brushes, placed.toggle, style.box, style.hover);
    }
    placed.box.module->Draw(ModuleContext(style), placed.box.contentOrigin);
    context->PushAxisAlignedClip(placed.shown, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (size_t member = 0; member < placed.members.size(); ++member) {
        DrawModule(placed.members[member], placed.entry->members[member].style);
    }
    context->PopAxisAlignedClip();
}

// The section's box, then each module.
void Bar::DrawSection(const std::vector<PlacedEntry>& entries, const SectionLayout& layout) {
    if (layout.length <= 0.0f) {
        return;
    }
    ID2D1DeviceContext* context   = _surface->Context();
    const Axis          axis      = AxisOf(_config.bar.edge);
    const SectionStyle& style     = _config.style.section;
    const float         margin    = CrossOf(style.margin, axis);
    const float         thickness = CrossLength(context->GetSize(), axis);
    const D2D1_RECT_F   section   = MainCrossRect(axis, layout.start, layout.length, margin, thickness - (2.0f * margin));
    DrawBox(context, *_brushes, section, style.box, style.box.background);

    for (const PlacedEntry& placed : entries) {
        if (placed.entry->group != nullptr) {
            DrawGroup(placed);
        } else {
            DrawModule(placed.box, placed.entry->style);
        }
    }
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
    const bool                     menuWasShown = _menu->Shown();
    const std::optional<MenuOwner> menuOwner    = std::exchange(_menuOwner, std::nullopt);
    _menu->Hide();
    const HitBox* box = BoxAt(client);
    if (box == nullptr) {
        return;
    }
    const std::optional<ModuleAction>& action = ActionFor(box->module->Actions(), input);
    if (const auto* command = action.has_value() ? std::get_if<Command>(&*action) : nullptr) {
        _launcher.Launch(*command);
    } else if (const auto* windowManagerCommand = action.has_value() ? std::get_if<WindowManagerCommand>(&*action) : nullptr) {
        _windowManager.RunCommand(windowManagerCommand->text);
    } else if (const auto* menu = action.has_value() ? std::get_if<MenuAction>(&*action) : nullptr) {
        if (!menuWasShown || !menuOwner.has_value() || menuOwner->module != box->module) {
            _menuOwner = MenuOwner{.module = box->module, .onSelect = menu->onSelect};
            _lister->List(menu->command, menu->timeout);
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
        box->module->Scroll(input == ModuleInput::ScrollUp ? ScrollDirection::Up : ScrollDirection::Down);
    }
}

void Bar::ScheduleNextChange() {
    const std::optional<std::uint32_t> wait = EarliestChange(_modules);
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

D2D1_POINT_2F Bar::ContentPoint(POINT client, const HitBox& box) const {
    const float scale = USER_DEFAULT_SCREEN_DPI / static_cast<float>(_placement.dpi);
    return D2D1::Point2F((static_cast<float>(client.x) * scale) - box.contentOrigin.x, (static_cast<float>(client.y) * scale) - box.contentOrigin.y);
}

bool Bar::Point(POINT client) {
    const HitBox* box    = BoxAt(client);
    IModule*      module = box == nullptr ? nullptr : box->module;
    if (module == _pointed) {
        // Within one module the cursor can still move to another of its items, which can have its own tooltip.
        if (module != nullptr && module->Hover(ContentPoint(client, *box))) {
            RestartHover();
            return true;
        }
        return false;
    }
    if (_pointed != nullptr) {
        _pointed->Hover(std::nullopt);
    }
    _pointed = module;
    if (module != nullptr) {
        module->Hover(ContentPoint(client, *box));
    }
    RestartHover();
    return true;
}

void Bar::PointAt(POINT client) {
    _cursor = client;
    if (Point(client)) {
        Redraw();
    }
}

void Bar::RestartHover() {
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
    // Against the item the text describes, such as one tray icon, or else the whole module.
    const std::optional<D2D1_RECT_F> item   = _pointed->TooltipItem();
    const D2D1_POINT_2F              origin = box->contentOrigin;
    const D2D1_RECT_F span = item.has_value() ? D2D1::RectF(origin.x + item->left, origin.y + item->top, origin.x + item->right, origin.y + item->bottom) : box->bounds;
    _tooltip->Show(*text, AnchorFor(span));
}

TooltipAnchor Bar::AnchorFor(const D2D1_RECT_F& span) const {
    RECT bar{};
    if (!GetWindowRect(_window, &bar)) {
        ThrowLastError("GetWindowRect");
    }
    const std::uint32_t dpi = _placement.dpi;
    return TooltipAnchor{.module = DipsToScreen(bar, span, dpi), .bar = bar, .monitor = _placement.monitorRect, .edge = _config.bar.edge, .dpi = dpi};
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

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

    float CrossLength(D2D1_SIZE_F size, Edge edge) {
        return IsHorizontal(edge) ? size.height : size.width;
    }

    // Spacing is on screen axes, as CSS writes it: along a top or bottom bar is x, along a left or right bar is y.
    float MainOf(const Spacing& spacing, Edge edge) {
        return IsHorizontal(edge) ? spacing.xDips : spacing.yDips;
    }

    float CrossOf(const Spacing& spacing, Edge edge) {
        return IsHorizontal(edge) ? spacing.yDips : spacing.xDips;
    }

    // A rectangle from a span along the bar (main) and a span across it (cross).
    D2D1_RECT_F MainCrossRect(Edge edge, float mainStart, float mainLength, float crossStart, float crossLength) {
        if (IsHorizontal(edge)) {
            return D2D1::RectF(mainStart, crossStart, mainStart + mainLength, crossStart + crossLength);
        }
        return D2D1::RectF(crossStart, mainStart, crossStart + crossLength, mainStart + mainLength);
    }

    // Where a module's content starts in its box: past the padding along the bar, centered across it.
    D2D1_POINT_2F ContentOrigin(Edge edge, const D2D1_RECT_F& box, float padding, D2D1_SIZE_F content) {
        if (IsHorizontal(edge)) {
            return D2D1::Point2F(box.left + padding, box.top + ((box.bottom - box.top - content.height) / 2.0f));
        }
        return D2D1::Point2F(box.left + ((box.right - box.left - content.width) / 2.0f), box.top + padding);
    }

    // A border is stroked centered on its outline, so the outline is inset by half the width to keep it inside.
    void DrawBox(ID2D1DeviceContext* context, BrushCache& brushes, const D2D1_RECT_F& box, const BoxStyle& style, const D2D1_COLOR_F& fill) {
        const float radius = std::min(style.radiusDips, std::min(box.right - box.left, box.bottom - box.top) / 2.0f);
        if (fill.a > 0.0f) {
            context->FillRoundedRectangle(D2D1::RoundedRect(box, radius, radius), brushes.Get(fill));
        }
        const float width = style.borderWidthDips;
        if (width > 0.0f && style.borderColor.a > 0.0f) {
            const float       inset = width / 2.0f;
            const D2D1_RECT_F edge  = D2D1::RectF(box.left + inset, box.top + inset, box.right - inset, box.bottom - inset);
            const float       round = std::max(radius - inset, 0.0f);
            context->DrawRoundedRectangle(D2D1::RoundedRect(edge, round, round), brushes.Get(style.borderColor), width);
        }
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

Bar::Bar(HINSTANCE instance, const GraphicsDevice& graphics, const Config& config, const ModuleSources& sources, Launcher& launcher, FrameClock& frames,
         const BarPlacement& placement, AppState& state, HWND control, std::uint32_t deviceLostMessage) :
    _graphics(graphics), _config(config), _launcher(launcher), _frames(frames), _windowManager(sources.windowManager), _state(state), _control(control),
    _deviceLostMessage(deviceLostMessage), _placement(placement), _modules(CreateModules(config, sources)),
    _styles(MapSections(config.modules, [](const ModuleInstance& instance) { return instance.style; })), _window(CreateBarWindow(instance, this)) {
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
    RequireFontFamily(_graphics.dwrite.Get(), _config.style.font.c_str());
    ForEachInSections(_styles, [this](const ModuleStyle& style) { RequireFontFamily(_graphics.dwrite.Get(), style.icon.font.c_str()); });
    ThrowIfFailed(_graphics.dwrite->CreateTextFormat(_config.style.font.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                     _config.style.fontSizeDips, L"en-us", &_textFormat),
                  "IDWriteFactory::CreateTextFormat");

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
            if (_pointed != nullptr) {
                _pointed->Hover(std::nullopt);
            }
            _pointed = nullptr;
            _cursor  = std::nullopt;
            _tooltip->Hide();
            // Draws the box that was under the cursor without its hover.
            Render();
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

RenderContext Bar::ModuleContext(const ModuleStyle& style) const {
    return RenderContext{.context       = _surface->Context(),
                         .dwrite        = _graphics.dwrite.Get(),
                         .textFormat    = _textFormat.Get(),
                         .iconFont      = style.icon.font.c_str(),
                         .iconSizeDips  = style.icon.sizeDips,
                         .iconGapDips   = style.icon.gapDips,
                         .foreground    = _brushes->Get(style.color),
                         .accent        = _brushes->Get(style.icon.color),
                         .alert         = _brushes->Get(style.alertColor),
                         .brushes       = _brushes.get(),
                         .boxHeightDips = BoxCrossLength()};
}

// A module box spans the bar's thickness less the sections' margin and padding on both sides.
float Bar::BoxCrossLength() const {
    const Edge          edge      = _config.bar.edge;
    const SectionStyle& section   = _config.style.section;
    const float         thickness = CrossLength(_surface->Context()->GetSize(), edge);
    return thickness - (2.0f * CrossOf(section.margin, edge)) - (2.0f * CrossOf(section.box.padding, edge));
}

bool Bar::DeviceLost() const {
    return _deviceLost;
}

bool Bar::HasPresented() const {
    return _presented;
}

bool Bar::Animating() const {
    return _animating && !_deviceLost;
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
    bool changed = false;
    // Every module lays out, so none is left holding stale content.
    for (const auto& [modules, styles] : {std::pair{&_modules.left, &_styles.left}, std::pair{&_modules.center, &_styles.center}, std::pair{&_modules.right, &_styles.right}}) {
        for (size_t index = 0; index < modules->size(); ++index) {
            changed = (*modules)[index]->Layout(ModuleContext((*styles)[index])) || changed;
        }
    }
    return changed;
}

// Each module's box along the bar: what it draws plus its padding on both sides.
std::vector<float> Bar::BoxLengths(const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<ModuleStyle>& styles) const {
    std::vector<float> lengths;
    lengths.reserve(modules.size());
    for (size_t index = 0; index < modules.size(); ++index) {
        lengths.push_back(MainLength(modules[index]->Size(), _config.bar.edge) + (2.0f * MainOf(styles[index].box.padding, _config.bar.edge)));
    }
    return lengths;
}

void Bar::Draw() {
    ID2D1DeviceContext*   context = _surface->Context();
    const Sections<float> lengths{
        .left = BoxLengths(_modules.left, _styles.left), .center = BoxLengths(_modules.center, _styles.center), .right = BoxLengths(_modules.right, _styles.right)};
    const Edge             edge    = _config.bar.edge;
    const SectionStyle&    section = _config.style.section;
    const BarLayout        layout  = ArrangeBar(lengths, MainLength(context->GetSize(), edge), MainOf(section.margin, edge), MainOf(section.box.padding, edge));
    const Sections<HitBox> boxes{.left   = PlaceSection(_modules.left, _styles.left, lengths.left, layout.left),
                                 .center = PlaceSection(_modules.center, _styles.center, lengths.center, layout.center),
                                 .right  = PlaceSection(_modules.right, _styles.right, lengths.right, layout.right)};
    _hitBoxes.clear();
    ForEachInSections(boxes, [this](const HitBox& box) { _hitBoxes.push_back(box); });
    // Items move under a cursor that stays still, as a tray unfolds, and no WM_MOUSEMOVE says so.
    if (_cursor.has_value()) {
        Point(*_cursor);
    }

    context->BeginDraw();
    context->Clear(D2D1::ColorF(0, 0, 0, 0));
    DrawSection(boxes.left, _styles.left, layout.left);
    DrawSection(boxes.center, _styles.center, layout.center);
    DrawSection(boxes.right, _styles.right, layout.right);
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

std::vector<Bar::HitBox> Bar::PlaceSection(const std::vector<std::unique_ptr<IModule>>& modules, const std::vector<ModuleStyle>& styles, const std::vector<float>& lengths,
                                           const SectionLayout& layout) const {
    std::vector<HitBox> boxes;
    if (layout.length <= 0.0f) {
        return boxes;
    }
    const Edge          edge     = _config.bar.edge;
    const SectionStyle& style    = _config.style.section;
    const float         boxStart = CrossOf(style.margin, edge) + CrossOf(style.box.padding, edge);
    boxes.reserve(modules.size());
    for (size_t index = 0; index < modules.size(); ++index) {
        const D2D1_RECT_F box = MainCrossRect(edge, layout.offsets[index], lengths[index], boxStart, BoxCrossLength());
        boxes.push_back(
            HitBox{.module = modules[index].get(), .bounds = box, .contentOrigin = ContentOrigin(edge, box, MainOf(styles[index].box.padding, edge), modules[index]->Size())});
    }
    return boxes;
}

// The section's box, then each module's box (its background, and its hover while under the cursor), then the module.
void Bar::DrawSection(const std::vector<HitBox>& boxes, const std::vector<ModuleStyle>& styles, const SectionLayout& layout) {
    if (layout.length <= 0.0f) {
        return;
    }
    ID2D1DeviceContext* context   = _surface->Context();
    const Edge          edge      = _config.bar.edge;
    const SectionStyle& style     = _config.style.section;
    const float         margin    = CrossOf(style.margin, edge);
    const float         thickness = CrossLength(context->GetSize(), edge);
    const D2D1_RECT_F   section   = MainCrossRect(edge, layout.start, layout.length, margin, thickness - (2.0f * margin));
    DrawBox(context, *_brushes, section, style.box, style.box.background);

    for (size_t index = 0; index < boxes.size(); ++index) {
        const ModuleStyle& moduleStyle = styles[index];
        DrawBox(context, *_brushes, boxes[index].bounds, moduleStyle.box, boxes[index].module == _pointed ? moduleStyle.hover : moduleStyle.box.background);
        boxes[index].module->Draw(ModuleContext(moduleStyle), boxes[index].contentOrigin);
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
        box->module->Click(ContentPoint(client, *box));
        // A click can change what the module itself shows, such as the tray folding away its icons.
        Refresh();
    } else if (input == ModuleInput::RightClick) {
        box->module->RightClick(ContentPoint(client, *box));
        Refresh();
    } else if (input == ModuleInput::ScrollUp || input == ModuleInput::ScrollDown) {
        box->module->Scroll(input == ModuleInput::ScrollUp ? ScrollDirection::Up : ScrollDirection::Down);
    }
}

void Bar::ScheduleNextChange() {
    const std::optional<std::uint32_t> wait = EarliestChange(_modules);
    _animating                              = wait == 0u;
    if (!wait.has_value() || _animating) {
        KillTimer(_window, kChangeTimer);
        // A timer ticks at the system's 15.6 ms, off the display's refresh, so an animation follows the frame clock.
        if (_animating) {
            _frames.Start();
        }
        return;
    }
    if (SetTimer(_window, kChangeTimer, *wait, nullptr) == 0) {
        ThrowLastError("SetTimer");
    }
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
        Render();
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
    RECT bar{};
    if (!GetWindowRect(_window, &bar)) {
        ThrowLastError("GetWindowRect");
    }
    // Against the item the text describes, such as one tray icon, or else the whole module.
    const std::optional<D2D1_RECT_F> item   = _pointed->TooltipItem();
    const D2D1_POINT_2F              origin = box->contentOrigin;
    const D2D1_RECT_F   anchor = item.has_value() ? D2D1::RectF(origin.x + item->left, origin.y + item->top, origin.x + item->right, origin.y + item->bottom) : box->bounds;
    const std::uint32_t dpi    = _placement.dpi;
    const RECT          module{.left   = bar.left + DipsToPixels(anchor.left, dpi),
                               .top    = bar.top + DipsToPixels(anchor.top, dpi),
                               .right  = bar.left + DipsToPixels(anchor.right, dpi),
                               .bottom = bar.top + DipsToPixels(anchor.bottom, dpi)};
    _tooltip->Show(*text, TooltipAnchor{.module = module, .bar = bar, .monitor = _placement.monitorRect, .edge = _config.bar.edge, .dpi = dpi});
}

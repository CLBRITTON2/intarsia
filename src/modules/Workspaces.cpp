#include "modules/Workspaces.hpp"

#include "platform/Text.hpp"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <utility>

Workspaces::Workspaces(IWindowManager& windowManager, HMONITOR monitor, ModuleStyle style) :
    _windowManager(windowManager), _monitor(reinterpret_cast<std::intptr_t>(monitor)), _style(std::move(style)), _row{.boxes = {}, .size = {.width = 0.0f, .height = 0.0f}},
    _disconnected(EmptyIconText()) {}

bool Workspaces::Layout(const RenderContext& render) {
    const WindowManagerStatus status           = _windowManager.Status();
    std::wstring              disconnectedText = status.connection == Connection::Disconnected ? status.name + L" disconnected" : std::wstring();
    std::vector<Workspace>    shown            = disconnectedText.empty() ? WorkspacesOnMonitor(_windowManager.Workspaces(), _monitor) : std::vector<Workspace>();
    if (_laidOut && shown == _shown && disconnectedText == _disconnectedText) {
        return false;
    }
    _shown            = std::move(shown);
    _disconnectedText = std::move(disconnectedText);
    _laidOut          = true;
    _labels.clear();
    _states.clear();
    _disconnected = _disconnectedText.empty() ? EmptyIconText() : LayOutIconText(render, L"", _disconnectedText);

    std::vector<D2D1_SIZE_F>      contents;
    std::vector<const ItemStyle*> styles;
    for (const Workspace& workspace : _shown) {
        const ItemStyle& state = WorkspaceState(workspace, _style);
        _labels.push_back(LayOutWeightedText(render, workspace.label, state.weight));
        _states.push_back(state);
        contents.push_back(_labels.back().size);
        styles.push_back(&state);
    }
    _row = ArrangeItems(contents, styles);
    return true;
}

D2D1_SIZE_F Workspaces::Size() const {
    return _disconnectedText.empty() ? _row.size : _disconnected.size;
}

void Workspaces::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    if (!_disconnectedText.empty()) {
        DrawIconText(render, origin, _disconnected, render.alert);
        return;
    }
    for (size_t index = 0; index < _labels.size(); ++index) {
        const ItemStyle& state   = _states[index];
        const bool       hovered = _hovered == index;
        DrawItemBackground(render, _row.boxes[index], state, hovered ? state.hoverBackground : state.background, origin);
        DrawIconText(render, ItemContentOrigin(_row.boxes[index], state, origin), _labels[index], render.brushes->Get(hovered ? state.hoverColor : state.color));
    }
}

std::optional<std::uint32_t> Workspaces::MillisecondsUntilChange() const {
    return std::nullopt;
}

bool Workspaces::Hover(std::optional<D2D1_POINT_2F> point) {
    const std::optional<size_t> hovered = point.has_value() ? ItemAt(_row, *point) : std::nullopt;
    const bool                  changed = hovered != _hovered;
    _hovered                            = hovered;
    return changed;
}

void                       Workspaces::Scroll(ScrollDirection) {}

void                       Workspaces::RightClick(D2D1_POINT_2F) {}

std::optional<D2D1_RECT_F> Workspaces::TooltipItem() const {
    return std::nullopt;
}

void Workspaces::Click(D2D1_POINT_2F point) {
    const std::optional<size_t> item = ItemAt(_row, point);
    if (item.has_value()) {
        _windowManager.FocusWorkspace(_shown[*item].name);
    }
}

const ModuleActions& Workspaces::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> Workspaces::TooltipText() {
    const WindowManagerStatus status = _windowManager.Status();
    if (status.connection != Connection::Disconnected) {
        return std::nullopt;
    }
    return ToWide(status.detail);
}

std::vector<Workspace> WorkspacesOnMonitor(const std::vector<Workspace>& workspaces, std::intptr_t monitor) {
    std::vector<Workspace> shown;
    std::ranges::copy_if(workspaces, std::back_inserter(shown), [monitor](const Workspace& workspace) { return workspace.monitor == monitor; });
    return shown;
}

const ItemStyle& WorkspaceState(const Workspace& workspace, const ModuleStyle& style) {
    if (workspace.focused) {
        return style.focused;
    }
    return workspace.occupied ? style.occupied : style.empty;
}

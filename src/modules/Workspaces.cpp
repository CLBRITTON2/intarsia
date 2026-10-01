#include "modules/Workspaces.hpp"

#include "platform/Text.hpp"

#include <chrono>
#include <cstdint>
#include <utility>

Workspaces::Workspaces(IWindowManager& windowManager, HMONITOR monitor, ModuleStyle style) :
    _windowManager(windowManager), _monitor(reinterpret_cast<std::intptr_t>(monitor)), _style(std::move(style)), _row(EmptyItemRow()), _disconnected(EmptyIconText()) {}

bool Workspaces::Layout(const RenderContext& render) {
    const bool changed = LayOutItems(render);
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return changed;
}

bool Workspaces::LayOutItems(const RenderContext& render) {
    const WindowManagerStatus status           = _windowManager.Status();
    std::wstring              disconnectedText = status.connection == Connection::Disconnected ? status.name + L" disconnected" : std::wstring();
    std::vector<Workspace>    shown            = disconnectedText.empty() ? OnMonitor(_windowManager.Workspaces(), _monitor) : std::vector<Workspace>();
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
        _labels.push_back(LayOutWeightedText(render, workspace.label, state.weight, ItemTextInset(state, render.axis)));
        _states.push_back(state);
        contents.push_back(_labels.back().size);
        styles.push_back(&state);
    }
    _row = ArrangeItems(contents, styles, render.axis);
    return true;
}

D2D1_SIZE_F Workspaces::Size() const {
    return _disconnectedText.empty() ? _row.size : _disconnected.size;
}

void Workspaces::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    if (!_disconnectedText.empty()) {
        DrawIconText(render, origin, _disconnected, render.accent, render.alert);
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    for (size_t index = 0; index < _labels.size(); ++index) {
        const ItemStyle& state = _states[index];
        DrawItemBackground(render, _row.boxes[index], state, _fades.Background(index, now), origin);
        DrawIconText(render, ItemContentOrigin(_row.boxes[index], state, origin), _labels[index], render.accent, render.brushes->Get(_fades.Text(index, now)));
    }
}

std::optional<std::uint32_t> Workspaces::MillisecondsUntilChange() const {
    if (_fades.Running(std::chrono::steady_clock::now())) {
        return 0;
    }
    return std::nullopt;
}

bool Workspaces::Hover(std::optional<D2D1_POINT_2F> point) {
    const bool changed = _hovered.Set(point.has_value() ? ItemAt(_row, *point) : std::nullopt);
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return changed;
}

void Workspaces::Click(D2D1_POINT_2F point) {
    const std::optional<size_t> item = ItemAt(_row, point);
    if (item.has_value()) {
        _windowManager.FocusWorkspace(_shown[*item].name);
    }
}

const ModuleActions& Workspaces::Actions() const {
    return kNoActions;
}

std::vector<const ItemStyle*> Workspaces::ItemStyles() const {
    std::vector<const ItemStyle*> styles;
    styles.reserve(_states.size());
    for (const ItemStyle& state : _states) {
        styles.push_back(&state);
    }
    return styles;
}

std::optional<std::wstring> Workspaces::TooltipText() {
    const WindowManagerStatus status = _windowManager.Status();
    if (status.connection != Connection::Disconnected) {
        return std::nullopt;
    }
    return DrawableText(status.detail);
}

const ItemStyle& WorkspaceState(const Workspace& workspace, const ModuleStyle& style) {
    if (workspace.focused) {
        return style.focused;
    }
    return workspace.occupied ? style.occupied : style.empty;
}

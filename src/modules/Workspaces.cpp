#include "modules/Workspaces.hpp"

#include "platform/Text.hpp"

#include <chrono>
#include <cstdint>
#include <utility>

Workspaces::Workspaces(IWindowManager& windowManager, HMONITOR monitor, ModuleStyle style) :
    _windowManager(windowManager), _monitor(reinterpret_cast<std::intptr_t>(monitor)), _style(std::move(style)), _row(EmptyItemRow()), _disconnected(EmptyIconText()) {}

bool Workspaces::Layout(const RenderContext& render) {
    if (!LayOutItems(render)) {
        return false;
    }
    _fades.Follow(ItemStyles(), _hovered, std::chrono::steady_clock::now());
    return true;
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
    _disconnected = _disconnectedText.empty() ? EmptyIconText() : LayOutIconText(render, L"", _disconnectedText);

    std::vector<D2D1_SIZE_F> contents;
    for (const Workspace& workspace : _shown) {
        const ItemStyle& itemStyle = WorkspaceStyle(workspace, _style);
        _labels.push_back(LayOutWeightedText(render, workspace.label, itemStyle.weight, ItemTextInset(itemStyle, render.axis)));
        contents.push_back(_labels.back().size);
    }
    _row = ArrangeItems(contents, ItemStyles(), render.axis);
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
    for (size_t index = 0; index < _labels.size(); ++index) {
        const ItemStyle& itemStyle = WorkspaceStyle(_shown[index], _style);
        DrawItemBackground(render, _row.boxes[index], itemStyle, _fades.Background(index, render.frameTime), _fades.Shadows(index, render.frameTime), origin);
        DrawIconText(render, ItemContentOrigin(_row.boxes[index], itemStyle, origin), _labels[index], render.accent, render.brushes->Get(_fades.Text(index, render.frameTime)));
    }
}

std::optional<std::uint32_t> Workspaces::MillisecondsUntilChange() const {
    return _fades.Wait(std::chrono::steady_clock::now());
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
    styles.reserve(_shown.size());
    for (const Workspace& workspace : _shown) {
        styles.push_back(&WorkspaceStyle(workspace, _style));
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

const ItemStyle& WorkspaceStyle(const Workspace& workspace, const ModuleStyle& style) {
    if (workspace.focused) {
        return style.focused;
    }
    return workspace.occupied ? style.occupied : style.empty;
}

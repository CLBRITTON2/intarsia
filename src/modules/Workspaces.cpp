#include "modules/Workspaces.hpp"

#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <utility>

namespace {

    constexpr std::wstring_view kSeparator = L"  ";

} // namespace

Workspaces::Workspaces(IWindowManager& windowManager, HMONITOR monitor) :
    _windowManager(windowManager), _monitor(reinterpret_cast<std::intptr_t>(monitor)), _line{.text = {}, .runs = {}},
    _text{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}} {}

bool Workspaces::Layout(const RenderContext& render) {
    const WindowManagerStatus status       = _windowManager.Status();
    const bool                disconnected = status.connection == Connection::Disconnected;
    WorkspaceLine             line = disconnected ? WorkspaceLine{.text = status.name + L" disconnected", .runs = {}} : JoinWorkspaces(_windowManager.Workspaces(), _monitor);
    if (_text.layout != nullptr && line == _line && disconnected == _disconnected) {
        return false;
    }
    _line         = std::move(line);
    _disconnected = disconnected;

    _text = LayOutIconText(render, L"", _line.text, disconnected ? render.alert : render.foreground);
    for (const LabelRun& run : _line.runs) {
        if (run.focused) {
            ThrowIfFailed(_text.layout->SetDrawingEffect(render.accent, DWRITE_TEXT_RANGE{.startPosition = run.start, .length = run.length}),
                          "IDWriteTextLayout::SetDrawingEffect");
        }
    }
    return true;
}

D2D1_SIZE_F Workspaces::Size() const {
    return _text.size;
}

void Workspaces::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, _text.layout.Get(), render.foreground);
}

std::optional<UINT> Workspaces::MillisecondsUntilChange() const {
    return std::nullopt;
}

void Workspaces::Click(D2D1_POINT_2F point) {
    if (_text.layout == nullptr) {
        return;
    }
    BOOL                    trailing = FALSE;
    BOOL                    inside   = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    ThrowIfFailed(_text.layout->HitTestPoint(point.x, point.y, &trailing, &inside, &metrics), "IDWriteTextLayout::HitTestPoint");
    if (!inside) {
        return;
    }
    const std::optional<std::string> name = WorkspaceAt(_line, metrics.textPosition);
    if (name.has_value()) {
        _windowManager.FocusWorkspace(*name);
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

WorkspaceLine JoinWorkspaces(const std::vector<Workspace>& workspaces, std::intptr_t monitor) {
    WorkspaceLine line{.text = {}, .runs = {}};
    for (const Workspace& workspace : workspaces) {
        if (workspace.monitor != monitor) {
            continue;
        }
        if (!line.text.empty()) {
            line.text += kSeparator;
        }
        line.runs.push_back(
            LabelRun{.start = static_cast<UINT32>(line.text.size()), .length = static_cast<UINT32>(workspace.label.size()), .focused = workspace.focused, .name = workspace.name});
        line.text += workspace.label;
    }
    return line;
}

std::optional<std::string> WorkspaceAt(const WorkspaceLine& line, UINT32 textPosition) {
    for (const LabelRun& run : line.runs) {
        if (textPosition >= run.start && textPosition < run.start + run.length) {
            return run.name;
        }
    }
    return std::nullopt;
}

#include "modules/Workspaces.hpp"

#include "platform/Errors.hpp"

#include <limits>

namespace {

    constexpr std::wstring_view kSeparator = L"  ";

} // namespace

Workspaces::Workspaces(IWindowManager& windowManager, HMONITOR monitor) :
    _windowManager(windowManager), _monitor(reinterpret_cast<std::intptr_t>(monitor)), _line{.text = {}, .runs = {}}, _size{.width = 0.0f, .height = 0.0f} {}

void Workspaces::Layout(const RenderContext& render) {
    const WindowManagerStatus status       = _windowManager.Status();
    const bool                disconnected = status.connection == Connection::Disconnected;
    _line = disconnected ? WorkspaceLine{.text = status.name + L" disconnected", .runs = {}} : JoinWorkspaces(_windowManager.Workspaces(), _monitor);

    _layout.Reset();
    ThrowIfFailed(render.dwrite->CreateTextLayout(_line.text.c_str(), static_cast<UINT32>(_line.text.size()), render.textFormat, std::numeric_limits<float>::max(),
                                                  std::numeric_limits<float>::max(), &_layout),
                  "IDWriteFactory::CreateTextLayout");
    ThrowIfFailed(_layout->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP), "IDWriteTextLayout::SetWordWrapping");
    if (disconnected) {
        ThrowIfFailed(_layout->SetDrawingEffect(render.alert, DWRITE_TEXT_RANGE{.startPosition = 0, .length = static_cast<UINT32>(_line.text.size())}),
                      "IDWriteTextLayout::SetDrawingEffect");
    }
    for (const LabelRun& run : _line.runs) {
        if (run.focused) {
            ThrowIfFailed(_layout->SetDrawingEffect(render.accent, DWRITE_TEXT_RANGE{.startPosition = run.start, .length = run.length}), "IDWriteTextLayout::SetDrawingEffect");
        }
    }

    DWRITE_TEXT_METRICS metrics{};
    ThrowIfFailed(_layout->GetMetrics(&metrics), "IDWriteTextLayout::GetMetrics");
    _size = D2D1_SIZE_F{.width = metrics.widthIncludingTrailingWhitespace, .height = metrics.height};
}

D2D1_SIZE_F Workspaces::Size() const {
    return _size;
}

void Workspaces::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, _layout.Get(), render.foreground);
}

std::optional<UINT> Workspaces::MillisecondsUntilChange() const {
    return std::nullopt;
}

void Workspaces::Click(D2D1_POINT_2F point) {
    if (_layout == nullptr) {
        return;
    }
    BOOL                    trailing = FALSE;
    BOOL                    inside   = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    ThrowIfFailed(_layout->HitTestPoint(point.x, point.y, &trailing, &inside, &metrics), "IDWriteTextLayout::HitTestPoint");
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

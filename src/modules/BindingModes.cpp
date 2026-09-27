#include "modules/BindingModes.hpp"

#include <cstdint>

BindingModes::BindingModes(const IWindowManager& windowManager) : _windowManager(windowManager) {}

bool BindingModes::Layout(const RenderContext& render) {
    return _line.Update(render, JoinModes(_windowManager.BindingModes()), render.accent);
}

D2D1_SIZE_F BindingModes::Size() const {
    return _line.Size();
}

void BindingModes::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _line.Draw(render, origin);
}

std::optional<std::uint32_t> BindingModes::MillisecondsUntilChange() const {
    return std::nullopt;
}

void                 BindingModes::Click(D2D1_POINT_2F) {}

const ModuleActions& BindingModes::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> BindingModes::TooltipText() {
    return std::nullopt;
}

std::wstring JoinModes(const std::vector<std::wstring>& modes) {
    std::wstring joined;
    for (const std::wstring& mode : modes) {
        joined += joined.empty() ? L"" : L"  ";
        joined += mode;
    }
    return joined;
}

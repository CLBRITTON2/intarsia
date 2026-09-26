#include "modules/DirectionToggle.hpp"

#include <format>
#include <stdexcept>
#include <utility>

DirectionToggle::DirectionToggle(TilingDirectionConfig config, IWindowManager& windowManager) : _config(std::move(config)), _windowManager(windowManager) {}

bool DirectionToggle::Layout(const RenderContext& render) {
    return _line.Update(render, DirectionText(_config, _windowManager.Direction()), render.foreground);
}

D2D1_SIZE_F DirectionToggle::Size() const {
    return _line.Size();
}

void DirectionToggle::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _line.Draw(render, origin);
}

std::optional<UINT> DirectionToggle::MillisecondsUntilChange() const {
    return std::nullopt;
}

void DirectionToggle::Click(D2D1_POINT_2F) {
    _windowManager.ToggleTilingDirection();
}

const ModuleActions& DirectionToggle::Actions() const {
    static const ModuleActions kNone{.click = std::nullopt, .rightClick = std::nullopt, .scrollUp = std::nullopt, .scrollDown = std::nullopt};
    return kNone;
}

std::optional<std::wstring> DirectionToggle::TooltipText() {
    return std::nullopt;
}

std::wstring DirectionText(const TilingDirectionConfig& config, std::optional<TilingDirection> direction) {
    if (!direction.has_value()) {
        return {};
    }
    switch (*direction) {
        case TilingDirection::Horizontal: return config.horizontal;
        case TilingDirection::Vertical: return config.vertical;
    }
    throw std::invalid_argument(std::format("unknown tiling direction {}", static_cast<int>(*direction)));
}

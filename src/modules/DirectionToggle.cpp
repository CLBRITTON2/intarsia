#include "modules/DirectionToggle.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

DirectionToggle::DirectionToggle(TilingDirectionConfig config, IWindowManager& windowManager) : _config(std::move(config)), _windowManager(windowManager) {}

// The direction's text is a glyph, so it draws as an icon: icon font, icon size, icon color.
bool DirectionToggle::Layout(const RenderContext& render) {
    return _glyph.Update(render, ModuleText{.icon = DirectionText(_config, _windowManager.Direction()), .text = {}, .state = TextState::Normal});
}

D2D1_SIZE_F DirectionToggle::Size() const {
    return _glyph.Size();
}

void DirectionToggle::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _glyph.Draw(render, origin);
}

void DirectionToggle::Click(D2D1_POINT_2F) {
    _windowManager.ToggleTilingDirection();
}

const ModuleActions& DirectionToggle::Actions() const {
    return kNoActions;
}

std::wstring DirectionText(const TilingDirectionConfig& config, std::optional<TilingDirection> direction) {
    if (!direction.has_value()) {
        return {};
    }
    switch (*direction) {
        case TilingDirection::Horizontal: return config.horizontal;
        case TilingDirection::Vertical: return config.vertical;
    }
    throw std::invalid_argument(std::format("unknown tiling direction {}", static_cast<std::int32_t>(*direction)));
}

#include "modules/DirectionToggle.hpp"

#include <cstdint>
#include <format>
#include <stdexcept>
#include <utility>

DirectionToggle::DirectionToggle(TilingDirectionConfig config, IWindowManager& windowManager) :
    _config(std::move(config)), _windowManager(windowManager), _glyph(EmptyIconText()) {}

// The direction's text is a glyph, so it draws as an icon: icon font, icon size, icon color.
bool DirectionToggle::Layout(const RenderContext& render) {
    std::wstring shown = DirectionText(_config, _windowManager.Direction());
    if (_laidOut && shown == _shown) {
        return false;
    }
    _glyph   = LayOutIconText(render, shown, L"");
    _shown   = std::move(shown);
    _laidOut = true;
    return true;
}

D2D1_SIZE_F DirectionToggle::Size() const {
    return _glyph.size;
}

void DirectionToggle::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    DrawIconText(render, origin, _glyph, render.foreground);
}

std::optional<std::uint32_t> DirectionToggle::MillisecondsUntilChange() const {
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
    throw std::invalid_argument(std::format("unknown tiling direction {}", static_cast<std::int32_t>(*direction)));
}

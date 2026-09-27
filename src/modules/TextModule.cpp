#include "modules/TextModule.hpp"

#include "modules/Ticks.hpp"

#include <cstdint>
#include <chrono>
#include <utility>

TextModule::TextModule(TextModuleConfig config) : _config(std::move(config)), _shown{.text = {}, .alert = false}, _text(EmptyIconText()) {}

bool TextModule::Layout(const RenderContext& render) {
    ModuleText content = Format(_config.format);
    if (_laidOut && content == _shown) {
        return false;
    }
    _text    = LayOutIconText(render, _config.icon, content.text);
    _shown   = std::move(content);
    _laidOut = true;
    return true;
}

D2D1_SIZE_F TextModule::Size() const {
    return _text.size;
}

void TextModule::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    DrawIconText(render, origin, _text, _shown.alert ? render.alert : render.foreground);
}

std::optional<std::uint32_t> TextModule::MillisecondsUntilChange() const {
    return static_cast<std::uint32_t>(TimeUntilNextTick(std::chrono::system_clock::now(), _config.interval).count());
}

void                 TextModule::Click(D2D1_POINT_2F) {}

const ModuleActions& TextModule::Actions() const {
    return _config.actions;
}

std::optional<std::wstring> TextModule::TooltipText() {
    if (!_config.tooltip.has_value()) {
        return std::nullopt;
    }
    return Format(*_config.tooltip).text;
}

const TextModuleConfig& TextModule::Settings() const {
    return _config;
}

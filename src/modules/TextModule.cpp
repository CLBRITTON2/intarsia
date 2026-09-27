#include "modules/TextModule.hpp"

#include "modules/Ticks.hpp"

#include <cstdint>
#include <chrono>
#include <utility>

TextModule::TextModule(TextModuleConfig config) :
    _config(std::move(config)), _shown{.text = {}, .alert = false}, _text{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}} {}

bool TextModule::Layout(const RenderContext& render) {
    ModuleText content = Format(_config.format);
    if (_text.layout != nullptr && content == _shown) {
        return false;
    }
    _text  = LayOutIconText(render, _config.icon, content.text, content.alert ? render.alert : render.foreground);
    _shown = std::move(content);
    return true;
}

D2D1_SIZE_F TextModule::Size() const {
    return _text.size;
}

void TextModule::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, _text.layout.Get(), render.foreground);
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

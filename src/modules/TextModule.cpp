#include "modules/TextModule.hpp"

#include "modules/Ticks.hpp"

#include <chrono>
#include <utility>

TextModule::TextModule(TextModuleConfig config) : _config(std::move(config)), _text{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}} {}

void TextModule::Layout(const RenderContext& render) {
    _text = LayOutIconText(render, _config.icon, Text());
}

D2D1_SIZE_F TextModule::Size() const {
    return _text.size;
}

void TextModule::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    render.context->DrawTextLayout(origin, _text.layout.Get(), render.foreground);
}

std::optional<UINT> TextModule::MillisecondsUntilChange() const {
    return static_cast<UINT>(TimeUntilNextTick(std::chrono::system_clock::now(), _config.interval).count());
}

void                 TextModule::Click(D2D1_POINT_2F) {}

const ModuleActions& TextModule::Actions() const {
    return _config.actions;
}

const TextModuleConfig& TextModule::Settings() const {
    return _config;
}

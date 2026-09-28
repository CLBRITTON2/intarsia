#include "modules/TextModule.hpp"

#include "modules/Ticks.hpp"

#include <cstdint>
#include <chrono>
#include <utility>

TextModule::TextModule(TextModuleConfig config) : _config(std::move(config)) {}

bool TextModule::Layout(const RenderContext& render) {
    return _line.Update(render, Format(_config.format));
}

D2D1_SIZE_F TextModule::Size() const {
    return _line.Size();
}

void TextModule::Draw(const RenderContext& render, D2D1_POINT_2F origin) const {
    _line.Draw(render, origin);
}

std::optional<std::uint32_t> TextModule::MillisecondsUntilChange() const {
    return static_cast<std::uint32_t>(TimeUntilNextTick(std::chrono::system_clock::now(), _config.interval).count());
}

bool TextModule::Hover(std::optional<D2D1_POINT_2F>) {
    return false;
}

void                       TextModule::Scroll(ScrollDirection) {}

void                       TextModule::Click(D2D1_POINT_2F) {}

void                       TextModule::RightClick(D2D1_POINT_2F) {}

std::optional<D2D1_RECT_F> TextModule::TooltipItem() const {
    return std::nullopt;
}

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

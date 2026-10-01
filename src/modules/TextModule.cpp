#include "modules/TextModule.hpp"

#include "modules/Ticks.hpp"

#include <cstdint>
#include <chrono>
#include <format>
#include <utility>

TextModule::TextModule(TextModuleConfig config, std::string_view type, const std::vector<Placeholder>& samples) : IconTextModule(std::move(config)) {
    RequirePlaceholders(std::format("{} format", type), Settings().format, samples);
    RequireOptionalPlaceholders(std::format("{} tooltip", type), Settings().tooltip, samples);
}

std::optional<std::uint32_t> TextModule::MillisecondsUntilChange() const {
    return static_cast<std::uint32_t>(TimeUntilNextTick(std::chrono::system_clock::now(), Settings().interval).count());
}

std::optional<std::wstring> TextModule::TooltipText() {
    if (!Settings().tooltip.has_value()) {
        return std::nullopt;
    }
    return Format(*Settings().tooltip).text;
}

ModuleText TextModule::Content() {
    return Format(Settings().format);
}

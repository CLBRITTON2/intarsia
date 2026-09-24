#include "modules/ModuleFactory.hpp"

#include "modules/Clock.hpp"
#include "modules/Window.hpp"
#include "modules/Workspaces.hpp"

#include <format>
#include <optional>
#include <stdexcept>
#include <string_view>

namespace {

    template <typename ModuleConfig>
    const ModuleConfig& RequireModuleConfig(const std::optional<ModuleConfig>& config, std::string_view name) {
        if (!config.has_value()) {
            throw std::logic_error(std::format("{} is listed in a section without a {} config, LoadConfig should have rejected this", name, name));
        }
        return *config;
    }

} // namespace

std::unique_ptr<IModule> CreateModule(ModuleKind kind, const Config& config, const ModuleSources& sources) {
    switch (kind) {
        case ModuleKind::Clock: return std::make_unique<Clock>(RequireModuleConfig(config.clock, "clock"));
        case ModuleKind::Window: return std::make_unique<Window>(RequireModuleConfig(config.window, "window"), sources.windowManager);
        case ModuleKind::Workspaces: return std::make_unique<Workspaces>(sources.windowManager, sources.monitor);
    }
    throw std::invalid_argument(std::format("unknown module kind {}", static_cast<int>(kind)));
}

Sections<std::unique_ptr<IModule>> CreateModules(const Config& config, const ModuleSources& sources) {
    return MapSections(config.modules, [&config, &sources](ModuleKind kind) { return CreateModule(kind, config, sources); });
}

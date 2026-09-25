#include "modules/ModuleFactory.hpp"

#include "modules/Clock.hpp"
#include "modules/Cpu.hpp"
#include "modules/Custom.hpp"
#include "modules/Memory.hpp"
#include "modules/Window.hpp"
#include "modules/Workspaces.hpp"

#include <format>
#include <stdexcept>
#include <variant>

namespace {

    template <typename Settings>
    const Settings& RequireSettings(const ModuleInstance& instance) {
        const Settings* settings = std::get_if<Settings>(&instance.settings);
        if (settings == nullptr) {
            throw std::logic_error(std::format("module {} has the wrong settings for its type, LoadConfig should have read them", instance.name));
        }
        return *settings;
    }

} // namespace

std::unique_ptr<IModule> CreateModule(const ModuleInstance& instance, const ModuleSources& sources) {
    switch (instance.kind) {
        case ModuleKind::Clock: return std::make_unique<Clock>(RequireSettings<TextModuleConfig>(instance));
        case ModuleKind::Window: return std::make_unique<Window>(RequireSettings<WindowConfig>(instance), sources.windowManager);
        case ModuleKind::Workspaces: return std::make_unique<Workspaces>(sources.windowManager, sources.monitor);
        case ModuleKind::Cpu: return std::make_unique<Cpu>(RequireSettings<TextModuleConfig>(instance));
        case ModuleKind::Memory: return std::make_unique<Memory>(RequireSettings<TextModuleConfig>(instance));
        case ModuleKind::Custom: return std::make_unique<Custom>(RequireSettings<CustomConfig>(instance), sources.commandPollers.For(instance.name));
    }
    throw std::invalid_argument(std::format("unknown module kind {}", static_cast<int>(instance.kind)));
}

Sections<std::unique_ptr<IModule>> CreateModules(const Config& config, const ModuleSources& sources) {
    return MapSections(config.modules, [&sources](const ModuleInstance& instance) { return CreateModule(instance, sources); });
}

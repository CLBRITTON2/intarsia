#include "modules/ModuleFactory.hpp"

#include "modules/Audio.hpp"
#include "modules/BindingModes.hpp"
#include "modules/Clock.hpp"
#include "modules/Cpu.hpp"
#include "modules/Custom.hpp"
#include "modules/Battery.hpp"
#include "modules/DirectionToggle.hpp"
#include "modules/Divider.hpp"
#include "modules/Memory.hpp"
#include "modules/Network.hpp"
#include "modules/Tray.hpp"
#include "modules/Window.hpp"
#include "modules/WindowList.hpp"
#include "modules/Workspaces.hpp"

#include <cstdint>
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
        case ModuleKind::Workspaces: return std::make_unique<Workspaces>(sources.windowManager, sources.monitor, instance.style);
        case ModuleKind::Windows: return std::make_unique<WindowList>(sources.windowManager, sources.windowIcons, sources.monitor, instance.style);
        case ModuleKind::BindingMode: return std::make_unique<BindingModes>(sources.windowManager, instance.style);
        case ModuleKind::TilingDirection: return std::make_unique<DirectionToggle>(RequireSettings<TilingDirectionConfig>(instance), sources.windowManager);
        case ModuleKind::Divider: return std::make_unique<Divider>(instance.style);
        case ModuleKind::Cpu: return std::make_unique<Cpu>(RequireSettings<TextModuleConfig>(instance));
        case ModuleKind::Memory: return std::make_unique<Memory>(RequireSettings<TextModuleConfig>(instance));
        case ModuleKind::Audio: return std::make_unique<Audio>(RequireSettings<AudioConfig>(instance), sources.audio);
        case ModuleKind::Network: return std::make_unique<Network>(RequireSettings<NetworkConfig>(instance), sources.network);
        case ModuleKind::Battery: return std::make_unique<Battery>(RequireSettings<BatteryConfig>(instance), sources.battery);
        case ModuleKind::Tray:
            if (sources.tray == nullptr) {
                throw std::logic_error(std::format("module {} is a tray, and App created no tray for it", instance.name));
            }
            return std::make_unique<Tray>(RequireSettings<TrayConfig>(instance), *sources.tray, instance.style);
        case ModuleKind::Custom: return std::make_unique<Custom>(RequireSettings<CustomConfig>(instance), sources.commandPollers.For(instance.name));
    }
    throw std::invalid_argument(std::format("unknown module kind {}", static_cast<std::int32_t>(instance.kind)));
}

Sections<std::unique_ptr<IModule>> CreateModules(const Sections<ModuleInstance>& modules, const ModuleSources& sources) {
    return MapSections(modules, [&sources](const ModuleInstance& instance) { return CreateModule(instance, sources); });
}

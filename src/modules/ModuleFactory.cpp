#include "modules/ModuleFactory.hpp"

#include "modules/Audio.hpp"
#include "modules/BindingModes.hpp"
#include "modules/Clock.hpp"
#include "modules/Counter.hpp"
#include "modules/Cpu.hpp"
#include "modules/Custom.hpp"
#include "modules/Battery.hpp"
#include "modules/DirectionToggle.hpp"
#include "modules/Memory.hpp"
#include "modules/Network.hpp"
#include "modules/Tray.hpp"
#include "modules/Window.hpp"
#include "modules/WindowList.hpp"
#include "modules/Workspaces.hpp"

#include <cstdint>
#include <format>
#include <memory>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

namespace {

    template <typename Settings>
    const Settings& RequireSettings(const ModuleInstance& instance) {
        const Settings* settings = std::get_if<Settings>(&instance.settings);
        if (settings == nullptr) {
            throw std::logic_error(std::format("module {} has the wrong settings for its type, LoadConfig should have read them", instance.name));
        }
        return *settings;
    }

    template <typename Source>
    Source& RequireSource(Source* source, const ModuleInstance& instance) {
        if (source == nullptr) {
            throw std::logic_error(std::format("module {} reads a source App created no instance of for its kind", instance.name));
        }
        return *source;
    }

    std::unique_ptr<IModule> CreateModule(const ModuleInstance& instance, const ModuleSources& sources) {
        switch (instance.kind) {
            case ModuleKind::Clock: return std::make_unique<Clock>(RequireSettings<TextModuleConfig>(instance));
            case ModuleKind::Window: return std::make_unique<Window>(RequireSettings<WindowConfig>(instance), sources.windowManager);
            case ModuleKind::Workspaces: return std::make_unique<Workspaces>(sources.windowManager, sources.monitor, instance.style);
            case ModuleKind::Windows: return std::make_unique<WindowList>(sources.windowManager, RequireSource(sources.windowIcons, instance), sources.monitor, instance.style);
            case ModuleKind::BindingMode: return std::make_unique<BindingModes>(sources.windowManager, instance.style);
            case ModuleKind::TilingDirection: return std::make_unique<DirectionToggle>(RequireSettings<TilingDirectionConfig>(instance), sources.windowManager);
            case ModuleKind::Cpu: return std::make_unique<Cpu>(RequireSettings<TextModuleConfig>(instance));
            case ModuleKind::Memory: return std::make_unique<Memory>(RequireSettings<TextModuleConfig>(instance));
            case ModuleKind::Audio: {
                const AudioConfig& config = RequireSettings<AudioConfig>(instance);
                return std::make_unique<Audio>(config, RequireSource(config.flow == AudioFlow::Output ? sources.audioOutput : sources.audioInput, instance));
            }
            case ModuleKind::Network: return std::make_unique<Network>(RequireSettings<NetworkConfig>(instance), RequireSource(sources.network, instance));
            case ModuleKind::Battery: return std::make_unique<Battery>(RequireSettings<BatteryConfig>(instance), RequireSource(sources.battery, instance));
            case ModuleKind::Tray: return std::make_unique<Tray>(RequireSettings<TrayConfig>(instance), RequireSource(sources.tray, instance), instance.style);
            case ModuleKind::Custom: return std::make_unique<Custom>(RequireSettings<CustomConfig>(instance), sources.commandPollers.For(instance.name));
            case ModuleKind::Counter: return std::make_unique<Counter>(RequireSettings<CounterConfig>(instance), sources.counterSamplers.For(instance.name));
            case ModuleKind::Group: throw std::logic_error(std::format("module {} is a group, which CreateEntry builds with its members", instance.name));
        }
        throw std::invalid_argument(std::format("unknown module kind {}", static_cast<std::int32_t>(instance.kind)));
    }

    ModuleEntry CreateEntry(const ModuleInstance& instance, const ModuleSources& sources) {
        const auto* settings = std::get_if<GroupConfig>(&instance.settings);
        if (settings == nullptr) {
            std::unique_ptr<IModule> module = CreateModule(instance, sources);
            return ModuleEntry{.module = std::move(module), .style = instance.style, .group = nullptr, .members = {}};
        }
        auto                     group = std::make_unique<Group>(*settings, instance.style);
        const Group*             view  = group.get();
        std::vector<ModuleEntry> members;
        members.reserve(settings->members.size());
        for (const ModuleInstance& member : settings->members) {
            members.push_back(CreateEntry(member, sources));
        }
        return ModuleEntry{.module = std::move(group), .style = instance.style, .group = view, .members = std::move(members)};
    }

} // namespace

Sections<ModuleEntry> CreateModules(const Sections<ModuleInstance>& modules, const ModuleSources& sources) {
    return MapSections(modules, [&sources](const ModuleInstance& instance) { return CreateEntry(instance, sources); });
}

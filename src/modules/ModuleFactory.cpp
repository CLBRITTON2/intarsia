#include "modules/ModuleFactory.hpp"

#include "modules/Audio.hpp"
#include "modules/Battery.hpp"
#include "modules/BindingModes.hpp"
#include "modules/Clock.hpp"
#include "modules/Counter.hpp"
#include "modules/Cpu.hpp"
#include "modules/Custom.hpp"
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

    template <typename Source>
    Source& RequireSource(Source* source, const ModuleInstance& instance) {
        if (source == nullptr) {
            throw std::logic_error(std::format("module {} reads a source App created no instance of for its kind", instance.name));
        }
        return *source;
    }

    IAudio* AudioSourceFor(AudioFlow flow, const ModuleSources& sources) {
        if (flow == AudioFlow::Output) {
            return sources.audioOutput;
        }
        return sources.audioInput;
    }

    std::unique_ptr<IModule> CreateModule(const ModuleInstance& instance, const ModuleSources& sources) {
        switch (instance.kind) {
            case ModuleKind::Clock: return std::make_unique<Clock>(std::get<TextModuleConfig>(instance.settings));
            case ModuleKind::Window: return std::make_unique<Window>(std::get<WindowConfig>(instance.settings), sources.windowManager);
            case ModuleKind::Workspaces: return std::make_unique<Workspaces>(sources.windowManager, sources.monitor, instance.style);
            case ModuleKind::Windows: return std::make_unique<WindowList>(sources.windowManager, RequireSource(sources.windowIcons, instance), sources.monitor, instance.style);
            case ModuleKind::BindingMode: return std::make_unique<BindingModes>(sources.windowManager, instance.style);
            case ModuleKind::TilingDirection: return std::make_unique<DirectionToggle>(std::get<TilingDirectionConfig>(instance.settings), sources.windowManager);
            case ModuleKind::Cpu: return std::make_unique<Cpu>(std::get<TextModuleConfig>(instance.settings));
            case ModuleKind::Memory: return std::make_unique<Memory>(std::get<TextModuleConfig>(instance.settings));
            case ModuleKind::Audio: {
                const AudioConfig& config = std::get<AudioConfig>(instance.settings);
                return std::make_unique<Audio>(config, RequireSource(AudioSourceFor(config.flow, sources), instance));
            }
            case ModuleKind::Network: return std::make_unique<Network>(std::get<NetworkConfig>(instance.settings), RequireSource(sources.network, instance));
            case ModuleKind::Battery: return std::make_unique<Battery>(std::get<BatteryConfig>(instance.settings), RequireSource(sources.battery, instance));
            case ModuleKind::Tray: return std::make_unique<Tray>(std::get<TrayConfig>(instance.settings), RequireSource(sources.tray, instance), instance.style);
            case ModuleKind::Custom: return std::make_unique<Custom>(std::get<CustomConfig>(instance.settings), sources.commandPollers.For(instance.name));
            case ModuleKind::Counter: return std::make_unique<Counter>(std::get<CounterConfig>(instance.settings), sources.counterSamplers.For(instance.name));
            case ModuleKind::Group: throw std::logic_error(std::format("module {} is a group, which CreateEntry builds with its members", instance.name));
        }
        throw std::invalid_argument(std::format("unknown module kind {}", static_cast<std::int32_t>(instance.kind)));
    }

    std::vector<ModuleEntry> CreateEntries(const std::vector<ModuleInstance>& instances, const ModuleSources& sources);

    // A group's entry holds its members' entries, and LoadConfig makes sure no member is a group.
    ModuleEntry CreateEntry(const ModuleInstance& instance, const ModuleSources& sources) {
        const auto* settings = std::get_if<GroupConfig>(&instance.settings);
        if (settings == nullptr) {
            std::unique_ptr<IModule> module = CreateModule(instance, sources);
            return ModuleEntry{
                .module  = std::move(module),
                .style   = instance.style,
                .group   = nullptr,
                .members = {},
            };
        }
        auto                     group   = std::make_unique<Group>(*settings, instance.style);
        const Group*             view    = group.get();
        std::vector<ModuleEntry> members = CreateEntries(settings->members, sources);
        return ModuleEntry{
            .module  = std::move(group),
            .style   = instance.style,
            .group   = view,
            .members = std::move(members),
        };
    }

    std::vector<ModuleEntry> CreateEntries(const std::vector<ModuleInstance>& instances, const ModuleSources& sources) {
        std::vector<ModuleEntry> entries;
        entries.reserve(instances.size());
        for (const ModuleInstance& instance : instances) {
            entries.push_back(CreateEntry(instance, sources));
        }
        return entries;
    }

} // namespace

Sections<ModuleEntry> CreateModules(const Sections<ModuleInstance>& modules, const ModuleSources& sources) {
    return Sections<ModuleEntry>{
        .left   = CreateEntries(modules.left, sources),
        .center = CreateEntries(modules.center, sources),
        .right  = CreateEntries(modules.right, sources),
    };
}

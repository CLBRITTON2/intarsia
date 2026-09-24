#include "modules/ModuleFactory.hpp"

#include "modules/Clock.hpp"

#include <format>
#include <stdexcept>

std::unique_ptr<IModule> CreateModule(ModuleKind kind, const Config& config) {
    switch (kind) {
        case ModuleKind::Clock:
            if (!config.clock.has_value()) {
                throw std::logic_error("clock is listed in a section without a clock config, LoadConfig should have rejected this");
            }
            return std::make_unique<Clock>(*config.clock);
    }
    throw std::invalid_argument(std::format("unknown module kind {}", static_cast<int>(kind)));
}

Sections<std::unique_ptr<IModule>> CreateModules(const Config& config) {
    return MapSections(config.modules, [&config](ModuleKind kind) { return CreateModule(kind, config); });
}

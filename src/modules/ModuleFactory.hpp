#pragma once

#include "bar/Sections.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"
#include "wm/IWindowManager.hpp"

#include <memory>

// Data sources that outlive every bar and that modules read from.
struct ModuleSources {
    const IWindowManager& windowManager;
};

std::unique_ptr<IModule> CreateModule(ModuleKind kind, const Config& config, const ModuleSources& sources);

// Fresh module instances for one bar, in the order the config lists them.
Sections<std::unique_ptr<IModule>> CreateModules(const Config& config, const ModuleSources& sources);

#pragma once

#include "bar/Sections.hpp"
#include "config/Config.hpp"
#include "modules/IModule.hpp"

#include <memory>

std::unique_ptr<IModule> CreateModule(ModuleKind kind, const Config& config);

// Fresh module instances for one bar, in the order the config lists them.
Sections<std::unique_ptr<IModule>> CreateModules(const Config& config);

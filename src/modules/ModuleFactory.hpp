#pragma once

#include "audio/IAudio.hpp"
#include "bar/Sections.hpp"
#include "config/Config.hpp"
#include "modules/CommandPoller.hpp"
#include "modules/IModule.hpp"
#include "modules/WindowIcons.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <memory>

// What a module reads beyond its own config: shared data sources, and the monitor of the bar it belongs to.
struct ModuleSources {
    IWindowManager&       windowManager;
    const CommandPollers& commandPollers;
    WindowIcons&          windowIcons;
    IAudio&               audio;
    HMONITOR              monitor;
};

std::unique_ptr<IModule> CreateModule(const ModuleInstance& instance, const ModuleSources& sources);

// Fresh module instances for one bar, in the order the config lists them.
Sections<std::unique_ptr<IModule>> CreateModules(const Config& config, const ModuleSources& sources);

#pragma once

#include "audio/IAudio.hpp"
#include "bar/Sections.hpp"
#include "battery/IBattery.hpp"
#include "config/Config.hpp"
#include "modules/CommandPoller.hpp"
#include "modules/IModule.hpp"
#include "modules/WindowIcons.hpp"
#include "network/INetwork.hpp"
#include "tray/ITray.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <memory>

// What a module reads beyond its own config: shared data sources, and the monitor of the bar it belongs to.
struct ModuleSources {
    IWindowManager&       windowManager;
    const CommandPollers& commandPollers;
    WindowIcons&          windowIcons;
    IAudio&               audio;
    const INetwork&       network;
    const IBattery&       battery;
    // Null unless the config shows a tray module: the tray takes over every app's icons while it exists.
    ITray*   tray;
    HMONITOR monitor;
};

std::unique_ptr<IModule> CreateModule(const ModuleInstance& instance, const ModuleSources& sources);

// Fresh module instances for one bar, in the order its sections list them.
Sections<std::unique_ptr<IModule>> CreateModules(const Sections<ModuleInstance>& modules, const ModuleSources& sources);

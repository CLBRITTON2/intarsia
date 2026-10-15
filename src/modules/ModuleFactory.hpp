#pragma once

#include "audio/IAudio.hpp"
#include "battery/IBattery.hpp"
#include "config/Config.hpp"
#include "modules/Counter.hpp"
#include "modules/Custom.hpp"
#include "modules/Group.hpp"
#include "modules/IModule.hpp"
#include "modules/WindowList.hpp"
#include "network/INetwork.hpp"
#include "tray/ITray.hpp"
#include "wm/IWindowManager.hpp"

#include <windows.h>

#include <memory>
#include <vector>

// What a module reads beyond its own config: shared data sources, and the monitor of the bar it belongs to. Each
// pointer is null unless the config shows a module of its kind, since a source samples the system for as long as it
// exists, and the tray takes over every app's icons.
struct ModuleSources {
    IWindowManager&        windowManager;
    const CommandPollers&  commandPollers;
    const CounterSamplers& counterSamplers;
    WindowIcons*           windowIcons;
    IAudio*                audioOutput;
    IAudio*                audioInput;
    const INetwork*        network;
    const IBattery*        battery;
    ITray*                 tray;
    HMONITOR               monitor;
};

// One module a bar shows, with its style. A group's entry also holds the group and its members, which the bar lays out
// after the group's toggle.
struct ModuleEntry {
    std::unique_ptr<IModule> module;
    ModuleStyle              style;
    const Group*             group;   // module, for a group, and null for any other type
    std::vector<ModuleEntry> members; // a group's, in the order it lists them
};

// Every entry of every section, each group's members right after it.
template <typename Visit>
void ForEachEntry(const Sections<ModuleEntry>& modules, Visit visit) {
    ForEachInSections(modules, [&visit](const ModuleEntry& entry) {
        visit(entry);
        for (const ModuleEntry& member : entry.members) {
            visit(member);
        }
    });
}

// Fresh module instances for one bar, in the order its sections list them.
Sections<ModuleEntry> CreateModules(const Sections<ModuleInstance>& modules, const ModuleSources& sources);

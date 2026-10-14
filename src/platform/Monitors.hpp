#pragma once

#include <windows.h>

#include <optional>
#include <string>
#include <vector>

// One screen a monitor's picture goes to. A mirrored monitor has several.
struct DisplayTarget {
    std::optional<std::string> id;   // MonitorId, absent for a screen with no EDID (a virtual display)
    std::optional<std::string> name; // Windows' friendly name, absent when it gives none
    std::string                connection;
    bool                       internal; // a laptop's own panel

    bool                       operator==(const DisplayTarget& other) const = default;
};

struct MonitorDescription {
    HMONITOR                   monitor;
    bool                       primary;
    std::vector<DisplayTarget> targets; // empty when Windows reports no active path for it

    bool                       operator==(const MonitorDescription& other) const = default;
};

// Every monitor's handle, in Windows' order.
std::vector<HMONITOR> ListMonitors();

// Throws MonitorGoneError when the handle no longer names a monitor.
MONITORINFOEXW MonitorInfo(HMONITOR monitor);

// Each monitor's targets from the active display paths and their EDIDs. Every failure throws MonitorGoneError, since
// any of them can come from a monitor that goes or a display change while it is read.
std::vector<MonitorDescription> DescribeMonitors(const std::vector<HMONITOR>& monitors);

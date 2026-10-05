#pragma once

#include "config/Config.hpp"
#include "platform/Monitors.hpp"

#include <string>
#include <vector>

// The --monitors listing: one row per target with its id, name, connection, whether its monitor is primary and whether
// bar gives that monitor a bar, in columns under a header.
std::string MonitorTable(const std::vector<MonitorDescription>& monitors, const BarConfig& bar);

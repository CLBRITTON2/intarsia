#pragma once

#include "config/Config.hpp"
#include "platform/Monitors.hpp"

#include <string>
#include <vector>

// The intarsia-cli monitors listing: one row per target with its id, name, connection, whether its monitor is primary and whether
// bar gives that monitor a bar, in columns under a header. Each unsafe character of a name prints as U+FFFD.
std::string MonitorTable(const std::vector<MonitorDescription>& descriptions, const BarConfig& bar);

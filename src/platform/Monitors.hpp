#pragma once

#include <windows.h>

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

// Who a monitor is, from its EDID. model is the manufacturer's three letters and the product code in hex
// (DEL41A8). serial is the descriptor's text serial, else the base block's number, and absent when the monitor leaves
// both empty, as many laptop panels do.
struct EdidIdentity {
    std::string model;
    std::optional<std::string> serial;

    bool operator==(const EdidIdentity& other) const = default;
};

class EdidError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

// Reads the 128-byte base block. Throws EdidError when it is short or lacks the EDID header. The checksum is not
// checked, since a monitor with a wrong one still names itself correctly and Windows shows it anyway.
EdidIdentity ParseEdid(std::span<const std::uint8_t> edid);

// model-serial, or model alone without a serial: the same for one physical monitor on any port, adapter or dock.
std::string MonitorId(const EdidIdentity& identity);

// Whether text has MonitorId's shape, so a config can refuse a misspelled keyword instead of matching nothing.
bool IsMonitorId(std::string_view text);

// One screen a monitor's picture goes to. A mirrored monitor has several.
struct DisplayTarget {
    std::optional<std::string> id;   // MonitorId, absent for a screen with no EDID (a virtual display)
    std::optional<std::string> name; // Windows' friendly name, absent when it gives none
    std::string connection;

    // A laptop's own panel.
    bool IsInternal() const;
    bool operator==(const DisplayTarget& other) const = default;
};

struct MonitorDescription {
    HMONITOR monitor;
    bool primary;
    std::vector<DisplayTarget> targets; // empty when Windows reports no active path for it

    bool operator==(const MonitorDescription& other) const = default;
};

// Every monitor's handle, in Windows' order.
std::vector<HMONITOR> ListMonitors();

// Throws MonitorGoneError when the handle no longer names a monitor.
MONITORINFOEXW MonitorInfo(HMONITOR monitor);

// Each monitor's targets from the active display paths and their EDIDs. Every failure throws MonitorGoneError, since
// any of them can come from a monitor that goes or a display change while it is read.
std::vector<MonitorDescription> DescribeMonitors(const std::vector<HMONITOR>& monitors);

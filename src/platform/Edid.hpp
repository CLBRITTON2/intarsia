#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

// Who a monitor is, from its EDID. model is the manufacturer's three letters and the product code in hex
// (DEL41A8). serial is the descriptor's text serial, else the base block's number, and absent when the monitor leaves
// both empty, as many laptop panels do.
struct EdidIdentity {
    std::string                model;
    std::optional<std::string> serial;

    bool                       operator==(const EdidIdentity& other) const = default;
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

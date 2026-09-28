#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// One value a format can name, as {signal} or {down}.
struct Placeholder {
    std::string_view                                  name;
    std::variant<std::int64_t, double, std::string> value;
};

// The format with each {name} replaced by that placeholder's value, and {name:spec} formatted by spec as std::format
// formats it ("{signal:>3}"). {{ and }} are literal braces. Throws std::format_error for a name no placeholder has, a
// spec that does not fit the value, or an unmatched brace.
std::string FormatPlaceholders(std::string_view format, const std::vector<Placeholder>& placeholders);

// Formats sample values once so a bad format fails when the config loads, not on the first redraw. Throws ConfigError
// naming the key and the names the format may use.
void RequirePlaceholders(std::string_view key, const std::string& format, const std::vector<Placeholder>& samples);

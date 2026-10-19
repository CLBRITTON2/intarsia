#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

// One value a format can name, as {signal} or {down}.
struct Placeholder {
    std::string_view name;
    std::variant<std::int64_t, double, std::string, std::chrono::zoned_time<std::chrono::seconds>> value;
};

// The format with each {name} replaced by that placeholder's value, and {name:spec} formatted by spec as std::format
// formats it ("{signal:>3}", "{time:%H:%M}"), as wide text. {{ and }} are literal braces. Throws std::format_error for
// a name no placeholder has, a spec that does not fit the value, or an unmatched brace.
std::wstring FormatPlaceholders(std::string_view format, const std::vector<Placeholder>& placeholders);

// Whether the format has a {name} or {name:spec} field. Throws std::format_error for an unmatched brace.
bool FormatNames(std::string_view format, std::string_view name);

// A rate in SI units with one decimal, as "850 B/s", "1.2 kB/s" or "3.4 MB/s".
std::string FormatRate(double bytesPerSecond);

// Formats sample values once so a bad format fails when the config loads, not on the first redraw. Throws ConfigError
// naming the key and the names the format may use.
void RequirePlaceholders(std::string_view key, const std::string& format, const std::vector<Placeholder>& samples);

// RequirePlaceholders for a format the config may leave out, such as a tooltip.
void RequireOptionalPlaceholders(std::string_view key, const std::optional<std::string>& format, const std::vector<Placeholder>& samples);

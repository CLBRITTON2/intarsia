#include "modules/Placeholders.hpp"

#include "config/Config.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <functional>

namespace {

    // The std::format pattern for one value formatted by spec.
    std::string FieldPattern(std::string_view spec) {
        if (spec.empty()) {
            return "{}";
        }
        return std::format("{{:{}}}", spec);
    }

    std::string FormatOne(const Placeholder& placeholder, std::string_view spec) {
        const std::string pattern = FieldPattern(spec);
        return std::visit([&pattern](const auto& value) { return std::vformat(pattern, std::make_format_args(value)); }, placeholder.value);
    }

    std::string PlaceholderList(const std::vector<Placeholder>& placeholders) {
        std::string names;
        for (const Placeholder& placeholder : placeholders) {
            if (!names.empty()) {
                names += ", ";
            }
            names += "{";
            names += placeholder.name;
            names += "}";
        }
        if (names.empty()) {
            return "none";
        }
        return names;
    }

    // Walks the format, giving literal text to onText and each {name:spec} field to onField, with an empty spec when
    // the field has none. Throws std::format_error for an unmatched brace.
    void WalkFormat(std::string_view format, const std::function<void(std::string_view)>& onText, const std::function<void(std::string_view, std::string_view)>& onField) {
        size_t index = 0;
        while (index < format.size()) {
            const char character = format[index];
            const bool doubled   = index + 1 < format.size() && format[index + 1] == character;
            if (character == '}') {
                if (!doubled) {
                    throw std::format_error(std::format("'}}' at {} has no '{{' before it, write '}}}}' for a brace", index));
                }
                onText("}");
                index += 2;
                continue;
            }
            if (character != '{') {
                onText(format.substr(index, 1));
                ++index;
                continue;
            }
            if (doubled) {
                onText("{");
                index += 2;
                continue;
            }
            const size_t close = format.find('}', index);
            if (close == std::string_view::npos) {
                throw std::format_error(std::format("'{{' at {} is never closed with '}}'", index));
            }
            const std::string_view inside = format.substr(index + 1, close - index - 1);
            const size_t           colon  = inside.find(':');
            if (colon == std::string_view::npos) {
                onField(inside, std::string_view());
            } else {
                onField(inside.substr(0, colon), inside.substr(colon + 1));
            }
            index = close + 1;
        }
    }

} // namespace

std::string FormatPlaceholders(std::string_view format, const std::vector<Placeholder>& placeholders) {
    std::string result;
    WalkFormat(
        format, [&result](std::string_view text) { result += text; },
        [&result, &placeholders](std::string_view name, std::string_view spec) {
            const auto found = std::ranges::find(placeholders, name, &Placeholder::name);
            if (found == placeholders.end()) {
                throw std::format_error(std::format("{{{}}} is not a value this format has", name));
            }
            result += FormatOne(*found, spec);
        });
    return result;
}

bool FormatNames(std::string_view format, std::string_view name) {
    bool names = false;
    WalkFormat(format, [](std::string_view) {}, [&names, name](std::string_view field, std::string_view) { names = names || field == name; });
    return names;
}

std::string FormatRate(double bytesPerSecond) {
    constexpr std::array<std::string_view, 4> kUnits{"B/s", "kB/s", "MB/s", "GB/s"};
    double                                    value = bytesPerSecond;
    size_t                                    unit  = 0;
    while (value >= 1000.0 && unit + 1 < kUnits.size()) {
        value /= 1000.0;
        ++unit;
    }
    if (unit == 0) {
        return std::format("{:.0f} {}", value, kUnits[unit]);
    }
    return std::format("{:.1f} {}", value, kUnits[unit]);
}

void RequirePlaceholders(std::string_view key, const std::string& format, const std::vector<Placeholder>& samples) {
    try {
        FormatPlaceholders(format, samples);
    } catch (const std::format_error& error) {
        throw ConfigError(std::format("{} '{}' is not a valid format: {}. It can use {}", key, format, error.what(), PlaceholderList(samples)));
    }
}

void RequireOptionalPlaceholders(std::string_view key, const std::optional<std::string>& format, const std::vector<Placeholder>& samples) {
    if (format.has_value()) {
        RequirePlaceholders(key, *format, samples);
    }
}

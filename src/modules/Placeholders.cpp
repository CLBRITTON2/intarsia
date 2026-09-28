#include "modules/Placeholders.hpp"

#include "config/Config.hpp"

#include <algorithm>
#include <format>

namespace {

    std::string FormatOne(const Placeholder& placeholder, std::string_view spec) {
        const std::string pattern = spec.empty() ? std::string("{}") : std::format("{{:{}}}", spec);
        return std::visit([&pattern](const auto& value) { return std::vformat(pattern, std::make_format_args(value)); }, placeholder.value);
    }

    std::string Names(const std::vector<Placeholder>& placeholders) {
        std::string names;
        for (const Placeholder& placeholder : placeholders) {
            names += names.empty() ? "{" : ", {";
            names += placeholder.name;
            names += "}";
        }
        return names.empty() ? std::string("none") : names;
    }

} // namespace

std::string FormatPlaceholders(std::string_view format, const std::vector<Placeholder>& placeholders) {
    std::string result;
    size_t      index = 0;
    while (index < format.size()) {
        const char character = format[index];
        const bool doubled   = index + 1 < format.size() && format[index + 1] == character;
        if (character == '}') {
            if (!doubled) {
                throw std::format_error(std::format("'}}' at {} has no '{{' before it, write '}}}}' for a brace", index));
            }
            result += '}';
            index += 2;
            continue;
        }
        if (character != '{') {
            result += character;
            ++index;
            continue;
        }
        if (doubled) {
            result += '{';
            index += 2;
            continue;
        }
        const size_t close = format.find('}', index);
        if (close == std::string_view::npos) {
            throw std::format_error(std::format("'{{' at {} is never closed with '}}'", index));
        }
        const std::string_view inside = format.substr(index + 1, close - index - 1);
        const size_t           colon  = inside.find(':');
        const std::string_view name   = inside.substr(0, colon);
        const auto             found  = std::ranges::find(placeholders, name, &Placeholder::name);
        if (found == placeholders.end()) {
            throw std::format_error(std::format("{{{}}} is not a value this format has", name));
        }
        result += FormatOne(*found, colon == std::string_view::npos ? std::string_view() : inside.substr(colon + 1));
        index = close + 1;
    }
    return result;
}

void RequirePlaceholders(std::string_view key, const std::string& format, const std::vector<Placeholder>& samples) {
    try {
        FormatPlaceholders(format, samples);
    } catch (const std::format_error& error) { throw ConfigError(std::format("{} '{}' is not a valid format: {}. It can use {}", key, format, error.what(), Names(samples))); }
}

void RequireOptionalPlaceholders(std::string_view key, const std::optional<std::string>& format, const std::vector<Placeholder>& samples) {
    if (format.has_value()) {
        RequirePlaceholders(key, *format, samples);
    }
}

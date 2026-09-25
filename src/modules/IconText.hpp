#pragma once

#include "config/Config.hpp"
#include "render/RenderContext.hpp"

#include <wrl/client.h>

#include <format>
#include <string>
#include <string_view>

struct IconText {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    D2D1_SIZE_F                               size;
};

// The icon in the accent color at the icon size, two spaces, then the text drawn with textBrush. Just the text when
// there is no icon.
IconText LayOutIconText(const RenderContext& render, std::wstring_view icon, std::wstring_view text, ID2D1Brush* textBrush);

template <typename Value>
std::string FormatValue(const std::string& format, Value value) {
    return std::vformat(format, std::make_format_args(value));
}

// Formats a sample value once so a bad spec fails when the config loads, not on the first redraw. Throws ConfigError
// naming the key.
template <typename Value>
void RequireFormat(std::string_view key, const std::string& format, Value sample) {
    try {
        FormatValue(format, sample);
    } catch (const std::format_error& error) { throw ConfigError(std::format("{} '{}' is not a valid std::format spec for its value: {}", key, format, error.what())); }
}

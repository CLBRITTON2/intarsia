#pragma once

#include "config/Config.hpp"
#include "render/RenderContext.hpp"

#include <wrl/client.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>

struct IconText {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
    D2D1_SIZE_F                               size;
};

// The icon in the accent color at the icon size, two spaces, then the text drawn with textBrush. Just the text when
// there is no icon.
IconText LayOutIconText(const RenderContext& render, std::wstring_view icon, std::wstring_view text, ID2D1Brush* textBrush);

// One line of text, laid out again only when the text changes. The brush is the bar's and stays the same.
class TextLine {
  public:
    // Whether the text differs from the last update's.
    bool        Update(const RenderContext& render, std::wstring text, ID2D1Brush* brush);
    D2D1_SIZE_F Size() const;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const;

  private:
    std::wstring _shown; // what _text holds, once it has a layout
    IconText     _text{.layout = nullptr, .size = {.width = 0.0f, .height = 0.0f}};
};

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

// RequireFormat for a module's format and, when it has one, its tooltip. module names the type, such as "cpu".
template <typename Value>
void RequireFormats(std::string_view module, const std::string& format, const std::optional<std::string>& tooltip, Value sample) {
    RequireFormat(std::format("{} format", module), format, sample);
    if (tooltip.has_value()) {
        RequireFormat(std::format("{} tooltip", module), *tooltip, sample);
    }
}

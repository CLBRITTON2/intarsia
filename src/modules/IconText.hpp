#pragma once

#include "config/Config.hpp"
#include "render/RenderContext.hpp"

#include <wrl/client.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>

// An icon and a line of text beside it, each laid out on its own so the gap between them is a set distance.
struct IconText {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> icon; // null when there is no icon
    Microsoft::WRL::ComPtr<IDWriteTextLayout> text; // null when there is no text
    D2D1_SIZE_F                               iconSize;
    D2D1_SIZE_F                               textSize;
    float                                     gapDips; // 0 unless there are both
    D2D1_SIZE_F                               size;    // both, and the gap
};

// The icon in the icon font and size, the icon gap, then the text. Layout only measures: colors are chosen at draw, so
// laying out needs no device.
IconText LayOutIconText(const RenderContext& render, std::wstring_view icon, std::wstring_view text);

// The text alone in weight, as an item's font-weight sets it.
IconText LayOutWeightedText(const RenderContext& render, std::wstring_view text, DWRITE_FONT_WEIGHT weight);

// Draws the icon in render.accent then the text in textBrush from origin, each centered across the line.
void DrawIconText(const RenderContext& render, D2D1_POINT_2F origin, const IconText& laidOut, ID2D1Brush* textBrush);

// Nothing laid out yet.
IconText EmptyIconText();

// One line of text in render.foreground, laid out again only when the text changes.
class TextLine {
  public:
    // Whether the text differs from the last update's.
    bool        Update(const RenderContext& render, std::wstring text);
    D2D1_SIZE_F Size() const;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const;

  private:
    std::wstring _shown; // what _text holds, once laid out
    bool         _laidOut = false;
    IconText     _text    = EmptyIconText();
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

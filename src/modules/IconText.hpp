#pragma once

#include "render/RenderContext.hpp"

#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

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

// The icon for a level from 0 to 100 (a signal, a charge): icons split the range evenly, lowest first. A level outside
// the range takes the nearest end.
const std::wstring& LevelIcon(const std::vector<std::wstring>& icons, std::int32_t percent);

// What a module shows as one line: its icon, the text after it, either of them empty.
struct ModuleText {
    std::wstring icon;
    std::wstring text;
    bool         alert; // drawn in the alert color, for a value that needs attention such as a failed command

    bool         operator==(const ModuleText&) const = default;
};

// A ModuleText, laid out again only when it changes. The text draws in render.alert for an alert, else in
// render.foreground.
class TextLine {
  public:
    // Whether content differs from the last update's.
    bool        Update(const RenderContext& render, ModuleText content);
    D2D1_SIZE_F Size() const;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const;

  private:
    ModuleText _shown{.icon = {}, .text = {}, .alert = false}; // what _text holds, once laid out
    bool       _laidOut = false;
    IconText   _text    = EmptyIconText();
};

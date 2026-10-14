#pragma once

#include "render/RenderContext.hpp"

#include <wrl/client.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// An icon and text after it, each laid out on its own so the gap between them is a set distance. Along a horizontal bar
// the text is one line beside the icon. Down a vertical bar it is under the icon, each centered across, its lines
// centered and cut with an ellipsis at the room across the bar.
struct IconText {
    Microsoft::WRL::ComPtr<IDWriteTextLayout> icon; // null when there is no icon
    Microsoft::WRL::ComPtr<IDWriteTextLayout> text; // null when there is no text
    Axis                                      axis;
    D2D1_SIZE_F                               iconSize;
    float                                     iconInkShiftDips; // moves the icon's ink onto its line's center, down when positive
    D2D1_SIZE_F                               textSize;
    float                                     textLeftDips; // where the text's lines start in its layout, which centers them in the room
    float                                     gapDips;      // 0 unless there are both
    D2D1_SIZE_F                               size;         // both, and the gap
};

// The icon in the icon font and size, the icon gap, then the text. Layout only measures: colors are chosen at draw, so
// laying out needs no device.
IconText LayOutIconText(const RenderContext& render, std::wstring_view icon, std::wstring_view text);

// The text alone in weight, as an item's font-weight sets it, insetDips in from each side of the room across a
// vertical bar, as an item's padding and margin take.
IconText LayOutWeightedText(const RenderContext& render, std::wstring_view text, DWRITE_FONT_WEIGHT weight, float insetDips);

// Draws the icon in iconBrush then the text in textBrush from origin, each centered across the bar: the text by its
// line box, the icon by its ink, since glyphs sit anywhere in their em (a text arrow near the baseline, a Nerd Font icon
// on the em's center).
void DrawIconText(const RenderContext& render, D2D1_POINT_2F origin, const IconText& laidOut, ID2D1Brush* iconBrush, ID2D1Brush* textBrush);

// DrawIconText turned clockwise by degrees about the laid out box's center, as a fold's toggle turns.
void DrawTurnedIconText(const RenderContext& render, D2D1_POINT_2F origin, const IconText& laidOut, float degrees, ID2D1Brush* iconBrush, ID2D1Brush* textBrush);

// Nothing laid out yet.
IconText EmptyIconText();

// The icon for a level from 0 to 100 (a signal, a charge): icons split the range evenly, lowest first. A level outside
// the range takes the nearest end.
const std::wstring& LevelIcon(const std::vector<std::wstring>& icons, std::int32_t percent);

// Which of a module's colors a line draws in.
enum class TextState : std::uint8_t {
    Normal,
    Alert,    // alert text, for a value that needs attention such as a failed command
    Warning,  // warning text and icon, a battery running low
    Critical, // critical text and icon, a battery nearly empty
    Muted,    // muted text and icon, a muted audio output
    Classed   // a custom module's declared class, by ModuleText::classIndex
};

// What a module shows as one line: its icon, the text after it, either of them empty.
struct ModuleText {
    std::wstring icon;
    std::wstring text;
    TextState    state;
    size_t       classIndex; // Classed: which of the module's declared classes, in their order, else 0

    bool         operator==(const ModuleText&) const = default;
};

ModuleText NormalText(std::wstring icon, std::wstring text);

// A ModuleText, laid out again only when it changes, drawn in its state's colors.
class TextLine {
  public:
    // Whether content differs from the last update's.
    bool        Update(const RenderContext& render, ModuleText content);
    D2D1_SIZE_F Size() const;
    void        Draw(const RenderContext& render, D2D1_POINT_2F origin) const;

  private:
    ModuleText _shown   = NormalText(L"", L""); // what _text holds, once laid out
    bool       _laidOut = false;
    IconText   _text    = EmptyIconText();
};

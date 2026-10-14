#pragma once

#include "modules/IconText.hpp"
#include "render/RenderContext.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

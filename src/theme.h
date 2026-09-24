#pragma once

#include <d2d1.h>

namespace theme {

    inline constexpr float kThicknessDips = 54.0f;
    inline constexpr float kTextSizeDips  = 15.0f;
    inline constexpr float kIconSizeDips  = 18.0f;
    inline constexpr float kBorderDips    = 1.0f;

    // Propo keeps icon glyphs at their natural width, the plain Nerd Font squeezes them into one cell.
    inline constexpr wchar_t      kFontFamily[] = L"JetBrainsMono Nerd Font Propo";

    inline constexpr D2D1_COLOR_F kBackground{25.0f / 255, 23.0f / 255, 36.0f / 255, 0.9f};
    inline constexpr D2D1_COLOR_F kBorder{110.0f / 255, 106.0f / 255, 134.0f / 255, 0.25f};
    inline constexpr D2D1_COLOR_F kForeground{224.0f / 255, 222.0f / 255, 244.0f / 255, 1.0f};
    inline constexpr D2D1_COLOR_F kAccentAlt{196.0f / 255, 167.0f / 255, 231.0f / 255, 1.0f};

} // namespace theme

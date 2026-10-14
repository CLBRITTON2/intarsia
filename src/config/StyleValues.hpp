#pragma once

#include "config/StyleConfig.hpp"

#include <d2d1.h>
#include <dwrite.h>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// The grammar of style.conf's property values. Each Parse function throws std::invalid_argument with what the value
// should look like, and its caller adds where the value is.

inline constexpr D2D1_COLOR_F kTransparent{
    .r = 0.0f,
    .g = 0.0f,
    .b = 0.0f,
    .a = 0.0f,
};
inline constexpr Transition kNoTransition{.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease};
inline constexpr BorderSide kNoBorder{.widthDips = 0.0f, .color = kTransparent};

// A transition as written: the property it animates, then the transition.
struct TransitionValue {
    std::string_view property;
    Transition       transition;
};

// text without the spaces, tabs and carriage returns around it.
std::string_view Trim(std::string_view text);

float            ParseSignedNumber(std::string_view text);
float            ParseLength(std::string_view text); // 0 or more
float            ParsePositiveLength(std::string_view text);

// A positive length read as the optional it caps, for a property that has no cap when left out.
std::optional<float> ParseCap(std::string_view text);

// One number for every side, or two: above and below, then left and right.
Spacing ParseSpacing(std::string_view text);

// A hex color, optionally followed by an opacity percentage that multiplies its alpha, or transparent.
D2D1_COLOR_F       ParseColor(std::string_view text);

std::wstring       ParseFamily(std::string_view text);
DWRITE_FONT_WEIGHT ParseWeight(std::string_view text);
bool               ParseSwitch(std::string_view text); // on or off

// none, or transitions separated by commas, each a property, a duration and an optional timing function (ease when
// left out). none animates nothing.
std::vector<TransitionValue> ParseTransitionValues(std::string_view text);

// The transition a list gives property, the last one naming it, or none.
Transition ParseTransition(std::string_view text, std::string_view property);

// none, or shadows separated by commas, each numbers then a color.
std::vector<InsetShadow> ParseShadows(std::string_view text);

// none, or a width then a color.
BorderSide ParseBorderSide(std::string_view text);

// Checks value as what key is read as. A key means the same in every block that takes it.
void CheckValue(std::string_view key, std::string_view value);

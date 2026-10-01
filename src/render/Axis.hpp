#pragma once

#include "config/Config.hpp"

#include <d2d1.h>

#include <cstdint>

// The way a bar runs: along a top or bottom edge, or down a left or right one. Main is along the bar, cross across it.
enum class Axis : std::uint8_t {
    Horizontal,
    Vertical
};

Axis  AxisOf(Edge edge);

float MainLength(D2D1_SIZE_F size, Axis axis);
float CrossLength(D2D1_SIZE_F size, Axis axis);
// Spacing is on screen axes, as CSS writes it: along a horizontal bar is x, along a vertical one is y.
float MainOf(const Spacing& spacing, Axis axis);
float CrossOf(const Spacing& spacing, Axis axis);
float MainOf(D2D1_POINT_2F point, Axis axis);
// Where a rectangle starts and ends along the main axis.
float         MainStart(const D2D1_RECT_F& rect, Axis axis);
float         MainEnd(const D2D1_RECT_F& rect, Axis axis);
D2D1_POINT_2F MovedAlong(D2D1_POINT_2F point, float distance, Axis axis);
D2D1_SIZE_F   MainCrossSize(Axis axis, float main, float cross);
// A rectangle from a span along the main axis and a span across it.
D2D1_RECT_F MainCrossRect(Axis axis, float mainStart, float mainLength, float crossStart, float crossLength);

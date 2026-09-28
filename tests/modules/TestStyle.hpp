#pragma once

#include "config/Config.hpp"

// An item in a test style: padded 4 across and 2 down, 5 of margin across on each side, so two items are 10 apart.
inline ItemStyle TestItemStyle(std::uint32_t color, std::uint32_t background) {
    return ItemStyle{.color      = D2D1::ColorF(color),
                     .background = D2D1::ColorF(background),
                     .weight     = DWRITE_FONT_WEIGHT_NORMAL,
                     .padding    = {.yDips = 2.0f, .xDips = 4.0f},
                     .margin     = {.yDips = 0.0f, .xDips = 5.0f},
                     .radiusDips      = 6.0f,
                     .hoverColor      = D2D1::ColorF(0xFFFFFF),
                     .hoverBackground = D2D1::ColorF(0x404040)};
}

// A distinct color per state, so a test can tell which one applies.
inline ModuleStyle TestModuleStyle() {
    return ModuleStyle{
        .box = {.background = D2D1::ColorF(0, 0.0f), .borderColor = D2D1::ColorF(0, 0.0f), .borderWidthDips = 0.0f, .radiusDips = 0.0f, .padding = {.yDips = 0.0f, .xDips = 0.0f}},
        .hover         = D2D1::ColorF(0, 0.0f),
        .color         = D2D1::ColorF(0xFFFFFF),
        .alertColor    = D2D1::ColorF(0xFF0000),
        .icon          = {.font = L"Segoe UI", .sizeDips = 20.0f, .color = D2D1::ColorF(0xFFFFFF), .gapDips = 4.0f},
        .item          = TestItemStyle(0x000001, 0x000011),
        .focused       = TestItemStyle(0x000002, 0x000022),
        .occupied      = TestItemStyle(0x000003, 0x000033),
        .empty         = TestItemStyle(0x000004, 0x000044),
        .lineWidthDips = 1.0f,
        .lineHeight    = 0.5f};
}

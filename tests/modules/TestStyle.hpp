#pragma once

#include "config/Config.hpp"
#include "modules/IModule.hpp"

#include <chrono>
#include <string>
#include <utility>

// An item in a test style: padded 4 across and 2 down, 5 of margin across on each side, so two items are 10 apart.
inline ItemStyle TestItemStyle(std::uint32_t color, std::uint32_t background) {
    return ItemStyle{
        .color                = D2D1::ColorF(color),
        .background           = D2D1::ColorF(background),
        .weight               = DWRITE_FONT_WEIGHT_NORMAL,
        .padding              = {.yDips = 2.0f, .xDips = 4.0f},
        .margin               = {.yDips = 0.0f, .xDips = 5.0f},
        .radiusDips           = 6.0f,
        .shadows              = {},
        .hoverColor           = D2D1::ColorF(0xFFFFFF),
        .hoverBackground      = D2D1::ColorF(0x404040),
        .backgroundTransition = {.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease},
        .colorTransition      = {.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease},
    };
}

// A distinct color per state, so a test can tell which one applies.
inline ModuleStyle TestModuleStyle() {
    return ModuleStyle{
        .box =
            {
                .background = D2D1::ColorF(0, 0.0f),
                .border     = {},
                .radiusDips = 0.0f,
                .padding    = {.yDips = 0.0f, .xDips = 0.0f},
                .shadows    = {},
            },
        .margin               = {.yDips = 0.0f, .xDips = 0.0f},
        .hover                = D2D1::ColorF(0, 0.0f),
        .backgroundTransition = {.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease},
        .color                = D2D1::ColorF(0xFFFFFF),
        .alertColor           = D2D1::ColorF(0xFF0000),
        .warning              = {.text = D2D1::ColorF(0xFFFF00), .icon = D2D1::ColorF(0xFFFF01)},
        .critical             = {.text = D2D1::ColorF(0xFF0001), .icon = D2D1::ColorF(0xFF0002)},
        .muted                = {.text = D2D1::ColorF(0x808080), .icon = D2D1::ColorF(0x808081)},
        .classes              = {},
        .icon =
            {
                .font            = L"Segoe UI",
                .sizeDips        = 20.0f,
                .color           = D2D1::ColorF(0xFFFFFF),
                .gapDips         = 4.0f,
                .rotationDegrees = 0.0f,
                .transition      = {.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease},
            },
        .item                        = TestItemStyle(0x000001, 0x000011),
        .focused                     = TestItemStyle(0x000002, 0x000022),
        .occupied                    = TestItemStyle(0x000003, 0x000033),
        .empty                       = TestItemStyle(0x000004, 0x000044),
        .toggle                      = TestItemStyle(0x000005, 0x000055),
        .transition                  = {.duration = std::chrono::milliseconds(0), .timing = TimingFunction::Ease},
        .expandedIconRotationDegrees = 180.0f,
    };
}

// A text module config showing format every second, with no tooltip, icon or actions.
inline TextModuleConfig TextConfigWithFormat(std::string format) {
    return TextModuleConfig{
        .format   = std::move(format),
        .tooltip  = std::nullopt,
        .icon     = L"",
        .interval = std::chrono::seconds(1),
        .actions  = kNoActions,
    };
}

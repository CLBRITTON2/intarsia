#include "Harness.hpp"

#include <gtest/gtest.h>

#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>

namespace {

    constexpr std::uint32_t kProbeColor   = 0x27AE60;
    constexpr std::uint32_t kRulerColor   = 0xF39C12;
    constexpr std::uint32_t kTooltipColor = 0x3498DB;
    constexpr auto          kLimit        = std::chrono::seconds(5);

    // The ruler prints the probe's tooltip text in the bar, so the two spans can be compared. The probe sits in the
    // center, so a tooltip centered on it needs no pushing back onto the monitor.
    constexpr std::string_view kConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40 },
    window_manager = "glazewm",
    modules_left = { "ruler" },
    modules_center = { "probe" },
    modules_right = {},
    layouts = {},
    modules = {
        ruler = { type = "clock", format = "WWWW", icon = "", interval = 1 },
        probe = { type = "clock", format = "W", icon = "", interval = 1, tooltip = "WWWW" },
    },
}
)";

    // Large text, so glyph stems cover whole pixels in the exact color.
    constexpr std::string_view kStyle = R"(
* { font-family: "Segoe UI"; font-size: 24px; color: #e0def4; }
section { margin: 4px; padding: 3px; background: #191724; }
#ruler { color: #f39c12; }
#probe { color: #27ae60; }
tooltip { color: #3498db; background: #191724; padding: 4px; }
)";

} // namespace

TEST(Tooltip, OpensUnderTheModuleWithItsTextAndClosesWhenTheCursorMovesOff) {
    integration::RunningBar     bar("tooltip", kConfig, kStyle);
    std::optional<double>       probeColumn;
    std::optional<std::int32_t> rulerSpan;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            const integration::Capture capture = integration::CaptureScreenOver(bar.PrimaryBar());
            probeColumn                        = integration::MeanColumn(capture, kProbeColor);
            rulerSpan                          = integration::ColumnSpan(capture, kRulerColor);
            return probeColumn.has_value() && rulerSpan.has_value();
        },
        kLimit))
        << "the modules never drew in their colors";
    const auto                     probeX = static_cast<std::int32_t>(std::lround(probeColumn.value_or(0)));

    const integration::SavedCursor saved;
    bar.MoveCursorTo(probeX);
    std::optional<std::int32_t> tooltipSpan;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            HWND tooltip = bar.Tooltip();
            tooltipSpan  = tooltip == nullptr ? std::nullopt : integration::ColumnSpan(integration::CaptureScreenOver(tooltip), kTooltipColor);
            return tooltipSpan.has_value();
        },
        kLimit))
        << "resting over the module never showed its tooltip text";
    const RECT barRect     = integration::PhysicalWindowRect(bar.PrimaryBar());
    const RECT tooltipRect = integration::PhysicalWindowRect(bar.Tooltip());
    const auto tooltipMid  = (tooltipRect.left + tooltipRect.right) / 2;
    integration::Log("tooltip_shown",
                     std::format("rect=({},{},{},{}) bar_bottom={} probe_x={} text_columns={} ruler_columns={}", tooltipRect.left, tooltipRect.top, tooltipRect.right,
                                 tooltipRect.bottom, barRect.bottom, barRect.left + probeX, tooltipSpan.value_or(-1), rulerSpan.value_or(-1)));
    EXPECT_EQ(tooltipRect.top, barRect.bottom) << "the tooltip is not against the bar's desktop side";
    EXPECT_NEAR(tooltipMid, barRect.left + probeX, 4) << "the tooltip is not centered on the module";
    // The same text in the same font spans the same columns, give or take antialiasing at its ends.
    EXPECT_NEAR(tooltipSpan.value_or(0), rulerSpan.value_or(0), 2) << "the tooltip does not show the module's tooltip text";

    // The right section is empty, so its end has no module under it.
    bar.MoveCursorTo(barRect.right - barRect.left - 2);
    EXPECT_TRUE(integration::Eventually([&] { return bar.Tooltip() == nullptr; }, kLimit)) << "moving off the module left its tooltip shown";
}

#include "Harness.hpp"

#include "Capture.hpp"

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

    // The ruler prints the probe's tooltip text in the bar, so the two spans can be compared.
    constexpr std::string_view kConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
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
bar {
  font: Segoe UI
  size: 24
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
ruler {
  color: #f39c12
}
probe {
  color: #27ae60
}
tooltip {
  color: #3498db
  background: #191724
  padding: 4
}
)";

} // namespace

TEST(Tooltip, OpensWithTheModulesTextAndClosesWhenTheCursorMovesOff) {
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
            if (tooltip == nullptr) {
                return false;
            }
            tooltipSpan = integration::ColumnSpan(integration::CaptureScreenOver(tooltip), kTooltipColor);
            return tooltipSpan.has_value();
        },
        kLimit))
        << "resting over the module never showed its tooltip text";
    integration::Log("tooltip_shown", std::format("text_columns={} ruler_columns={}", tooltipSpan.value_or(-1), rulerSpan.value_or(-1)));
    // The same text in the same font spans the same columns, give or take antialiasing at its ends.
    EXPECT_NEAR(tooltipSpan.value_or(0), rulerSpan.value_or(0), 2) << "the tooltip does not show the module's tooltip text";

    bar.MoveCursorTo(bar.OffsetPastLastModule());
    EXPECT_TRUE(integration::Eventually([&] { return bar.Tooltip() == nullptr; }, kLimit)) << "moving off the module left its tooltip shown";
}

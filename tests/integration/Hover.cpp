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

    constexpr std::uint32_t    kRestColor    = 0x27AE60;
    constexpr std::uint32_t    kHoverColor   = 0x2E86C1;
    constexpr std::uint32_t    kProbeColor   = 0x27AE60;
    constexpr std::uint32_t    kRulerColor   = 0xF39C12;
    constexpr std::uint32_t    kTooltipColor = 0x3498DB;
    constexpr auto             kLimit        = std::chrono::seconds(5);

    constexpr std::string_view kModuleConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
    window_manager = "glazewm",
    modules_left = { "probe" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { probe = { type = "clock", format = "WWWW", icon = "", interval = 60 } },
}
)";

    // The transition makes the hover an animation, which logs its frames when it ends.
    constexpr std::string_view kModuleStyle = R"(
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
module {
  transition: background 200ms
}
probe {
  background: #27ae60
  hover {
    background: #2e86c1
  }
}
)";

    constexpr std::string_view kItemConfig = R"(
return {
    bar = { edge = "top", layer = "top", space = "share", thickness = 40, monitors = { "*" } },
    window_manager = "glazewm",
    modules_left = { "apps" },
    modules_center = {},
    modules_right = {},
    layouts = {},
    modules = { apps = { type = "windows" } },
}
)";

    constexpr std::string_view kItemStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
section {
  margin: 4
  padding: 3
  background: #191724
}
module {
  item {
    padding: 4 8
    margin: 0 2
    background: #27ae60
    transition: background 200ms
    hover {
      background: #2e86c1
    }
  }
}
)";

    // The ruler prints the probe's tooltip text in the bar, so the two spans can be compared.
    constexpr std::string_view kTooltipConfig = R"(
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
    constexpr std::string_view kTooltipStyle = R"(
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

TEST(Hover, AnimatesAModulesHoverBackgroundInAndOut) {
    integration::RunningBar           bar("hover-module", kModuleConfig, kModuleStyle);
    const std::optional<std::int32_t> probe = bar.DrawnColumn(kRestColor, kLimit);
    if (!probe.has_value()) {
        FAIL() << "the module never drew its background at rest";
    }
    EXPECT_EQ(bar.PixelCount(kHoverColor), size_t{0}) << "the hover background drew with no cursor over the module";

    const integration::SavedCursor saved;
    bar.MoveCursorTo(*probe);
    EXPECT_TRUE(bar.Draws(kHoverColor, kLimit)) << "the hover background never drew under the cursor";
    EXPECT_TRUE(integration::Eventually([&] { return !bar.Events("animation_frames").empty(); }, kLimit)) << "the hover transition logged no frames";

    bar.MoveCursorTo(bar.OffsetPastLastModule());
    EXPECT_TRUE(bar.StopsDrawing(kHoverColor, kLimit)) << "the hover background still drew after the cursor moved off";
    EXPECT_TRUE(bar.Draws(kRestColor, kLimit)) << "the rest background never came back";
}

TEST(Hover, DrawsTheHoverBackgroundOfTheItemUnderTheCursorOnly) {
    const integration::TestWindow first(L" 1");
    const integration::TestWindow second(L" 2");
    integration::RunningBar       bar("hover-item", kItemConfig, kItemStyle);
    std::optional<double>         middle;
    std::optional<std::int32_t>   span;
    ASSERT_TRUE(integration::Eventually(
        [&] {
            const integration::Capture capture = integration::CaptureScreenOver(bar.PrimaryBar());
            middle                             = integration::MeanColumn(capture, kRestColor);
            span                               = integration::ColumnSpan(capture, kRestColor);
            return middle.has_value() && span.has_value();
        },
        kLimit))
        << "the window items never drew their background at rest";

    const integration::SavedCursor saved;
    // Two items of one size: the first one's middle lies a quarter of their span before the middle of both.
    bar.MoveCursorTo(static_cast<std::int32_t>(std::lround(middle.value_or(0) - (static_cast<double>(span.value_or(0)) / 4))));
    std::optional<double> hovered;
    std::optional<double> rest;
    EXPECT_TRUE(integration::Eventually(
        [&] {
            const integration::Capture capture = integration::CaptureScreenOver(bar.PrimaryBar());
            hovered                            = integration::MeanColumn(capture, kHoverColor);
            rest                               = integration::MeanColumn(capture, kRestColor);
            return hovered.has_value() && rest.has_value() && *hovered < *rest;
        },
        kLimit))
        << "the first item did not draw the hover background alone: hovered column " << hovered.value_or(-1) << ", rest column " << rest.value_or(-1);
    integration::Log("hover_columns", std::format("hovered={:.1f} rest={:.1f}", hovered.value_or(-1), rest.value_or(-1)));

    bar.MoveCursorTo(bar.OffsetPastLastModule());
    EXPECT_TRUE(bar.StopsDrawing(kHoverColor, kLimit)) << "the item's hover background still drew after the cursor moved off";
}

TEST(Tooltip, OpensWithTheModulesTextAndClosesWhenTheCursorMovesOff) {
    integration::RunningBar     bar("tooltip", kTooltipConfig, kTooltipStyle);
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

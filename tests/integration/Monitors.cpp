#include "Harness.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>
#include <vector>

using testing::HasSubstr;

namespace {

    constexpr auto kReloaded = std::chrono::seconds(5);

    // monitors is the Lua list contents of bar.monitors.
    std::string MonitorsConfig(std::string_view monitors) {
        return std::format(R"(
return {{
    bar = {{ edge = "top", layer = "top", space = "share", thickness = 40, monitors = {{ {} }} }},
    window_manager = "glazewm",
    modules_left = {{ "mark" }},
    modules_center = {{}},
    modules_right = {{}},
    layouts = {{}},
    modules = {{ mark = {{ type = "clock", format = "WWWW", icon = "", interval = 60 }} }},
}}
)",
                           monitors);
    }

    constexpr std::string_view kStyle = R"(
bar {
  font: Segoe UI
  size: 13
  color: #e0def4
}
mark {
  background: #27ae60
}
)";

    // The monitor lines logged after the first skipped ones, once there are at least count of them.
    std::vector<std::string> MonitorLinesAfter(const integration::RunningBar& bar, size_t skipped, size_t count) {
        std::vector<std::string> lines;
        integration::Eventually(
            [&] {
                const std::vector<std::string> all = bar.Events("monitor");
                lines.assign(all.begin() + static_cast<std::ptrdiff_t>(std::min(skipped, all.size())), all.end());
                return lines.size() >= count;
            },
            kReloaded);
        return lines;
    }

} // namespace

TEST(Monitors, KeepsRunningWithNoBarWhenNoMonitorMatches) {
    const integration::RunningBar bar("monitors-none", MonitorsConfig(R"("*")"), kStyle);
    const size_t                  startLines = bar.Events("monitor").size();
    ASSERT_GT(startLines, size_t{0}) << "the bar logged no monitor line at startup";
    integration::Log("monitor_lines", std::format("phase=start lines={}", startLines));

    // An id no monitor has, in the shape IsMonitorId accepts.
    bar.SaveConfig(MonitorsConfig(R"("ZZZ0000")"));
    const std::vector<std::string> unmatched = MonitorLinesAfter(bar, startLines, startLines);
    integration::Log("monitor_lines", std::format("phase=unmatched lines={}", unmatched.size()));
    ASSERT_GE(unmatched.size(), startLines) << "the reload logged fewer monitor lines than startup";
    for (const std::string& line : unmatched) {
        EXPECT_THAT(line, HasSubstr(" bar=no"));
    }
    EXPECT_TRUE(integration::Eventually([&] { return bar.PrimaryBar() == nullptr; }, kReloaded)) << "the primary bar stayed although no monitor matches";
    // Long enough for a failure after the rebuild to reach the fatal box.
    integration::PumpFor(std::chrono::seconds(2));
    EXPECT_EQ(bar.PrimaryBar(), nullptr) << "a bar came back although no monitor matches";
    EXPECT_THAT(bar.Events("fatal"), testing::IsEmpty());

    const size_t unmatchedEnd = bar.Events("monitor").size();
    bar.SaveConfig(MonitorsConfig(R"("*")"));
    EXPECT_TRUE(integration::Eventually([&] { return bar.PrimaryBar() != nullptr; }, kReloaded)) << "the bar did not return once every monitor matched again";
    const std::vector<std::string> matched = MonitorLinesAfter(bar, unmatchedEnd, startLines);
    integration::Log("monitor_lines", std::format("phase=matched lines={}", matched.size()));
    EXPECT_THAT(matched, testing::Contains(HasSubstr(" bar=yes")));
}

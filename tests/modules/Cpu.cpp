#include "modules/Cpu.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

    TextModuleConfig CpuWithFormat(std::string format) {
        return TextModuleConfig{.format = std::move(format), .tooltip = std::nullopt, .icon = L"", .interval = std::chrono::seconds(1), .actions = {}};
    }

} // namespace

TEST(Cpu, UsageIsTheBusyShareOfKernelPlusUserTime) {
    // 400 ticks elapsed (kernel 300 of which idle 100, user 100): 300 busy of 400 is 75%.
    const CpuTimes before{.idle = 1000, .kernel = 5000, .user = 2000};
    const CpuTimes after{.idle = 1100, .kernel = 5300, .user = 2100};
    EXPECT_EQ(CpuUsagePercent(before, after), 75);
}

TEST(Cpu, UsageRoundsToTheNearestPercent) {
    const CpuTimes before{.idle = 0, .kernel = 0, .user = 0};
    EXPECT_EQ(CpuUsagePercent(before, CpuTimes{.idle = 1, .kernel = 1, .user = 2}), 67);
}

TEST(Cpu, UsageIsZeroWhenNoTimePassed) {
    const CpuTimes same{.idle = 10, .kernel = 20, .user = 30};
    EXPECT_EQ(CpuUsagePercent(same, same), 0);
}

TEST(Cpu, IdleBeyondTotalIsAnError) {
    const CpuTimes before{.idle = 0, .kernel = 0, .user = 0};
    EXPECT_THROW(CpuUsagePercent(before, CpuTimes{.idle = 10, .kernel = 5, .user = 0}), std::runtime_error);
}

TEST(Cpu, GigahertzIsTheBaseFrequencyScaledByPerformance) {
    EXPECT_DOUBLE_EQ(CpuGigahertz(2400.0, 150.0), 3.6);
    EXPECT_DOUBLE_EQ(CpuGigahertz(2400.0, 0.0), 0.0);
}

TEST(Cpu, TheTooltipCanNameTheClockSpeedAndTheBusiestCore) {
    EXPECT_EQ(FormatPlaceholders("{usage}% {ghz:.2f} GHz busiest {max_core}%", CpuPlaceholders(CpuSample{.usagePercent = 12, .gigahertz = 3.6, .maxCorePercent = 71, .cores = {}})),
              "12% 3.60 GHz busiest 71%");
}

TEST(Cpu, CoreUsageListsEveryCoreInOrderFourToALine) {
    const std::vector<CounterInstance> instances{{.name = L"10", .value = 5.4}, {.name = L"_Total", .value = 30.0}, {.name = L"0", .value = 12.2},  {.name = L"1", .value = 99.6},
                                                 {.name = L"2", .value = 0.0},  {.name = L"3", .value = 50.0},      {.name = L"4", .value = 100.0}, {.name = L"", .value = 1.0}};
    EXPECT_EQ(FormatCoreUsage(instances), " 0:  12%   1: 100%   2:   0%   3:  50%\n 4: 100%  10:   5%");
    EXPECT_EQ(FormatCoreUsage({}), "");
}

TEST(Cpu, CountersAreSampledOnlyWhenNamed) {
    TextModuleConfig config = CpuWithFormat("{usage}%");
    config.tooltip          = "{ghz:.1f} GHz, busiest core {max_core}%";
    Cpu module(std::move(config));
    // The tooltip reads the counters' first values after one interval.
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const std::optional<std::wstring> tooltip = module.TooltipText();
    EXPECT_TRUE(tooltip.value_or(L"").ends_with(L"%"));
    EXPECT_NE(tooltip.value_or(L"").find(L" GHz, busiest core "), std::wstring::npos);
}

TEST(Cpu, FormatMustRenderAnInteger) {
    EXPECT_THROW(Cpu(CpuWithFormat("{usage:.1f}%")), ConfigError);
    EXPECT_NO_THROW(Cpu(CpuWithFormat("{usage}%")));
}

TEST(Cpu, TooltipIsCheckedLikeTheFormat) {
    TextModuleConfig config = CpuWithFormat("{usage}%");
    config.tooltip          = "{usage:.1f}";
    EXPECT_THROW(Cpu(std::move(config)), ConfigError);
}

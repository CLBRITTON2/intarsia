#include "modules/Cpu.hpp"

#include "TestStyle.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <string>
#include <utility>
#include <vector>

TEST(Cpu, UsageIsTheBusyShareOfKernelPlusUserTime) {
    // 400 ticks elapsed (kernel 300 of which idle 100, user 100): 300 busy of 400 is 75%.
    const CpuTimes before{
        .idle   = 1000,
        .kernel = 5000,
        .user   = 2000,
    };
    const CpuTimes after{
        .idle   = 1100,
        .kernel = 5300,
        .user   = 2100,
    };
    EXPECT_EQ(CpuUsagePercent(before, after), 75);
    // 2 busy of 3 rounds to the nearest percent.
    EXPECT_EQ(CpuUsagePercent(
                  CpuTimes{
                      .idle   = 0,
                      .kernel = 0,
                      .user   = 0,
                  },
                  CpuTimes{
                      .idle   = 1,
                      .kernel = 1,
                      .user   = 2,
                  }),
              67);
}

TEST(Cpu, TheTooltipCanNameTheClockSpeedAndTheBusiestCore) {
    EXPECT_EQ(FormatPlaceholders("{usage}% {ghz:.2f} GHz busiest {max_core}%",
                                 CpuPlaceholders(CpuSample{
                                     .usagePercent   = 12,
                                     .gigahertz      = 3.6,
                                     .maxCorePercent = 71,
                                     .cores          = {},
                                 })),
              L"12% 3.60 GHz busiest 71%");
}

TEST(Cpu, CoreUsageListsEveryCoreInOrderFourToALine) {
    const std::vector<CounterInstance> instances{{.name = L"10", .value = 5.4}, {.name = L"_Total", .value = 30.0}, {.name = L"0", .value = 12.2}, {.name = L"1", .value = 99.6},
                                                 {.name = L"2", .value = 0.0},  {.name = L"3", .value = 50.0},      {.name = L"4", .value = 100.0}};
    EXPECT_EQ(FormatCoreUsage(instances), " 0:  12%   1: 100%   2:   0%   3:  50%\n 4: 100%  10:   5%");
    EXPECT_EQ(FormatCoreUsage({}), "");
}

TEST(Cpu, FormatMustRenderAnInteger) {
    EXPECT_THROW(Cpu(TextConfigWithFormat("{usage:.1f}%")), ConfigError);
    EXPECT_NO_THROW(Cpu(TextConfigWithFormat("{usage}%")));
    TextModuleConfig config = TextConfigWithFormat("{usage}%");
    config.tooltip          = "{usage:.1f}";
    EXPECT_THROW(Cpu(std::move(config)), ConfigError);
}

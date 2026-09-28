#include "modules/Cpu.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

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

TEST(Cpu, FormatMustRenderAnInteger) {
    EXPECT_THROW(Cpu(CpuWithFormat("{usage:.1f}%")), ConfigError);
    EXPECT_NO_THROW(Cpu(CpuWithFormat("{usage}%")));
}

TEST(Cpu, TooltipIsCheckedLikeTheFormat) {
    TextModuleConfig config = CpuWithFormat("{usage}%");
    config.tooltip          = "{usage:.1f}";
    EXPECT_THROW(Cpu(std::move(config)), ConfigError);
}

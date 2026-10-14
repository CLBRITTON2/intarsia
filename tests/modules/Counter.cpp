#include "modules/Counter.hpp"

#include "TestStyle.hpp"
#include "support/ChangeTarget.hpp"
#include "support/Eventually.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 1;

    CounterConfig           WithFormat(std::wstring path, std::string format) {
        return CounterConfig{
                      .path    = std::move(path),
                      .combine = CounterCombine::Average,
                      .text    = TextConfigWithFormat(std::move(format)),
        };
    }

    class CounterModule : public testing::Test {
      protected:
        ChangeTarget _target{kChanged};
    };

} // namespace

TEST(Counter, TheValueIsANumberAndTheRateIsBytesPerSecond) {
    EXPECT_EQ(FormatPlaceholders("{value:.1f}% {rate}", CounterPlaceholders(3'400'000.0)), "3400000.0% 3.4 MB/s");
}

TEST_F(CounterModule, FormatsAreCheckedAgainstTheValues) {
    const CounterConfig  config = WithFormat(L"\\Processor(_Total)\\% Processor Time", "{value:.0f}%");
    const CounterSampler sampler("cpu", config, _target.signal);
    EXPECT_NO_THROW(Counter(config, sampler));
    EXPECT_THROW(Counter(WithFormat(config.path, "{usage}"), sampler), ConfigError);
    EXPECT_THROW(Counter(WithFormat(config.path, "{rate:.1f}"), sampler), ConfigError);
}

// The sampler notifies only on a new outcome, so this tick is what redraws a new value.
TEST_F(CounterModule, ChangesByItsNextIntervalTick) {
    const CounterConfig                config = WithFormat(L"\\Processor(_Total)\\% Processor Time", "{value}");
    const CounterSampler               sampler("cpu", config, _target.signal);
    const Counter                      module(config, sampler);
    const std::optional<std::uint32_t> wait = module.MillisecondsUntilChange();
    ASSERT_TRUE(wait.has_value()) << "a counter waits for no change, so it shows its first sample forever";
    EXPECT_LE(wait.value_or(0), std::uint32_t{1000});
}

TEST_F(CounterModule, AFailingCounterShowsTheReasonAsItsTooltip) {
    const CounterConfig  config = WithFormat(L"\\No Such Object\\Nothing", "{value}");
    const CounterSampler sampler("gone", config, _target.signal);
    Counter              module(config, sampler);
    EXPECT_TRUE(Eventually([&sampler] { return sampler.Reading().outcome == CounterOutcome::Failed; }, std::chrono::seconds(2)));
    const std::optional<std::wstring> tooltip = module.TooltipText();
    ASSERT_TRUE(tooltip.has_value());
    EXPECT_NE(tooltip.value_or(L"").find(L"PdhAddEnglishCounterW"), std::wstring::npos);
}

#include "modules/Counter.hpp"

#include "TestStyle.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 1;

    CounterConfig SumOf(std::wstring path) {
        return CounterConfig{
            .path = std::move(path),
            .combine = CounterCombine::Sum,
            .text = TextConfigWithFormat("{value}"),
        };
    }

    CounterConfig WithFormat(std::wstring path, std::string format) {
        return CounterConfig{
            .path = std::move(path),
            .combine = CounterCombine::Average,
            .text = TextConfigWithFormat(std::move(format)),
        };
    }

    class Sampler : public testing::Test {
      protected:
        ChangeTarget _target{kChanged};
    };

    using CounterModule = Sampler;

} // namespace

TEST(CounterSampler, CombineSumsTakesTheMaxOrAveragesAndNoInstancesCombineToZero) {
    struct Case {
        CounterCombine combine;
        double ofOneFourSeven;
    };
    const std::vector<Case> cases{
        {.combine = CounterCombine::Sum, .ofOneFourSeven = 12.0},
        {.combine = CounterCombine::Max, .ofOneFourSeven = 7.0},
        {.combine = CounterCombine::Average, .ofOneFourSeven = 4.0},
    };
    for (const Case& test : cases) {
        SCOPED_TRACE(static_cast<std::int32_t>(test.combine));
        EXPECT_DOUBLE_EQ(CombineCounterValues({1.0, 4.0, 7.0}, test.combine), test.ofOneFourSeven);
        EXPECT_DOUBLE_EQ(CombineCounterValues({}, test.combine), 0.0);
    }
}

TEST(CounterSampler, OnlyANewOutcomeOrFailureChangesOutcome) {
    const CounterReading pending{
        .outcome = CounterOutcome::Pending,
        .value = 0.0,
        .detail = {},
    };
    const CounterReading sampled{
        .outcome = CounterOutcome::Sampled,
        .value = 1.0,
        .detail = {},
    };
    const CounterReading resampled{
        .outcome = CounterOutcome::Sampled,
        .value = 2.0,
        .detail = {},
    };
    const CounterReading failed{
        .outcome = CounterOutcome::Failed,
        .value = 0.0,
        .detail = "gone",
    };
    const CounterReading failedOtherwise{
        .outcome = CounterOutcome::Failed,
        .value = 0.0,
        .detail = "denied",
    };
    EXPECT_TRUE(ChangesOutcome(pending, sampled));
    EXPECT_FALSE(ChangesOutcome(sampled, resampled));
    EXPECT_TRUE(ChangesOutcome(sampled, failed));
    EXPECT_TRUE(ChangesOutcome(failed, failedOtherwise));
    EXPECT_FALSE(ChangesOutcome(failed, failed));
    EXPECT_TRUE(ChangesOutcome(failed, sampled));
}

TEST_F(Sampler, AStartedSamplerReadsTheCounterAfterOneInterval) {
    const CounterSampler sampler("cpu", SumOf(L"\\Processor(_Total)\\% Processor Time"), _target.signal);
    EXPECT_EQ(sampler.Reading().outcome, CounterOutcome::Pending);
    EXPECT_TRUE(Eventually([&sampler] { return sampler.Reading().outcome == CounterOutcome::Sampled; }, std::chrono::seconds(3)));
    EXPECT_GE(sampler.Reading().value, 0.0);
    MSG message{};
    EXPECT_TRUE(PeekMessageW(&message, _target.window.Get(), kChanged, kChanged, PM_REMOVE));
}

TEST_F(Sampler, ANewValueOfTheSameOutcomeIsReadWithoutANotify) {
    const CounterSampler sampler("uptime", SumOf(L"\\System\\System Up Time"), _target.signal);
    ASSERT_TRUE(Eventually([&sampler] { return sampler.Reading().outcome == CounterOutcome::Sampled; }, std::chrono::seconds(3)));
    const double first = sampler.Reading().value;
    MSG message{};
    ASSERT_TRUE(PeekMessageW(&message, _target.window.Get(), kChanged, kChanged, PM_REMOVE));

    EXPECT_TRUE(Eventually([&sampler, first] { return sampler.Reading().value > first; }, std::chrono::seconds(3))) << "the value stayed at " << first;
    EXPECT_EQ(sampler.Reading().outcome, CounterOutcome::Sampled);
    EXPECT_FALSE(PeekMessageW(&message, _target.window.Get(), kChanged, kChanged, PM_REMOVE));
}

TEST(Counter, TheValueIsANumberAndTheRateIsBytesPerSecond) {
    EXPECT_EQ(FormatPlaceholders("{value:.1f}% {rate}", CounterPlaceholders(3'400'000.0)), L"3400000.0% 3.4 MB/s");
}

TEST_F(CounterModule, FormatsAreCheckedAgainstTheValues) {
    const CounterConfig config = WithFormat(L"\\Processor(_Total)\\% Processor Time", "{value:.0f}%");
    const CounterSampler sampler("cpu", config, _target.signal);
    EXPECT_NO_THROW(Counter(config, sampler));
    EXPECT_THROW(Counter(WithFormat(config.path, "{usage}"), sampler), ConfigError);
    EXPECT_THROW(Counter(WithFormat(config.path, "{rate:.1f}"), sampler), ConfigError);
}

TEST_F(CounterModule, ChangesByItsNextIntervalTick) {
    const CounterConfig config = WithFormat(L"\\Processor(_Total)\\% Processor Time", "{value}");
    const CounterSampler sampler("cpu", config, _target.signal);
    const Counter module(config, sampler);
    const std::optional<std::uint32_t> wait = module.MillisecondsUntilChange();
    ASSERT_TRUE(wait.has_value()) << "a counter waits for no change, so it shows its first sample forever";
    EXPECT_LE(wait.value_or(0), std::uint32_t{1000});
}

TEST_F(CounterModule, AFailingCounterShowsTheReasonAsItsTooltip) {
    const CounterConfig config = WithFormat(L"\\No Such Object\\Nothing", "{value}");
    const CounterSampler sampler("gone", config, _target.signal);
    Counter module(config, sampler);
    EXPECT_TRUE(Eventually([&sampler] { return sampler.Reading().outcome == CounterOutcome::Failed; }, std::chrono::seconds(2)));
    const std::optional<std::wstring> tooltip = module.TooltipText();
    ASSERT_TRUE(tooltip.has_value());
    EXPECT_NE(tooltip.value_or(L"").find(L"PdhAddEnglishCounterW"), std::wstring::npos);
}

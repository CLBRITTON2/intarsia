#include "modules/CounterSampler.hpp"

#include "TestStyle.hpp"
#include "support/ChangeTarget.hpp"
#include "support/Eventually.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 1;

    CounterConfig           SumOf(std::wstring path) {
        return CounterConfig{
                      .path    = std::move(path),
                      .combine = CounterCombine::Sum,
                      .text    = TextConfigWithFormat("{value}"),
        };
    }

    class Sampler : public testing::Test {
      protected:
        ChangeTarget _target{kChanged};
    };

} // namespace

TEST(CounterSampler, CombineSumsTakesTheMaxOrAverages) {
    const std::vector<double> values{1.0, 4.0, 7.0};
    EXPECT_DOUBLE_EQ(CombineCounterValues(values, CounterCombine::Sum), 12.0);
    EXPECT_DOUBLE_EQ(CombineCounterValues(values, CounterCombine::Max), 7.0);
    EXPECT_DOUBLE_EQ(CombineCounterValues(values, CounterCombine::Average), 4.0);
}

TEST(CounterSampler, NoInstancesCombineToZero) {
    EXPECT_DOUBLE_EQ(CombineCounterValues({}, CounterCombine::Sum), 0.0);
    EXPECT_DOUBLE_EQ(CombineCounterValues({}, CounterCombine::Max), 0.0);
    EXPECT_DOUBLE_EQ(CombineCounterValues({}, CounterCombine::Average), 0.0);
}

TEST(CounterSampler, OnlyANewOutcomeOrFailureChangesOutcome) {
    const CounterReading pending{
        .outcome = CounterOutcome::Pending,
        .value   = 0.0,
        .detail  = {},
    };
    const CounterReading sampled{
        .outcome = CounterOutcome::Sampled,
        .value   = 1.0,
        .detail  = {},
    };
    const CounterReading resampled{
        .outcome = CounterOutcome::Sampled,
        .value   = 2.0,
        .detail  = {},
    };
    const CounterReading failed{
        .outcome = CounterOutcome::Failed,
        .value   = 0.0,
        .detail  = "gone",
    };
    const CounterReading failedOtherwise{
        .outcome = CounterOutcome::Failed,
        .value   = 0.0,
        .detail  = "denied",
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

// Only the module's own tick shows a new value, so the sampler stores it without a notify.
TEST_F(Sampler, ANewValueOfTheSameOutcomeIsReadWithoutANotify) {
    const CounterSampler sampler("uptime", SumOf(L"\\System\\System Up Time"), _target.signal);
    ASSERT_TRUE(Eventually([&sampler] { return sampler.Reading().outcome == CounterOutcome::Sampled; }, std::chrono::seconds(3)));
    const double first = sampler.Reading().value;
    MSG          message{};
    ASSERT_TRUE(PeekMessageW(&message, _target.window.Get(), kChanged, kChanged, PM_REMOVE));

    EXPECT_TRUE(Eventually([&sampler, first] { return sampler.Reading().value > first; }, std::chrono::seconds(3))) << "the value stayed at " << first;
    EXPECT_EQ(sampler.Reading().outcome, CounterOutcome::Sampled);
    EXPECT_FALSE(PeekMessageW(&message, _target.window.Get(), kChanged, kChanged, PM_REMOVE));
}

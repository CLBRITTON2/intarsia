#include "modules/CounterSampler.hpp"
#include "support/Eventually.hpp"
#include "support/MessageWindow.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <vector>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 1;

    CounterConfig           SumOf(std::wstring path) {
        return CounterConfig{.path    = std::move(path),
                                       .combine = CounterCombine::Sum,
                                       .text    = TextModuleConfig{.format = "{value}", .tooltip = std::nullopt, .icon = {}, .interval = std::chrono::seconds(1), .actions = {}}};
    }

    class Sampler : public testing::Test {
      public:
        Sampler() : _changes(_target.Get(), kChanged) {}

      protected:
        MessageWindow _target;
        ChangeSignal  _changes;
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

TEST_F(Sampler, AStartedSamplerReadsTheCounterAfterOneInterval) {
    const CounterSampler sampler("cpu", SumOf(L"\\Processor(_Total)\\% Processor Time"), _changes);
    EXPECT_EQ(sampler.Reading().outcome, CounterOutcome::Pending);
    EXPECT_TRUE(Eventually([&sampler] { return sampler.Reading().outcome == CounterOutcome::Sampled; }, std::chrono::seconds(3)));
    EXPECT_GE(sampler.Reading().value, 0.0);
    MSG message{};
    EXPECT_TRUE(PeekMessageW(&message, _target.Get(), kChanged, kChanged, PM_REMOVE));
}

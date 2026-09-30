#include "modules/CounterSampler.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <functional>
#include <thread>
#include <vector>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 1;

    CounterConfig           SumOf(std::wstring path) {
        return CounterConfig{.path    = std::move(path),
                                       .combine = CounterCombine::Sum,
                                       .text    = TextModuleConfig{.format = "{value}", .tooltip = std::nullopt, .icon = {}, .interval = std::chrono::seconds(1), .actions = {}}};
    }

    bool Eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit) {
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < deadline) {
            if (condition()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return condition();
    }

    class Sampler : public testing::Test {
      public:
        Sampler() : _target(CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)), _changes(_target, kChanged) {}
        ~Sampler() override {
            DestroyWindow(_target);
        }
        Sampler(const Sampler&)            = delete;
        Sampler& operator=(const Sampler&) = delete;

      protected:
        HWND         _target;
        ChangeSignal _changes;
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
    EXPECT_TRUE(PeekMessageW(&message, _target, kChanged, kChanged, PM_REMOVE));
}

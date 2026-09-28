#include "modules/Counter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <optional>
#include <string>
#include <thread>
#include <utility>

namespace {

    constexpr std::uint32_t kChanged = WM_APP + 1;

    CounterConfig           WithFormat(std::wstring path, std::string format) {
        return CounterConfig{.path    = std::move(path),
                                       .combine = CounterCombine::Average,
                                       .text = TextModuleConfig{.format = std::move(format), .tooltip = std::nullopt, .icon = L"", .interval = std::chrono::seconds(1), .actions = {}}};
    }

    class CounterModule : public testing::Test {
      public:
        CounterModule() : _target(CreateWindowExW(0, L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr)), _changes(_target, kChanged) {}
        ~CounterModule() override {
            DestroyWindow(_target);
        }
        CounterModule(const CounterModule&)            = delete;
        CounterModule& operator=(const CounterModule&) = delete;

      protected:
        HWND         _target;
        ChangeSignal _changes;
    };

} // namespace

TEST(Counter, TheValueIsANumberAndTheRateIsBytesPerSecond) {
    EXPECT_EQ(FormatPlaceholders("{value:.1f}% {rate}", CounterPlaceholders(3'400'000.0)), "3400000.0% 3.4 MB/s");
}

TEST_F(CounterModule, FormatsAreCheckedAgainstTheValues) {
    const CounterConfig  config = WithFormat(L"\\Processor(_Total)\\% Processor Time", "{value:.0f}%");
    const CounterSampler sampler("cpu", config, _changes);
    EXPECT_NO_THROW(Counter(config, sampler));
    EXPECT_THROW(Counter(WithFormat(config.path, "{usage}"), sampler), ConfigError);
    EXPECT_THROW(Counter(WithFormat(config.path, "{rate:.1f}"), sampler), ConfigError);
}

TEST_F(CounterModule, AFailingCounterShowsTheReasonAsItsTooltip) {
    const CounterConfig  config = WithFormat(L"\\No Such Object\\Nothing", "{value}");
    const CounterSampler sampler("gone", config, _changes);
    Counter              module(config, sampler);
    const auto           deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (sampler.Reading().outcome != CounterOutcome::Failed && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    const std::optional<std::wstring> tooltip = module.TooltipText();
    ASSERT_TRUE(tooltip.has_value());
    EXPECT_NE(tooltip.value_or(L"").find(L"PdhAddEnglishCounterW"), std::wstring::npos);
}

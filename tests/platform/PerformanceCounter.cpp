#include "platform/PerformanceCounter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>
#include <thread>
#include <vector>

namespace {

    // Samples every 200 ms until a sample has a value: PDH cannot calculate a rate from an occasional pair of samples.
    std::vector<double> NextValues(PerformanceCounter& counter) {
        constexpr std::uint32_t kSamples = 10;
        for (std::uint32_t sample = 0; sample < kSamples; ++sample) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            counter.Collect();
            const std::optional<std::vector<double>> values = counter.Values();
            if (values.has_value()) {
                return *values;
            }
        }
        ADD_FAILURE() << "none of " << kSamples << " samples had a value";
        return {};
    }

} // namespace

TEST(PerformanceCounter, ARateHasNoValueAtItsFirstSample) {
    PerformanceCounter counter(L"\\Processor(_Total)\\% Processor Time");
    counter.Collect();
    EXPECT_FALSE(counter.Values().has_value());
}

TEST(PerformanceCounter, ARateHasAValueFromALaterSample) {
    PerformanceCounter counter(L"\\Processor(_Total)\\% Processor Time");
    counter.Collect();
    const std::vector<double> values = NextValues(counter);
    ASSERT_EQ(values.size(), 1u);
    EXPECT_GE(values[0], 0.0);
}

TEST(PerformanceCounter, AWildcardHasAValuePerInstance) {
    PerformanceCounter counter(L"\\Processor(*)\\% Processor Time");
    counter.Collect();
    // Every logical processor, then _Total.
    EXPECT_GE(NextValues(counter).size(), 2u);
}

TEST(PerformanceCounter, AWildcardMatchingNothingHasNoValues) {
    PerformanceCounter counter(L"\\Process(intarsia-no-such-process*)\\ID Process");
    counter.Collect();
    counter.Collect();
    const std::optional<std::vector<double>> values = counter.Values();
    if (!values.has_value()) {
        FAIL() << "a wildcard matching nothing has no value, where it should have an empty one";
    }
    EXPECT_TRUE(values->empty());
}

TEST(PerformanceCounter, APathThisSystemHasNoCounterForThrows) {
    EXPECT_THROW(PerformanceCounter(L"\\No Such Object\\Nothing"), PdhError);
    EXPECT_THROW(PerformanceCounter(L"\\Processor(_Total)\\No Such Counter"), PdhError);
    EXPECT_THROW(PerformanceCounter(L"Processor(_Total)\\% Processor Time"), PdhError);
}

#include "platform/PerformanceCounter.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>
#include <vector>

TEST(PerformanceCounter, ARateHasValuesFromItsSecondSample) {
    PerformanceCounter counter(L"\\Processor(_Total)\\% Processor Time");
    counter.Collect();
    EXPECT_TRUE(counter.Values().empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    counter.Collect();
    const std::vector<double> values = counter.Values();
    ASSERT_EQ(values.size(), 1u);
    EXPECT_GE(values[0], 0.0);
}

TEST(PerformanceCounter, AWildcardHasAValuePerInstance) {
    PerformanceCounter counter(L"\\Processor(*)\\% Processor Time");
    counter.Collect();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    counter.Collect();
    // Every logical processor, then _Total.
    EXPECT_GE(counter.Values().size(), 2u);
}

TEST(PerformanceCounter, AWildcardMatchingNothingHasNoValues) {
    PerformanceCounter counter(L"\\Process(intarsia-no-such-process*)\\ID Process");
    counter.Collect();
    counter.Collect();
    EXPECT_TRUE(counter.Values().empty());
}

TEST(PerformanceCounter, APathThisSystemHasNoCounterForThrows) {
    EXPECT_THROW(PerformanceCounter(L"\\No Such Object\\Nothing"), PdhError);
    EXPECT_THROW(PerformanceCounter(L"\\Processor(_Total)\\No Such Counter"), PdhError);
    EXPECT_THROW(PerformanceCounter(L"Processor(_Total)\\% Processor Time"), PdhError);
}

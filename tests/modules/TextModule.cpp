#include "modules/TextModule.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace {

    std::chrono::system_clock::time_point AtMilliseconds(std::int64_t milliseconds) {
        return std::chrono::system_clock::time_point(std::chrono::milliseconds(milliseconds));
    }

} // namespace

TEST(Ticks, AlignsToEachInterval) {
    // On a boundary the wait is a full interval.
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(120'000), std::chrono::seconds(60)), std::chrono::milliseconds(60'000));
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(120'001), std::chrono::seconds(60)), std::chrono::milliseconds(59'999));
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(1'250), std::chrono::seconds(1)), std::chrono::milliseconds(750));
    EXPECT_EQ(TimeUntilNextTick(AtMilliseconds(3'599'000), std::chrono::seconds(3600)), std::chrono::milliseconds(1'000));
}

TEST(Ticks, ASampleIsDueOncePerIntervalWithSlackForAnEarlyTimer) {
    const std::chrono::steady_clock::time_point sampledAt{std::chrono::seconds(10)};
    EXPECT_FALSE(SampleDue(sampledAt, sampledAt, std::chrono::seconds(2)));
    EXPECT_FALSE(SampleDue(sampledAt, sampledAt + std::chrono::milliseconds(1'949), std::chrono::seconds(2)));
    EXPECT_TRUE(SampleDue(sampledAt, sampledAt + std::chrono::milliseconds(1'950), std::chrono::seconds(2)));
    EXPECT_TRUE(SampleDue(sampledAt, sampledAt + std::chrono::seconds(5), std::chrono::seconds(2)));
}

TEST(Ticks, EachSpecIsFormattedOncePerSample) {
    const std::chrono::steady_clock::time_point start{std::chrono::seconds(10)};
    SampledTexts texts(start);
    std::int32_t samples = 0;
    const auto sample = [&samples] { ++samples; };
    std::int32_t formats = 0;
    const auto format = [&formats] {
        ++formats;
        return std::to_wstring(formats);
    };
    EXPECT_EQ(texts.Of("{usage}", format), L"1");
    EXPECT_EQ(texts.Of("{ghz}", format), L"2");
    texts.SampleIfDue(start + std::chrono::seconds(1), std::chrono::seconds(2), sample);
    EXPECT_EQ(samples, 0);
    EXPECT_EQ(texts.Of("{usage}", format), L"1");
    texts.SampleIfDue(start + std::chrono::seconds(2), std::chrono::seconds(2), sample);
    EXPECT_EQ(samples, 1);
    EXPECT_EQ(texts.Of("{usage}", format), L"3");
    EXPECT_EQ(texts.Of("{usage}", format), L"3");
    // The interval counts from the last sample, not the first.
    texts.SampleIfDue(start + std::chrono::seconds(3), std::chrono::seconds(2), sample);
    EXPECT_EQ(samples, 1);
    EXPECT_EQ(formats, 3);
}

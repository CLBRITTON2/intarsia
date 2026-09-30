#include "modules/Memory.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>

namespace {

    TextModuleConfig MemoryWithFormat(std::string format) {
        return TextModuleConfig{.format = std::move(format), .tooltip = std::nullopt, .icon = L"", .interval = std::chrono::seconds(1), .actions = {}};
    }

} // namespace

TEST(Memory, TheTooltipValuesAreGibibytesAndAPercent) {
    constexpr std::uint64_t kGibibyte = std::uint64_t{1024} * 1024 * 1024;
    const MemoryUsage       usage{
              .totalBytes = 32 * kGibibyte, .availableBytes = 8 * kGibibyte, .cachedBytes = 6 * kGibibyte, .committedBytes = 30 * kGibibyte, .commitLimitBytes = 37 * kGibibyte};
    EXPECT_EQ(
        FormatPlaceholders("{used:.1f}/{total:.0f} GB {percent}% free {available:.0f} cached {cached:.0f} commit {committed:.0f}/{commit_limit:.0f}", MemoryPlaceholders(usage)),
        "24.0/32 GB 75% free 8 cached 6 commit 30/37");
}

TEST(Memory, FormatMustRenderADouble) {
    EXPECT_THROW(Memory(MemoryWithFormat("{used:d} GB")), ConfigError);
    EXPECT_NO_THROW(Memory(MemoryWithFormat("{used:.1f} GB")));
}

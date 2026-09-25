#include "modules/Memory.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

    TextModuleConfig MemoryWithFormat(std::string format) {
        return TextModuleConfig{.format = std::move(format), .icon = L"", .interval = std::chrono::seconds(1), .actions = {}};
    }

} // namespace

TEST(Memory, GibibytesUseBinaryUnits) {
    EXPECT_DOUBLE_EQ(BytesToGibibytes(1024ull * 1024 * 1024), 1.0);
    EXPECT_DOUBLE_EQ(BytesToGibibytes(3ull * 512 * 1024 * 1024), 1.5);
}

TEST(Memory, FormatMustRenderADouble) {
    EXPECT_THROW(Memory(MemoryWithFormat("{:d} GB")), ConfigError);
    EXPECT_NO_THROW(Memory(MemoryWithFormat("{:.1f} GB")));
}

#include "modules/Memory.hpp"

#include <gtest/gtest.h>

#include <chrono>

namespace {

    TextModuleConfig MemoryWithFormat(std::string format) {
        return TextModuleConfig{.format = std::move(format), .tooltip = std::nullopt, .icon = L"", .interval = std::chrono::seconds(1), .actions = {}};
    }

} // namespace

TEST(Memory, FormatMustRenderADouble) {
    EXPECT_THROW(Memory(MemoryWithFormat("{used:d} GB")), ConfigError);
    EXPECT_NO_THROW(Memory(MemoryWithFormat("{used:.1f} GB")));
}

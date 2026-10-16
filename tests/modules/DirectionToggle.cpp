#include "modules/DirectionToggle.hpp"

#include <gtest/gtest.h>

namespace {

    const TilingDirectionConfig kConfig{.horizontal = L"H", .vertical = L"V"};

} // namespace

TEST(DirectionToggle, ShowsTheConfiguredTextForTheDirection) {
    EXPECT_EQ(DirectionText(kConfig, TilingDirection::Horizontal), L"H");
    EXPECT_EQ(DirectionText(kConfig, TilingDirection::Vertical), L"V");
    EXPECT_EQ(DirectionText(kConfig, std::nullopt), L"");
}

#include "bar/Sections.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <vector>

TEST(Sections, MapKeepsOrderAndSectionMembership) {
    const Sections<std::int32_t> input{.left = {1, 2}, .center = {3}, .right = {4, 5, 6}};
    const Sections<std::int32_t> doubled = MapSections(input, [](std::int32_t value) { return value * 2; });
    EXPECT_EQ(doubled.left, (std::vector<std::int32_t>{2, 4}));
    EXPECT_EQ(doubled.center, (std::vector<std::int32_t>{6}));
    EXPECT_EQ(doubled.right, (std::vector<std::int32_t>{8, 10, 12}));
}

TEST(Sections, MapWorksWithMoveOnlyResults) {
    const Sections<std::int32_t>                  input{.left = {7}, .center = {}, .right = {8}};
    const Sections<std::unique_ptr<std::int32_t>> boxed = MapSections(input, [](std::int32_t value) { return std::make_unique<std::int32_t>(value); });
    ASSERT_EQ(boxed.left.size(), 1u);
    ASSERT_EQ(boxed.right.size(), 1u);
    EXPECT_EQ(*boxed.left[0], 7);
    EXPECT_EQ(*boxed.right[0], 8);
    EXPECT_TRUE(boxed.center.empty());
}

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

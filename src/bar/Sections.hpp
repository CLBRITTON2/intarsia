#pragma once

#include <algorithm>
#include <iterator>
#include <type_traits>
#include <vector>

// The three groups a bar lays out along its edge: left and center and right on a horizontal bar, top and center and
// bottom on a vertical one.
template <typename T>
struct Sections {
    std::vector<T> left;
    std::vector<T> center;
    std::vector<T> right;
};

template <typename T, typename Transform>
Sections<std::invoke_result_t<Transform, const T&>> MapSections(const Sections<T>& sections, Transform transform) {
    const auto mapOne = [&transform](const std::vector<T>& items) {
        std::vector<std::invoke_result_t<Transform, const T&>> mapped;
        mapped.reserve(items.size());
        std::ranges::transform(items, std::back_inserter(mapped), transform);
        return mapped;
    };
    return {.left = mapOne(sections.left), .center = mapOne(sections.center), .right = mapOne(sections.right)};
}

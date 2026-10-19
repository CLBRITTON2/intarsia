#include "render/Composition.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>

namespace {

    // D2D applies a color matrix to a row vector.
    std::array<float, 4> Apply(const std::array<float, 20>& matrix, const std::array<float, 4>& color) {
        std::array<float, 4> result{};
        for (size_t column = 0; column < 4; ++column) {
            result[column] = matrix[16 + column];
            for (size_t row = 0; row < 4; ++row) {
                result[column] += color[row] * matrix[(row * 4) + column];
            }
        }
        return result;
    }

} // namespace

TEST(Composition, BackdropColorMatrixLeavesAPlainBlurAsItIs) {
    const std::array<float, 4> out = Apply(BackdropColorMatrix({
                                               .source = BackdropSource::System,
                                               .radiusDips = 0.0f,
                                               .brightness = 1.0f,
                                               .saturation = 1.0f,
                                           }),
                                           {0.2f, 0.5f, 0.9f, 1.0f});
    EXPECT_NEAR(out[0], 0.2f, 1e-6f);
    EXPECT_NEAR(out[1], 0.5f, 1e-6f);
    EXPECT_NEAR(out[2], 0.9f, 1e-6f);
    EXPECT_FLOAT_EQ(out[3], 1.0f);
}

TEST(Composition, BackdropColorMatrixGreysByLuminanceAndScalesByBrightnessButNeverAlpha) {
    const std::array<float, 4> grey = Apply(BackdropColorMatrix({
                                                .source = BackdropSource::System,
                                                .radiusDips = 0.0f,
                                                .brightness = 0.5f,
                                                .saturation = 0.0f,
                                            }),
                                            {1.0f, 0.0f, 0.0f, 1.0f});
    EXPECT_NEAR(grey[0], 0.1065f, 1e-6f);
    EXPECT_NEAR(grey[1], 0.1065f, 1e-6f);
    EXPECT_NEAR(grey[2], 0.1065f, 1e-6f);
    EXPECT_FLOAT_EQ(grey[3], 1.0f);
    const std::array<float, 4> kept = Apply(BackdropColorMatrix({
                                                .source = BackdropSource::System,
                                                .radiusDips = 0.0f,
                                                .brightness = 1.0f,
                                                .saturation = 1.8f,
                                            }),
                                            {0.4f, 0.4f, 0.4f, 1.0f});
    EXPECT_NEAR(kept[0], 0.4f, 1e-6f);
    EXPECT_NEAR(kept[2], 0.4f, 1e-6f);
}

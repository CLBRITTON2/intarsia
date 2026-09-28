#include "render/Icon.hpp"

#include <objbase.h>

#include <gtest/gtest.h>

TEST(Icon, IconToPixelsGivesPremultipliedPixelsAtTheIconSize) {
    ASSERT_TRUE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
    const IconPixels pixels = IconToPixels(LoadIconW(nullptr, IDI_APPLICATION));
    CoUninitialize();
    EXPECT_GT(pixels.width, 0u);
    EXPECT_GT(pixels.height, 0u);
    EXPECT_EQ(pixels.pbgra.size(), static_cast<size_t>(pixels.width) * pixels.height * 4);
}

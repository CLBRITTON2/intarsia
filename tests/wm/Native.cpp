#include "wm/Native.hpp"

#include <gtest/gtest.h>

#include <string>

// Any process can give its own window such a title, and the window module's tooltip converts it to UTF-8.
TEST(Native, WindowTitleReplacesALoneSurrogateAndBidiControls) {
    const std::wstring title{L'x', static_cast<wchar_t>(0xD800), L'y', static_cast<wchar_t>(0x202E), L'z', static_cast<wchar_t>(0x2066)};
    HWND               window = CreateWindowExW(0, L"STATIC", title.c_str(), 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, nullptr, nullptr);
    ASSERT_NE(window, nullptr);
    constexpr auto kReplacement = static_cast<wchar_t>(0xFFFD);
    EXPECT_EQ(WindowTitle(window), (std::wstring{L'x', kReplacement, L'y', kReplacement, L'z', kReplacement}));
    DestroyWindow(window);
}

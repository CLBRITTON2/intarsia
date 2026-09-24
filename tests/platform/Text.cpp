#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(Text, RoundTripsAscii) {
    EXPECT_EQ(ToUtf8(L"intarsia"), "intarsia");
    EXPECT_EQ(ToWide("intarsia"), L"intarsia");
}

TEST(Text, RoundTripsAnAstralPlaneNerdFontGlyph) {
    const std::wstring glyph = L"\U000F1441";
    ASSERT_EQ(glyph.size(), 2u);
    const std::string utf8 = ToUtf8(glyph);
    EXPECT_EQ(utf8, "\xF3\xB1\x91\x81");
    EXPECT_EQ(ToWide(utf8), glyph);
}

TEST(Text, EmptyInputGivesEmptyOutput) {
    EXPECT_TRUE(ToUtf8(L"").empty());
    EXPECT_TRUE(ToWide("").empty());
}

TEST(Text, ToWideRejectsInvalidUtf8) {
    EXPECT_THROW(ToWide("\xFF\xFE"), Win32Error);
}

TEST(Text, ToUtf8RejectsALoneSurrogate) {
    const std::wstring loneHigh(1, static_cast<wchar_t>(0xD800));
    EXPECT_THROW(ToUtf8(loneHigh), Win32Error);
}

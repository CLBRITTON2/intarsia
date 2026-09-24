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

TEST(Text, LogExcerptEscapesQuotesBackslashesAndControlBytes) {
    EXPECT_EQ(LogExcerpt("a\"b\\c\nd\x7F", 100), "a\\\"b\\\\c\\x0ad\\x7f");
}

TEST(Text, LogExcerptCutsLongTextAndSaysHowMuchWasDropped) {
    EXPECT_EQ(LogExcerpt("abcdef", 4), "abcd...(2 more bytes)");
    EXPECT_EQ(LogExcerpt("abc", 4), "abc");
}

TEST(Text, LogExcerptNeverSplitsAUtf8Sequence) {
    // "a" then U+00E9 (two bytes): a cut after its first byte backs off to before it.
    EXPECT_EQ(LogExcerpt("a\xC3\xA9z", 2), "a...(3 more bytes)");
}

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

TEST(Text, LogExcerptEscapesBytesThatAreNotUtf8AndKeepsThoseThatAre) {
    // A stray continuation byte, a truncated sequence, an overlong '/', then a valid U+00E9.
    const std::string excerpt = LogExcerpt("a\x80"
                                           "b\xE2\x82"
                                           "c\xC0\xAF"
                                           "\xC3\xA9",
                                           100);
    EXPECT_EQ(excerpt, "a\\x80b\\xe2\\x82c\\xc0\\xaf\xC3\xA9");
    EXPECT_NO_THROW(ToWide(excerpt));
}

TEST(Text, ReplaceLoneSurrogatesKeepsPairsAndReplacesStrays) {
    const std::wstring text{
        L'a', static_cast<wchar_t>(0xD800), L'b', static_cast<wchar_t>(0xDC00), static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00), static_cast<wchar_t>(0xD800)};
    const std::wstring replaced     = ReplaceLoneSurrogates(text);
    constexpr auto     kReplacement = static_cast<wchar_t>(0xFFFD);
    EXPECT_EQ(replaced, (std::wstring{L'a', kReplacement, L'b', kReplacement, static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00), kReplacement}));
    EXPECT_NO_THROW(ToUtf8(replaced));
}

TEST(Text, LogExcerptNeverSplitsAUtf8Sequence) {
    // "a" then U+00E9 (two bytes): a cut after its first byte backs off to before it.
    EXPECT_EQ(LogExcerpt("a\xC3\xA9z", 2), "a...(3 more bytes)");
}

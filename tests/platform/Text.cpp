#include "platform/Errors.hpp"
#include "platform/Text.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>

namespace {

    // UTF-8 from its bytes, for a bidi mark that a string literal must not hold as text.
    std::string Utf8(std::initializer_list<std::uint8_t> bytes) {
        std::string text;
        for (const std::uint8_t byte : bytes) {
            text += static_cast<char>(byte);
        }
        return text;
    }

    const std::string kOverride = Utf8({0xE2, 0x80, 0xAE}); // U+202E
    const std::string kIsolate  = Utf8({0xE2, 0x81, 0xA6}); // U+2066

} // namespace

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
    // "a" then U+00E9 (two bytes): a cut after its first byte backs off to before it.
    EXPECT_EQ(LogExcerpt("a\xC3\xA9z", 2), "a...(3 more bytes)");
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

// A C1 control, a bidi override and a line separator are valid UTF-8 and would still reorder or break the log line.
TEST(Text, LogExcerptEscapesUnsafeCodePointsThatAreValidUtf8) {
    std::string text = "a\xC2\x85"
                       "b";
    text += kOverride;
    text += "c\xE2\x80\xA8"
            "d";
    text += kIsolate;
    EXPECT_EQ(LogExcerpt(text, 100), "a\\u0085b\\u202ec\\u2028d\\u2066");
}

TEST(Text, IsValidUtf8AcceptsWellFormedSequencesOnly) {
    EXPECT_TRUE(IsValidUtf8(""));
    EXPECT_TRUE(IsValidUtf8("plain"));
    EXPECT_TRUE(IsValidUtf8("\xC3\xA9\xF3\xB1\x91\x81"));
    EXPECT_FALSE(IsValidUtf8("\xFF"));
    EXPECT_FALSE(IsValidUtf8("\xC0\xAF"));     // overlong '/'
    EXPECT_FALSE(IsValidUtf8("\xED\xA0\x80")); // encoded surrogate
    EXPECT_FALSE(IsValidUtf8("\xE2\x82"));     // truncated
}

TEST(Text, DrawableTextReplacesUnsafeCharactersAndInvalidBytes) {
    constexpr auto kReplacement = static_cast<wchar_t>(0xFFFD);
    std::string    text         = kOverride;
    text += "ab\x01\xE2\x80\xA9";
    EXPECT_EQ(DrawableText(text), (std::wstring{kReplacement, L'a', L'b', kReplacement, kReplacement}));
    const std::wstring invalid = DrawableText("\xFF\xC0\xAF");
    EXPECT_FALSE(invalid.empty());
    EXPECT_TRUE(std::ranges::all_of(invalid, [](wchar_t unit) { return unit == kReplacement; }));
}

TEST(Text, DrawableWideTextReplacesUnsafeCharactersAndLoneSurrogates) {
    constexpr auto     kReplacement = static_cast<wchar_t>(0xFFFD);
    const std::wstring text{static_cast<wchar_t>(0x202E), L'a', static_cast<wchar_t>(0x200F), static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00),
                            static_cast<wchar_t>(0xDC00), L'\t'};
    EXPECT_EQ(DrawableText(std::wstring_view(text)),
              (std::wstring{kReplacement, L'a', kReplacement, static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00), kReplacement, kReplacement}));
}

TEST(Text, UnsafeCharactersAreControlsSeparatorsAndBidiMarks) {
    EXPECT_TRUE(IsUnsafeCharacter(L'\n'));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x7F)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x9F)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x2028)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x202E)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x2069)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x061C)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x200E)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0x200F)));
    EXPECT_TRUE(IsUnsafeCharacter(static_cast<wchar_t>(0xFEFF)));
    EXPECT_FALSE(IsUnsafeCharacter(L' '));
    EXPECT_FALSE(IsUnsafeCharacter(static_cast<wchar_t>(0x200D))); // a zero width joiner, which emoji sequences need
    EXPECT_FALSE(IsUnsafeCharacter(static_cast<wchar_t>(0x2027)));
}

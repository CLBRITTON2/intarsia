#include "platform/Text.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

namespace {

    // Bidi marks as bytes, which a string literal must not hold as text.
    const std::string kOverride{'\xE2', '\x80', '\xAE'}; // U+202E
    const std::string kIsolate{'\xE2', '\x81', '\xA6'};  // U+2066

} // namespace

TEST(Text, EmptyInputGivesEmptyOutput) {
    EXPECT_TRUE(ToUtf8(L"").empty());
    EXPECT_TRUE(ToWide("").empty());
}

TEST(Text, CharacterAtTakesTheWholeSequenceOrOneByteThatStartsNone) {
    EXPECT_EQ(CharacterAt("a\xC3\xA9z", 1), "\xC3\xA9");
    EXPECT_EQ(CharacterAt("a\xC3\xA9z", 0), "a");
    EXPECT_EQ(CharacterAt("\xA9z", 0), "\xA9");
    EXPECT_EQ(CharacterAt("\xC3", 0), "\xC3");
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

TEST(Text, ReplaceLoneSurrogatesKeepsPairsAndReplacesStrays) {
    const std::wstring text{
        L'a', static_cast<wchar_t>(0xD800), L'b', static_cast<wchar_t>(0xDC00), static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00), static_cast<wchar_t>(0xD800)};
    const std::wstring replaced = ReplaceLoneSurrogates(text);
    EXPECT_EQ(replaced,
              (std::wstring{L'a', kReplacementCharacter, L'b', kReplacementCharacter, static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00), kReplacementCharacter}));
    EXPECT_NO_THROW(ToUtf8(replaced));
}

TEST(Text, IsValidUtf8AcceptsWellFormedSequencesOnly) {
    EXPECT_TRUE(IsValidUtf8(""));
    EXPECT_TRUE(IsValidUtf8("\xC3\xA9\xF3\xB1\x91\x81"));
    EXPECT_FALSE(IsValidUtf8("\xFF"));
}

TEST(Text, DrawableTextReplacesUnsafeCharactersAndInvalidBytes) {
    std::string text = kOverride;
    text += "ab\x01\xE2\x80\xA9";
    EXPECT_EQ(DrawableText(text), (std::wstring{kReplacementCharacter, L'a', L'b', kReplacementCharacter, kReplacementCharacter}));
    const std::wstring invalid = DrawableText("\xFF\xC0\xAF");
    EXPECT_FALSE(invalid.empty());
    EXPECT_TRUE(std::ranges::all_of(invalid, [](wchar_t unit) { return unit == kReplacementCharacter; }));
}

TEST(Text, DrawableWideTextReplacesUnsafeCharactersAndLoneSurrogates) {
    const std::wstring text{static_cast<wchar_t>(0x202E), L'a', static_cast<wchar_t>(0x200F), static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00),
                            static_cast<wchar_t>(0xDC00), L'\t'};
    EXPECT_EQ(DrawableText(std::wstring_view(text)),
              (std::wstring{kReplacementCharacter, L'a', kReplacementCharacter, static_cast<wchar_t>(0xD83D), static_cast<wchar_t>(0xDE00), kReplacementCharacter,
                            kReplacementCharacter}));
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

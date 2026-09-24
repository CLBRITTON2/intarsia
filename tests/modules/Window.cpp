#include "modules/Window.hpp"

#include <gtest/gtest.h>

#include <string>

TEST(Window, TitleWithinTheLimitIsUnchanged) {
    EXPECT_EQ(TruncateTitle(L"intarsia", 8), L"intarsia");
}

TEST(Window, LongTitleEndsInAnEllipsisAtTheLimit) {
    const std::wstring title = TruncateTitle(L"intarsia - Visual Studio Code", 10);
    EXPECT_EQ(title, L"intarsia …");
    EXPECT_EQ(title.size(), 10u);
}

TEST(Window, TruncationNeverSplitsASurrogatePair) {
    // "ab" then U+F1441 as a surrogate pair then "cd": a cut after the high surrogate drops the whole pair.
    const std::wstring title = L"ab\U000F1441cd";
    EXPECT_EQ(TruncateTitle(title, 4), L"ab…");
}

TEST(Window, ZeroLimitGivesAnEmptyTitle) {
    EXPECT_TRUE(TruncateTitle(L"intarsia", 0).empty());
}

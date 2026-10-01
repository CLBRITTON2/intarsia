#include "bar/MenuLister.hpp"

#include "modules/CommandPoller.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    auto OutputErrorWith(std::string_view text) {
        return testing::ThrowsMessage<OutputError>(testing::HasSubstr(std::string(text)));
    }

} // namespace

TEST(MenuLister, ParsesEntriesWithActiveOptional) {
    const std::vector<MenuItem> items = ParseMenuOutput(R"({"items":[{"id":"work","text":"Work","active":true},{"id":"home","text":"Home"}]})");
    EXPECT_EQ(items,
              (std::vector<MenuItem>{MenuEntry{.id = "work", .text = "Work", .active = true, .tooltip = std::nullopt},
                                     MenuEntry{.id = "home", .text = "Home", .active = false, .tooltip = std::nullopt}}));
    EXPECT_TRUE(ParseMenuOutput(R"({"items":[]})").empty());
}

TEST(MenuLister, ParsesATooltipThatMayBreakLines) {
    const std::vector<MenuItem> items = ParseMenuOutput(R"({"items":[{"id":"a","text":"A","tooltip":"Full title\norg/repo #1"}]})");
    EXPECT_EQ(items, (std::vector<MenuItem>{MenuEntry{.id = "a", .text = "A", .active = false, .tooltip = "Full title\norg/repo #1"}}));
}

TEST(MenuLister, RefusesATooltipThatIsNotDrawableText) {
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"a","text":"A","tooltip":1}]})"); }, OutputErrorWith("tooltip that is not a string"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"a","text":"A","tooltip":"tab\there"}]})"); }, testing::Throws<OutputError>());
    EXPECT_THAT([] { return ParseMenuOutput("{\"items\":[{\"id\":\"a\",\"text\":\"A\",\"tooltip\":\"\\u202e\"}]}"); }, testing::Throws<OutputError>());
}

TEST(MenuLister, ParsesSeparatorsBetweenEntries) {
    const std::vector<MenuItem> items = ParseMenuOutput(R"({"items":[{"id":"a","text":"A"},{"separator":true},{"id":"b","text":"B"}]})");
    EXPECT_EQ(items,
              (std::vector<MenuItem>{MenuEntry{.id = "a", .text = "A", .active = false, .tooltip = std::nullopt}, MenuSeparator{},
                                     MenuEntry{.id = "b", .text = "B", .active = false, .tooltip = std::nullopt}}));
    EXPECT_TRUE(HasEntries(items));
    EXPECT_FALSE(HasEntries(ParseMenuOutput(R"({"items":[{"separator":true}]})")));
    EXPECT_FALSE(HasEntries({}));
}

TEST(MenuLister, RefusesASeparatorWithAnythingElse) {
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"separator":false}]})"); }, OutputErrorWith(R"(other than {"separator":true})"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"separator":1}]})"); }, OutputErrorWith(R"(other than {"separator":true})"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"separator":true,"id":"a","text":"A"}]})"); }, OutputErrorWith(R"(other than {"separator":true})"));
}

TEST(MenuLister, RefusesOutputThatIsNotOneItemsList) {
    EXPECT_THAT([] { return ParseMenuOutput("work\nhome"); }, OutputErrorWith("printed invalid JSON"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[],"title":"x"})"); }, OutputErrorWith("only an items list"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":{}})"); }, OutputErrorWith("not a list"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"a","text":"A","icon":"x"}]})"); }, OutputErrorWith("other than id, text, active and tooltip"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"a"}]})"); }, OutputErrorWith("without an id string and a text string"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"a","text":"A","active":1}]})"); }, OutputErrorWith("not true or false"));
}

TEST(MenuLister, RefusesIdsAProgramCouldReadAsAnOption) {
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"--delete","text":"A"}]})"); }, OutputErrorWith("starts with - or /"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"/s","text":"A"}]})"); }, OutputErrorWith("starts with - or /"));
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"","text":"A"}]})"); }, OutputErrorWith("empty"));
}

TEST(MenuLister, RefusesUndrawableTextAndTooManyItems) {
    EXPECT_THAT([] { return ParseMenuOutput("{\"items\":[{\"id\":\"a\",\"text\":\"\xff\"}]}"); }, testing::Throws<OutputError>());
    EXPECT_THAT([] { return ParseMenuOutput(R"({"items":[{"id":"a","text":"line\nbreak"}]})"); }, testing::Throws<OutputError>());
    std::string items;
    for (size_t index = 0; index <= kMaxMenuEntries; ++index) {
        items += std::format(R"({}{{"id":"e{}","text":"E"}})", index == 0 ? "" : ",", index);
    }
    EXPECT_THAT([&items] { return ParseMenuOutput("{\"items\":[" + items + "]}"); }, OutputErrorWith("more than 50 items"));
}

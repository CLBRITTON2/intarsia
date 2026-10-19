#include "modules/Custom.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

    const std::vector<std::string> kClasses{"sunny", "rain"};

    CommandResult Succeeded(std::string text, std::optional<std::string> tooltip, bool alert) {
        return CommandResult{
            .outcome = CommandOutcome::Succeeded,
            .text = std::move(text),
            .tooltip = std::move(tooltip),
            .alert = alert,
            .classIndex = std::nullopt,
            .detail = {},
        };
    }

    CommandResult ParseOutput(std::string_view output, CustomOutput format) {
        return ::ParseOutput(output, format, kClasses);
    }

    void ExpectFailure(std::string_view output, CustomOutput format, std::string_view reason) {
        const CommandResult result = ParseOutput(output, format);
        EXPECT_EQ(result.outcome, CommandOutcome::Failed) << output;
        EXPECT_EQ(result.text, reason) << output;
    }

} // namespace

TEST(CommandPoller, TextOutputIsTheFirstLine) {
    EXPECT_EQ(ParseOutput("\xF0\x9F\x8C\xA6 +74\xC2\xB0"
                          "F\r\nmore\r\n",
                          CustomOutput::Text),
              Succeeded("\xF0\x9F\x8C\xA6 +74\xC2\xB0"
                        "F",
                        std::nullopt, false));
    EXPECT_EQ(ParseOutput("unix\nsecond", CustomOutput::Text), Succeeded("unix", std::nullopt, false));
    EXPECT_EQ(ParseOutput("", CustomOutput::Text), Succeeded("", std::nullopt, false));
}

TEST(CommandPoller, TextOutputWithControlCharactersFails) {
    ExpectFailure("\x1b[31mred\x1b[0m\n", CustomOutput::Text, "output has control character U+001B");
    ExpectFailure("tab\there", CustomOutput::Text, "output has control character U+0009");
    // U+202E RIGHT-TO-LEFT OVERRIDE itself, which can make drawn text read backwards.
    ExpectFailure("safe\xE2\x80\xAEtxt.exe", CustomOutput::Text, "output has control character U+202E"); // NOLINT(misc-misleading-bidirectional)
    ExpectFailure("\xFF\xFE", CustomOutput::Text, "output is not UTF-8");
}

TEST(CommandPoller, JsonOutputSetsTextTooltipAndAlert) {
    EXPECT_EQ(ParseOutput(R"({"text": "3", "tooltip": "first\nsecond", "alert": true})", CustomOutput::Json), Succeeded("3", "first\nsecond", true));
    EXPECT_EQ(ParseOutput("{\"text\": \"3\"}\r\n", CustomOutput::Json), Succeeded("3", std::nullopt, false));
}

TEST(CommandPoller, AJsonClassIsOneTheModuleDeclares) {
    const CommandResult result = ParseOutput(R"({"text": "21C", "class": "rain"})", CustomOutput::Json);
    EXPECT_EQ(result.outcome, CommandOutcome::Succeeded);
    EXPECT_EQ(result.classIndex, std::optional<size_t>(1));
    ExpectFailure(R"({"text": "21C", "class": "snow"})", CustomOutput::Json, "printed a JSON class the module's classes do not list");
    ExpectFailure(R"({"text": "21C", "class": 2})", CustomOutput::Json, "printed a JSON class that is not a string");
    ExpectFailure(R"({"text": "21C", "class": "rain", "alert": true})", CustomOutput::Json, "printed both a JSON class and alert, and a module is in one state at a time");
    EXPECT_EQ(ParseOutput(R"({"text": "21C", "class": "sunny", "alert": false})", CustomOutput::Json).classIndex, std::optional<size_t>(0));
}

TEST(CommandPoller, JsonOutputIsCheckedFieldByField) {
    ExpectFailure("not json", CustomOutput::Json, "printed invalid JSON");
    ExpectFailure(R"(["3"])", CustomOutput::Json, "printed JSON that is not an object");
    ExpectFailure(R"({"tooltip": "x"})", CustomOutput::Json, "printed JSON without a text string");
    ExpectFailure(R"({"text": 3})", CustomOutput::Json, "printed JSON without a text string");
    ExpectFailure(R"({"text": "3", "color": "red"})", CustomOutput::Json, "printed a JSON field other than text, tooltip, alert and class");
    ExpectFailure(R"({"text": "3", "tooltip": 1})", CustomOutput::Json, "printed a JSON tooltip that is not a string");
    ExpectFailure(R"({"text": "3", "alert": "yes"})", CustomOutput::Json, "printed a JSON alert that is not true or false");
}

TEST(CommandPoller, JsonLineBreaksAreAllowedOnlyInTheTooltip) {
    ExpectFailure(R"({"text": "two\nlines"})", CustomOutput::Json, "text has control character U+000A");
    ExpectFailure(R"({"text": "3", "tooltip": "a\rb"})", CustomOutput::Json, "tooltip has control character U+000D");
    // Not raw strings: MSVC reads \u in a raw string as the character itself, and a bidi one there is error C5255.
    ExpectFailure("{\"text\": \"3\", \"tooltip\": \"\\u202e\"}", CustomOutput::Json, "tooltip has control character U+202E"); // NOLINT(modernize-raw-string-literal)
    ExpectFailure("{\"text\": \"\\u0000\"}", CustomOutput::Json, "text has control character U+0000");                        // NOLINT(modernize-raw-string-literal)
}

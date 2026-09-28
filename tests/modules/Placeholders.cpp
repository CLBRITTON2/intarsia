#include "modules/Placeholders.hpp"

#include "config/Config.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <format>
#include <vector>

namespace {

    const std::vector<Placeholder> kValues{{.name = "ssid", .value = std::string("home")}, {.name = "signal", .value = std::int64_t{72}}, {.name = "rate", .value = 1.25}};

} // namespace

TEST(Placeholders, NamesAreReplacedByTheirValues) {
    EXPECT_EQ(FormatPlaceholders("{ssid} {signal}%", kValues), "home 72%");
    EXPECT_EQ(FormatPlaceholders("", kValues), "");
    EXPECT_EQ(FormatPlaceholders("no values", kValues), "no values");
    EXPECT_EQ(FormatPlaceholders("{signal}{signal}", kValues), "7272");
}

TEST(Placeholders, ASpecFormatsTheValueAsStdFormatDoes) {
    EXPECT_EQ(FormatPlaceholders("{signal:>4}|{rate:.1f}|{ssid:.2}", kValues), "  72|1.2|ho");
}

TEST(Placeholders, DoubledBracesAreLiteral) {
    EXPECT_EQ(FormatPlaceholders("{{ssid}} {{}}", kValues), "{ssid} {}");
}

TEST(Placeholders, UnknownNamesBadSpecsAndStrayBracesThrow) {
    EXPECT_THROW(FormatPlaceholders("{essid}", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("{signal:.2s}", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("{ssid", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("ssid}", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("{}", kValues), std::format_error);
}

TEST(Placeholders, ConfigErrorsListTheNamesAFormatCanUse) {
    EXPECT_THAT([] { RequirePlaceholders("network format_wifi", "{essid}", kValues); },
                testing::ThrowsMessage<ConfigError>(
                    testing::HasSubstr("network format_wifi '{essid}' is not a valid format: {essid} is not a value this format has. It can use {ssid}, {signal}, {rate}")));
}

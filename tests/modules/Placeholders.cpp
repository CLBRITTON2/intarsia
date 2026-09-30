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

// A spec is handed to std::format, so a brace inside it must not open a second replacement field.
TEST(Placeholders, ASpecCannotNestAnotherField) {
    EXPECT_THROW(FormatPlaceholders("{ssid:{}}", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("{ssid:{signal}}", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("{ssid:>{signal}}", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("{:>3}", kValues), std::format_error);
    EXPECT_THROW(FormatPlaceholders("{ ssid}", kValues), std::format_error);
    EXPECT_EQ(FormatPlaceholders("{ssid:}", kValues), "home");
}

TEST(Placeholders, FormatNamesFindsAFieldByItsName) {
    EXPECT_TRUE(FormatNames("{down} x", "down"));
    EXPECT_TRUE(FormatNames("{down:>3}", "down"));
    EXPECT_FALSE(FormatNames("{{down}}", "down"));
    EXPECT_FALSE(FormatNames("{up} download", "down"));
    EXPECT_FALSE(FormatNames("", "down"));
    EXPECT_THROW(FormatNames("{down", "down"), std::format_error);
}

TEST(Placeholders, ConfigErrorsListTheNamesAFormatCanUse) {
    EXPECT_THAT([] { RequirePlaceholders("network format_wifi", "{essid}", kValues); },
                testing::ThrowsMessage<ConfigError>(
                    testing::HasSubstr("network format_wifi '{essid}' is not a valid format: {essid} is not a value this format has. It can use {ssid}, {signal}, {rate}")));
}

TEST(Placeholders, RatesUseSiUnits) {
    EXPECT_EQ(FormatRate(0.0), "0 B/s");
    EXPECT_EQ(FormatRate(999.0), "999 B/s");
    EXPECT_EQ(FormatRate(1250.0), "1.2 kB/s");
    EXPECT_EQ(FormatRate(3'400'000.0), "3.4 MB/s");
}

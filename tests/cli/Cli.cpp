#include "cli/Cli.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace {

    const std::filesystem::path kDefaultConfig = L"C:\\default\\config.lua";

    CliOptions Parse(const std::vector<std::wstring>& arguments) {
        return ParseCliOptions(arguments, kDefaultConfig);
    }

} // namespace

TEST(CliOptions, ReadsTheCommandAndTheConfigPathOrKeepsTheDefault) {
    const std::filesystem::path scratch = L"D:\\scratch\\config.lua";
    struct Case {
        std::wstring name;
        CliCommand command;
    };
    const Case cases[] = {{.name = L"monitors", .command = CliCommand::Monitors}, {.name = L"check", .command = CliCommand::Check}};
    for (const Case& test : cases) {
        SCOPED_TRACE(test.name);
        const CliOptions defaulted = Parse({test.name});
        EXPECT_EQ(defaulted.command, test.command);
        EXPECT_EQ(defaulted.configPath, kDefaultConfig);
        const CliOptions configured = Parse({test.name, L"--config", scratch.wstring()});
        EXPECT_EQ(configured.command, test.command);
        EXPECT_EQ(configured.configPath, scratch);
    }
}

TEST(CliOptions, RejectsBadArguments) {
    EXPECT_THROW(Parse({}), CliOptionsError);
    EXPECT_THROW(Parse({L"--config", L"D:\\scratch\\config.lua"}), CliOptionsError);
    EXPECT_THROW(Parse({L"workspaces"}), CliOptionsError);
    EXPECT_THROW(Parse({L"monitors", L"--config"}), CliOptionsError);
    EXPECT_THROW(Parse({L"monitors", L"--log", L"D:\\scratch\\bar.log"}), CliOptionsError);
    EXPECT_THROW(Parse({L"check", L"--config"}), CliOptionsError);
}

TEST(MonitorTable, ListsEachTargetUnderItsColumns) {
    const BarConfig bar{
        .edge = Edge::Top,
        .layer = Layer::Top,
        .space = Space::Reserve,
        .thicknessDips = 32.0F,
        .monitors = {MonitorWithId{.id = "DEL41A8-7XYZ123"}},
    };
    const MonitorDescription laptop{
        .monitor = nullptr,
        .primary = true,
        .targets = {DisplayTarget{.id = "BOE0B4D", .name = std::nullopt, .connection = "internal"}},
    };
    const MonitorDescription desk{
        .monitor = nullptr,
        .primary = false,
        .targets = {DisplayTarget{.id = "DEL41A8-7XYZ123", .name = "DELL U2720Q", .connection = "DisplayPort"}},
    };
    const MonitorDescription pathless{.monitor = nullptr, .primary = false, .targets = {}};
    EXPECT_EQ(MonitorTable({laptop, desk, pathless}, bar),
              "id               name         connection   primary  bar\n"
              "BOE0B4D          none         internal     yes      no\n"
              "DEL41A8-7XYZ123  DELL U2720Q  DisplayPort  no       yes\n"
              "none             none         none         no       no\n");
}

TEST(MonitorTable, ReplacesControlCharactersInAName) {
    const BarConfig bar{
        .edge = Edge::Top,
        .layer = Layer::Top,
        .space = Space::Reserve,
        .thicknessDips = 32.0F,
        .monitors = {MonitorWithId{.id = "DEL41A8"}},
    };
    // An OSC sequence that would set the terminal's title.
    const MonitorDescription hostile{
        .monitor = nullptr,
        .primary = true,
        .targets = {DisplayTarget{.id = "DEL41A8", .name = "EVIL\x1B]0;owned\x07", .connection = "HDMI"}},
    };
    const std::string table = MonitorTable({hostile}, bar);
    EXPECT_EQ(table.find('\x1B'), std::string::npos) << table;
    EXPECT_EQ(table.find('\x07'), std::string::npos) << table;
    EXPECT_NE(table.find("EVIL\xEF\xBF\xBD]0;owned\xEF\xBF\xBD  HDMI"), std::string::npos) << table;
}

#include "config/Config.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

    constexpr std::string_view kValidConfig = R"lua(
return {
    bar = { edge = "top", thickness = 54, border = 1, spacing = 8 },
    font = { family = "Some Font", text_size = 15, icon_size = 18 },
    colors = { background = "#191724e6", border = "#6e6a8640", foreground = "#e0def4", accent = "#c4a7e7", alert = "#eb6f92" },
    window_manager = "windows",
    modules_left = { "window" },
    modules_center = { "clock" },
    modules_right = {},
    clock = { format = "{:%H:%M}", icon = "x", interval = 60 },
    window = { max_length = 80 },
}
)lua";

    // A config file that deletes itself, so each case runs LoadConfig on a real file.
    class TempConfig {
      public:
        explicit TempConfig(std::string_view contents) :
            path_(std::filesystem::temp_directory_path() / ("intarsia-test-" + std::to_string(std::hash<std::string_view>{}(contents)) + ".lua")) {
            std::ofstream file(path_, std::ios::binary);
            file << contents;
        }
        ~TempConfig() {
            std::error_code error;
            if (!std::filesystem::remove(path_, error) || error) {
                std::fprintf(stderr, "could not delete temp config %ls: %s\n", path_.c_str(), error.message().c_str());
            }
        }
        TempConfig(const TempConfig&)                             = delete;
        TempConfig&                  operator=(const TempConfig&) = delete;

        const std::filesystem::path& Path() const {
            return path_;
        }

      private:
        std::filesystem::path path_;
    };

    // kValidConfig with one snippet swapped. Throws when the snippet is not there, so a stale case cannot pass.
    std::string ValidConfigWith(std::string_view from, std::string_view to) {
        std::string  text(kValidConfig);
        const size_t position = text.find(from);
        if (position == std::string::npos) {
            throw std::invalid_argument("snippet not in kValidConfig: " + std::string(from));
        }
        return text.replace(position, from.size(), to);
    }

    Config LoadText(std::string_view contents) {
        const TempConfig config(contents);
        return LoadConfig(config.Path());
    }

    auto ConfigErrorWith(std::string_view text) {
        return testing::ThrowsMessage<ConfigError>(testing::HasSubstr(std::string(text)));
    }

} // namespace

TEST(Config, ShippedExampleLoads) {
    const Config config = LoadConfig(INTARSIA_EXAMPLE_CONFIG);
    EXPECT_EQ(config.bar.edge, Edge::Top);
    EXPECT_EQ(config.modules.center, std::vector<ModuleKind>{ModuleKind::Clock});
    EXPECT_TRUE(config.clock.has_value());
}

TEST(Config, ValidConfigParsesIntoTypedValues) {
    const Config config = LoadText(kValidConfig);
    EXPECT_FLOAT_EQ(config.bar.thicknessDips, 54.0f);
    EXPECT_FLOAT_EQ(config.bar.spacingDips, 8.0f);
    EXPECT_EQ(config.font.family, L"Some Font");
    EXPECT_FLOAT_EQ(config.colors.background.a, 0xe6 / 255.0f);
    EXPECT_FLOAT_EQ(config.colors.foreground.a, 1.0f);
    ASSERT_TRUE(config.clock.has_value());
    if (config.clock.has_value()) {
        EXPECT_EQ(config.clock->interval, std::chrono::seconds(60));
    }
}

TEST(Config, MissingFileIsAnError) {
    EXPECT_THAT([] { return LoadConfig(std::filesystem::temp_directory_path() / "intarsia-test-missing.lua"); }, ConfigErrorWith("cannot open config file"));
}

TEST(Config, LuaSyntaxAndRuntimeErrorsAreReported) {
    EXPECT_THAT([] { return LoadText("return {"); }, ConfigErrorWith("expected"));
    EXPECT_THAT([] { return LoadText("error('boom')"); }, ConfigErrorWith("boom"));
}

TEST(Config, ChunkMustReturnATable) {
    EXPECT_THAT([] { return LoadText("return 42"); }, ConfigErrorWith("must return a table, got number"));
}

TEST(Config, MissingKeyNamesItsPath) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"(edge = "top", )", "")); }, ConfigErrorWith("bar.edge is missing"));
}

TEST(Config, WrongTypeNamesPathAndBothTypes) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith("thickness = 54", R"(thickness = "54")")); }, ConfigErrorWith("bar.thickness must be a number, got string"));
}

TEST(Config, UnknownKeysAreRejectedAtRootAndInsideTables) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith("modules_right = {},", "modules_right = {}, extra = 1,")); }, ConfigErrorWith("extra is not a known key"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith("spacing = 8", "spacing = 8, padding = 2")); }, ConfigErrorWith("bar.padding is not a known key"));
}

TEST(Config, EdgeMustBeOneOfFour) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"(edge = "top")", R"(edge = "side")")); }, ConfigErrorWith("got 'side'"));
}

TEST(Config, ColorsMustBeHex) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"("#e0def4")", R"("e0def4")")); }, ConfigErrorWith("colors.foreground must be '#RRGGBB'"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"("#e0def4")", R"("#e0dzf4")")); }, ConfigErrorWith("invalid hex digits 'dz'"));
}

TEST(Config, SizesMustBePositiveAndIntervalWholeSeconds) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith("thickness = 54", "thickness = 0")); }, ConfigErrorWith("bar.thickness must be greater than 0"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith("border = 1", "border = -1")); }, ConfigErrorWith("bar.border must be 0 or more"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith("interval = 60", "interval = 1.5")); }, ConfigErrorWith("clock.interval must be a whole number"));
}

TEST(Config, ModuleListsMustBePlainListsOfKnownModules) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"({ "clock" })", R"({ "clock", x = 1 })")); }, ConfigErrorWith("modules_center must be a plain list"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"({ "clock" })", R"({ "teapot" })")); },
                ConfigErrorWith("unknown module 'teapot', known modules are: clock, window, workspaces, cpu, memory"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"({ "clock" })", "{ 7 }")); }, ConfigErrorWith("modules_center[1] must be a string"));
}

TEST(Config, ListedModuleNeedsItsTableUnlistedMayKeepIt) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"(clock = { format = "{:%H:%M}", icon = "x", interval = 60 },)", "")); },
                ConfigErrorWith("clock is listed in a modules_ list but has no clock table"));
    EXPECT_TRUE(LoadText(ValidConfigWith(R"({ "clock" })", "{}")).clock.has_value());
}

TEST(Config, WindowManagerAndWindowModuleParse) {
    const Config config = LoadText(kValidConfig);
    EXPECT_EQ(config.windowManager, WindowManagerKind::Windows);
    EXPECT_EQ(config.modules.left, std::vector<ModuleKind>{ModuleKind::Window});
    ASSERT_TRUE(config.window.has_value());
    if (config.window.has_value()) {
        EXPECT_EQ(config.window->maxLength, 80u);
    }
}

TEST(Config, WindowManagerMustBeAKnownBackend) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"(window_manager = "windows")", R"(window_manager = "i3")")); },
                ConfigErrorWith("window_manager must be one of windows, glazewm, got 'i3'"));
}

TEST(Config, WorkspacesNeedAWindowManagerWithWorkspaces) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith(R"({ "window" })", R"({ "workspaces" })")); }, ConfigErrorWith("window_manager 'windows' has none"));
    const Config config =
        LoadText(ValidConfigWith("window_manager = \"windows\",\n    modules_left = { \"window\" },", "window_manager = \"glazewm\",\n    modules_left = { \"workspaces\" },"));
    EXPECT_EQ(config.windowManager, WindowManagerKind::GlazeWm);
    EXPECT_EQ(config.modules.left, std::vector<ModuleKind>{ModuleKind::Workspaces});
}

TEST(Config, WindowModuleNeedsItsTableAndAWholeMaxLength) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith("window = { max_length = 80 },", "")); }, ConfigErrorWith("window is listed in a modules_ list but has no window table"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith("max_length = 80", "max_length = 2.5")); }, ConfigErrorWith("window.max_length must be a whole number"));
}

TEST(Config, CpuAndMemoryShareTheTextModuleShape) {
    const Config config = LoadText(ValidConfigWith("modules_right = {},", R"(modules_right = { "cpu", "memory" },
    cpu = { format = "{}%", icon = "c", interval = 1 },
    memory = { format = "{:.1f} GB", icon = "m", interval = 2 },)"));
    EXPECT_EQ(config.modules.right, (std::vector<ModuleKind>{ModuleKind::Cpu, ModuleKind::Memory}));
    ASSERT_TRUE(config.cpu.has_value() && config.memory.has_value());
    if (config.cpu.has_value() && config.memory.has_value()) {
        EXPECT_EQ(config.cpu->format, "{}%");
        EXPECT_EQ(config.memory->interval, std::chrono::seconds(2));
    }
    EXPECT_THAT([] { return LoadText(ValidConfigWith("modules_right = {},", R"(modules_right = { "cpu" },)")); },
                ConfigErrorWith("cpu is listed in a modules_ list but has no cpu table"));
}

TEST(Config, ActionsResolveToAbsoluteExecutables) {
    const Config config = LoadText(ValidConfigWith("interval = 60 }", R"(interval = 60, on_click = { "cmd", "/c", "exit 0" }, on_scroll_up = { "cmd" } })"));
    ASSERT_TRUE(config.clock.has_value());
    if (config.clock.has_value()) {
        const ModuleActions& actions = config.clock->actions;
        ASSERT_TRUE(actions.click.has_value());
        if (actions.click.has_value()) {
            EXPECT_TRUE(actions.click->program.is_absolute());
            EXPECT_EQ(actions.click->arguments, (std::vector<std::wstring>{L"/c", L"exit 0"}));
        }
        EXPECT_TRUE(actions.scrollUp.has_value());
        EXPECT_FALSE(actions.rightClick.has_value());
        EXPECT_FALSE(actions.scrollDown.has_value());
    }
}

TEST(Config, ActionsMustNameAnExistingExecutable) {
    EXPECT_THAT([] { return LoadText(ValidConfigWith("interval = 60 }", "interval = 60, on_click = {} }")); }, ConfigErrorWith("clock.on_click must name a program"));
    EXPECT_THAT([] { return LoadText(ValidConfigWith("interval = 60 }", R"(interval = 60, on_click = { "intarsia-no-such-program" } })")); },
                ConfigErrorWith("not an existing file or a program on PATH"));
}

TEST(Config, ActionsRefuseScriptsThatWouldRunThroughCmd) {
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "intarsia-test-action.bat";
    std::ofstream(script) << "@echo off\n";
    std::string escaped = script.string();
    for (size_t position = escaped.find('\\'); position != std::string::npos; position = escaped.find('\\', position + 2)) {
        escaped.insert(position, "\\");
    }
    const std::string config = ValidConfigWith("interval = 60 }", "interval = 60, on_click = { \"" + escaped + "\" } }");
    EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith("which is not an .exe"));
    std::filesystem::remove(script);
}

TEST(Config, ErroringIndexMetamethodIsNotRun) {
    const std::string config =
        ValidConfigWith(R"(bar = { edge = "top", thickness = 54, border = 1, spacing = 8 },)", R"(bar = setmetatable({}, { __index = function() error("metamethod ran") end }),)");
    EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith("bar.edge is missing"));
}

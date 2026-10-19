#include "config/Config.hpp"

#include "support/ThrowsWith.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

namespace {

    constexpr std::string_view kValidConfig = R"lua(
return {
    bar = { edge = "top", layer = "top", space = "reserve", thickness = 54, monitors = { "*" } },
    window_manager = "windows",
    modules_left = { "title" },
    modules_center = { "time" },
    modules_right = {},
    layouts = {},
    modules = {
        time = { type = "clock", format = "{time:%H:%M}", icon = "x", interval = 60 },
        title = { type = "window", max_length = 80 },
    },
}
)lua";

    constexpr std::string_view kValidStyle = R"(
bar {
  font: Some Font
  size: 15
  color: #e0def4
}
)";

    // A config and a style.conf beside it in a directory that deletes itself, so each case runs LoadConfig on real files.
    class TempConfig {
      public:
        TempConfig(std::string_view config, std::string_view style) :
            _directory(std::filesystem::temp_directory_path() / ("intarsia-test-" + std::to_string(std::hash<std::string>{}(std::string(config) + std::string(style))))) {
            std::filesystem::create_directories(_directory);
            std::ofstream(_directory / "config.lua", std::ios::binary) << config;
            std::ofstream(_directory / "style.conf", std::ios::binary) << style;
        }
        ~TempConfig() {
            std::error_code error;
            std::filesystem::remove_all(_directory, error);
            if (error) {
                std::fprintf(stderr, "could not delete temp config %ls: %s\n", _directory.c_str(), error.message().c_str());
            }
        }
        TempConfig(const TempConfig&) = delete;
        TempConfig& operator=(const TempConfig&) = delete;

        std::filesystem::path Path() const {
            return _directory / "config.lua";
        }

      private:
        std::filesystem::path _directory;
    };

    // The config text with one snippet swapped. Throws when the snippet is not there, so a stale case cannot pass.
    std::string Replace(std::string_view config, std::string_view from, std::string_view to) {
        std::string text(config);
        const size_t position = text.find(from);
        if (position == std::string::npos) {
            throw std::invalid_argument("snippet not in the config: " + std::string(from));
        }
        return text.replace(position, from.size(), to);
    }

    // kValidConfig with extra modules defined, and shown on the right.
    std::string WithRightModules(std::string_view names, std::string_view definitions) {
        return Replace(Replace(kValidConfig, "modules_right = {},", std::string("modules_right = ") + std::string(names) + ","), "    modules = {\n",
                       "    modules = {\n" + std::string(definitions) + "\n");
    }

    std::string WithLayouts(std::string_view layouts) {
        return Replace(kValidConfig, "layouts = {},", "layouts = " + std::string(layouts) + ",");
    }

    // kValidConfig's clock module with more keys.
    std::string WithClockKeys(std::string_view config, std::string_view keys) {
        return Replace(config, "interval = 60 }", "interval = 60, " + std::string(keys) + " }");
    }

    constexpr std::string_view kIntervalRun = R"(run = "interval", timeout = 5,)";

    // A custom module's definition line. run is its run key and any timeout, each followed by a comma, as kIntervalRun.
    std::string CustomDefinition(std::string_view name, std::string_view run, std::string_view classes) {
        return std::format(
            R"(        {} = {{ type = "custom", command = {{ "cmd", "/c", "echo hi" }}, {} output = "json", classes = {}, format = "{{text}}", icon = "", interval = 30 }},)", name,
            run, classes);
    }

    Config LoadStyled(std::string_view config, std::string_view style) {
        const TempConfig files(config, style);
        return LoadConfig(files.Path());
    }

    Config LoadText(std::string_view config) {
        return LoadStyled(config, kValidStyle);
    }

    std::vector<ModuleKind> Kinds(const std::vector<ModuleInstance>& instances) {
        std::vector<ModuleKind> kinds;
        kinds.reserve(instances.size());
        for (const ModuleInstance& instance : instances) {
            kinds.push_back(instance.kind);
        }
        return kinds;
    }

    // Throws std::bad_variant_access, failing the test, when the instance holds other settings.
    template <typename Settings>
    const Settings& SettingsOf(const std::vector<ModuleInstance>& instances, size_t index) {
        return std::get<Settings>(instances.at(index).settings);
    }

} // namespace

// The shipped configs must load with their style, so neither drifts from the loader. What they show is theirs to change.
TEST(Config, ShippedConfigsLoad) {
    EXPECT_NO_THROW(LoadConfig(INTARSIA_WINDOWS_CONFIG));
    EXPECT_NO_THROW(LoadConfig(INTARSIA_GLAZEWM_CONFIG));
}

TEST(Config, ValidConfigParsesIntoTypedValues) {
    const Config config = LoadText(kValidConfig);
    EXPECT_FLOAT_EQ(config.bar.thicknessDips, 54.0f);
    EXPECT_EQ(config.style.font, L"Some Font");
    EXPECT_FLOAT_EQ(config.style.fontSizeDips, 15.0f);
    EXPECT_FLOAT_EQ(config.style.color.r, 0xe0 / 255.0f);
    EXPECT_EQ(config.windowManager, WindowManagerKind::Windows);
    EXPECT_EQ(config.modules.center.at(0).name, "time");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.center, 0).interval, std::chrono::seconds(60));
    EXPECT_EQ(Kinds(config.modules.left), std::vector<ModuleKind>{ModuleKind::Window});
    EXPECT_EQ(SettingsOf<WindowConfig>(config.modules.left, 0).maxLength, 80u);
}

TEST(Config, MissingFilesAreAnError) {
    const TempConfig files(kValidConfig, kValidStyle);
    std::filesystem::remove(StylePath(files.Path()));
    EXPECT_THAT([&files] { return LoadConfig(files.Path()); }, ThrowsWith<ConfigError>("cannot open style file"));
    std::filesystem::remove(files.Path());
    EXPECT_THAT([&files] { return LoadConfig(files.Path()); }, ThrowsWith<ConfigError>("cannot open config file"));
}

TEST(Config, ConfigFilesMustBeUtf8) {
    EXPECT_THAT([] { return LoadText("return {} -- \xFF\xFE"); },
                testing::ThrowsMessage<ConfigError>(testing::AllOf(testing::HasSubstr("config file"), testing::HasSubstr("is not valid UTF-8"))));
    EXPECT_THAT([] { return LoadStyled(kValidConfig, "// \xC0\xAF"); }, ThrowsWith<ConfigError>("style file"));
}

TEST(Config, FilesPastTheirSizeCapAreAnError) {
    EXPECT_THAT([] { return LoadText(std::string(kValidConfig) + "-- " + std::string(size_t{1} << 20, 'x')); }, ThrowsWith<ConfigError>("is larger than 1024 KiB"));
    EXPECT_THAT([] { return LoadStyled(kValidConfig, std::string(kValidStyle) + "// " + std::string(size_t{64} << 10, 'x')); }, ThrowsWith<ConfigError>("is larger than 64 KiB"));
    const std::string atTheCap = std::string(kValidStyle) + "// " + std::string((size_t{64} << 10) - kValidStyle.size() - 3, 'x');
    EXPECT_NO_THROW(LoadStyled(kValidConfig, atTheCap));
}

TEST(Config, LuaSyntaxAndRuntimeErrorsAreReported) {
    EXPECT_THAT([] { return LoadText("return {"); }, ThrowsWith<ConfigError>("expected"));
    EXPECT_THAT([] { return LoadText("error('boom')"); }, ThrowsWith<ConfigError>("boom"));
}

TEST(Config, ALuaErrorOfBytesThatAreNotUtf8IsReportedAsUtf8) {
    // U+FFFD in place of the bad byte, its literal split from "b" since a hex escape would take the b too.
    EXPECT_THAT([] { return LoadText(R"(error('a\xffb'))"); },
                ThrowsWith<ConfigError>("a\xEF\xBF\xBD"
                                        "b"));
}

TEST(Config, ChunkMustReturnATable) {
    EXPECT_THAT([] { return LoadText("return 42"); }, ThrowsWith<ConfigError>("must return a table, got number"));
}

TEST(Config, MissingKeyNamesItsPath) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(edge = "top", )", "")); }, ThrowsWith<ConfigError>("bar.edge is missing"));
}

TEST(Config, WrongTypeNamesPathAndBothTypes) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", R"(thickness = "54")")); }, ThrowsWith<ConfigError>("bar.thickness must be a number, got string"));
}

TEST(Config, UnknownKeysAreRejectedAtRootAndInsideTables) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "modules_right = {},", "modules_right = {}, extra = 1,")); }, ThrowsWith<ConfigError>("extra is not a known key"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 54, padding = 2")); }, ThrowsWith<ConfigError>("bar.padding is not a known key"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "max_length = 80", "max_length = 80, width = 3")); }, ThrowsWith<ConfigError>("modules.title.width is not a known key"));
}

TEST(Config, ChoicesNameEveryWordTheyTake) {
    struct Choice {
        std::string config;
        std::string_view error;
    };
    const std::vector<Choice> choices = {
        {.config = Replace(kValidConfig, R"(edge = "top")", R"(edge = "side")"), .error = "bar.edge must be one of top, bottom, left, right, got 'side'"},
        {.config = Replace(kValidConfig, R"(window_manager = "windows")", R"(window_manager = "i3")"), .error = "window_manager must be one of windows, glazewm, got 'i3'"},
        {.config = Replace(kValidConfig, R"(type = "clock")", R"(type = "teapot")"),
         .error = "modules.time.type must be one of clock, window, workspaces, windows, binding_mode, tiling_direction, cpu, memory, audio, network, battery, tray, "
                  "custom, counter, group, got 'teapot'"},
    };
    for (const Choice& choice : choices) {
        EXPECT_THAT([&choice] { return LoadText(choice.config); }, ThrowsWith<ConfigError>(choice.error));
    }
}

TEST(Config, SizesAndIntervalsMustBePositiveAndBounded) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 0")); }, ThrowsWith<ConfigError>("bar.thickness must be greater than 0"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 10001")); }, ThrowsWith<ConfigError>("bar.thickness must be at most 10000"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = 1.5")); }, ThrowsWith<ConfigError>("modules.time.interval must be a whole number"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = 86401")); },
                ThrowsWith<ConfigError>("modules.time.interval must be at most 86400 seconds"));
    EXPECT_NO_THROW(LoadText(Replace(kValidConfig, "interval = 60", "interval = 86400")));
}

TEST(Config, ModuleTableKeysMustBeStrings) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "    modules = {\n", "    modules = {\n        [1] = { type = \"clock\" },\n")); },
                ThrowsWith<ConfigError>("modules has a key that is not a string"));
}

// Lua escapes in valid UTF-8 source still build any bytes.
TEST(Config, ConfigStringsMustBeUtf8) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(icon = "x")", R"(icon = "\xff")")); }, ThrowsWith<ConfigError>("modules.time.icon is not valid UTF-8"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "title" })", R"({ "\u{D800}" })")); }, ThrowsWith<ConfigError>("modules_left[1] is not valid UTF-8"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "    modules = {\n", "    modules = {\n        [\"\\xc0\"] = { type = \"clock\" },\n")); },
                ThrowsWith<ConfigError>("a key of modules is not valid UTF-8"));
}

TEST(Config, ModuleNamesUseASafeAlphabet) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "two words" })", R"(        ["two words"] = { type = "cpu", format = "{usage}", icon = "", interval = 1 },)")); },
                ThrowsWith<ConfigError>("is not a valid module name"));
}

TEST(Config, AModuleMayNotTakeTheNameOfAStyleBlock) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "menu" })", R"(        menu = { type = "cpu" },)")); }, ThrowsWith<ConfigError>("names a block of style.conf's own"));
}

TEST(Config, OneTypeCanBeShownUnderSeveralNames) {
    const Config config = LoadText(WithRightModules(R"({ "cpu", "cpu_slow" })", R"(        cpu = { type = "cpu", format = "{usage}%", icon = "c", interval = 1 },
        cpu_slow = { type = "cpu", format = "{usage}", icon = "", interval = 5 },)"));
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::Cpu, ModuleKind::Cpu}));
    EXPECT_EQ(config.modules.right.at(1).name, "cpu_slow");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 0).format, "{usage}%");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 1).interval, std::chrono::seconds(5));
}

TEST(Config, MonitorsReadsEachEntryKind) {
    const Config config = LoadText(Replace(kValidConfig, R"(monitors = { "*" })", R"(monitors = { "primary", "internal", "DEL41A8-7XYZ123", "BOE0B4D" })"));
    EXPECT_EQ(config.bar.monitors, (std::vector<MonitorMatch>{PrimaryMonitor{}, InternalMonitor{}, MonitorWithId{.id = "DEL41A8-7XYZ123"}, MonitorWithId{.id = "BOE0B4D"}}));
    EXPECT_EQ(LoadText(kValidConfig).bar.monitors, std::vector<MonitorMatch>{AllMonitors{}});
}

TEST(Config, MonitorsRefusesListsThatCannotMeanWhatTheySay) {
    const auto withMonitors = [](std::string_view list) { return Replace(kValidConfig, R"(monitors = { "*" })", "monitors = " + std::string(list)); };
    EXPECT_THAT([&] { return LoadText(Replace(kValidConfig, R"(, monitors = { "*" })", "")); }, ThrowsWith<ConfigError>("bar.monitors is missing"));
    EXPECT_THAT([&] { return LoadText(withMonitors("{}")); }, ThrowsWith<ConfigError>("bar.monitors is empty"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "primary", "internal", "BOE0B4D", "internal" })")); }, ThrowsWith<ConfigError>("bar.monitors lists 'internal' twice"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "*", "primary" })")); }, ThrowsWith<ConfigError>("lists '*' beside other entries"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "Primary" })")); }, ThrowsWith<ConfigError>("lists 'Primary', which is not"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "del41a8" })")); }, ThrowsWith<ConfigError>("intarsia-cli monitors lists each monitor's id"));
}

TEST(Config, ShowsBarOnMatchesAnyEntry) {
    const auto bar = [](std::vector<MonitorMatch> monitors) {
        return BarConfig{
            .edge = Edge::Top,
            .layer = Layer::Top,
            .space = Space::Reserve,
            .thicknessDips = 32.0F,
            .monitors = std::move(monitors),
        };
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
    EXPECT_TRUE(ShowsBarOn(bar({AllMonitors{}}), pathless));
    EXPECT_TRUE(ShowsBarOn(bar({PrimaryMonitor{}}), laptop));
    EXPECT_FALSE(ShowsBarOn(bar({PrimaryMonitor{}}), desk));
    EXPECT_TRUE(ShowsBarOn(bar({InternalMonitor{}}), laptop));
    EXPECT_FALSE(ShowsBarOn(bar({InternalMonitor{}}), desk));
    EXPECT_TRUE(ShowsBarOn(bar({PrimaryMonitor{}, MonitorWithId{.id = "DEL41A8-7XYZ123"}}), desk));
    EXPECT_FALSE(ShowsBarOn(bar({MonitorWithId{.id = "DEL41A8"}}), desk));
    EXPECT_FALSE(ShowsBarOn(bar({MonitorWithId{.id = "DEL41A8"}}), pathless));
}

TEST(Config, ListsMayOnlyShowDefinedModules) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", R"({ "time", "clock" })")); },
                ThrowsWith<ConfigError>("modules_center shows 'clock', which is not defined in modules"));
}

TEST(Config, EveryDefinedModuleMustBeShown) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", "{}")); }, ThrowsWith<ConfigError>("modules.time is defined but no _modules list or group shows it"));
}

TEST(Config, TheLastLayoutAnEdgeReachesReplacesTheLists) {
    const Config config = LoadText(WithLayouts(R"({
        { min_length = 1921, modules_left = {}, modules_center = { "time" }, modules_right = { "title" } },
        { min_length = 3000, modules_left = { "time" }, modules_center = {}, modules_right = {} },
    })"));
    EXPECT_EQ(Kinds(ModulesForEdgeLength(config, 1920.0f).left), std::vector<ModuleKind>{ModuleKind::Window});
    EXPECT_EQ(Kinds(ModulesForEdgeLength(config, 1921.0f).right), std::vector<ModuleKind>{ModuleKind::Window});
    EXPECT_EQ(Kinds(ModulesForEdgeLength(config, 2999.5f).center), std::vector<ModuleKind>{ModuleKind::Clock});
    EXPECT_EQ(Kinds(ModulesForEdgeLength(config, 3000.0f).left), std::vector<ModuleKind>{ModuleKind::Clock});
    EXPECT_TRUE(ModulesForEdgeLength(config, 3000.0f).right.empty());
}

TEST(Config, AModuleOnlyALayoutShowsCountsAsShown) {
    const Config config = LoadText(Replace(WithLayouts(R"({ { min_length = 1921, modules_left = {}, modules_center = { "time" }, modules_right = {} } })"),
                                           R"(modules_center = { "time" })", "modules_center = {}"));
    EXPECT_TRUE(config.modules.center.empty());
    EXPECT_EQ(config.layouts.at(0).modules.center.at(0).name, "time");
}

TEST(Config, LayoutsAreNamedByTheirPlaceInErrors) {
    EXPECT_THAT([] { return LoadText(WithLayouts(R"({ { min_length = 1921, modules_left = {}, modules_center = {} } })")); },
                ThrowsWith<ConfigError>("layouts[1].modules_right is missing"));
}

TEST(Config, EachLayoutMustStartPastTheOneBefore) {
    EXPECT_THAT(
        [] {
            return LoadText(WithLayouts(R"({
            { min_length = 2560, modules_left = {}, modules_center = {}, modules_right = {} },
            { min_length = 1921, modules_left = {}, modules_center = {}, modules_right = {} },
        })"));
        },
        ThrowsWith<ConfigError>("layouts[2].min_length is 1921, it must be greater than the layout before it (2560)"));
}

namespace {

    constexpr std::string_view kGroup = R"(        layout = { type = "group", icon = "+", modules = { "cpu", "memory" } },
        cpu = { type = "cpu", format = "{usage}%", icon = "c", interval = 1 },
        memory = { type = "memory", format = "{used:.1f} GB", icon = "m", interval = 2 },)";

    std::string WithGroup(std::string_view group) {
        return WithRightModules(R"({ "layout" })", group);
    }

} // namespace

TEST(Config, AGroupReadsItsIconAndItsMembersInOrder) {
    const Config config = LoadText(WithGroup(kGroup));
    EXPECT_EQ(Kinds(config.modules.right), std::vector<ModuleKind>{ModuleKind::Group});
    const GroupConfig& group = SettingsOf<GroupConfig>(config.modules.right, 0);
    EXPECT_EQ(group.icon, L"+");
    EXPECT_EQ(Kinds(group.members), (std::vector<ModuleKind>{ModuleKind::Cpu, ModuleKind::Memory}));
    EXPECT_EQ(group.members.at(1).name, "memory");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(group.members, 1).interval, std::chrono::seconds(2));
}

TEST(Config, AGroupsMembersAreStyledByTheirOwnNames) {
    const Config config = LoadStyled(WithGroup(kGroup), std::string(kValidStyle) + "cpu {\n  color: #ff0000\n}\n");
    EXPECT_FLOAT_EQ(SettingsOf<GroupConfig>(config.modules.right, 0).members.at(0).style.color.r, 1.0f);
}

TEST(Config, AGroupNeedsAnIconAndAMember) {
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"(icon = "+")", R"(icon = "")"))); }, ThrowsWith<ConfigError>("modules.layout.icon must be a glyph to click"));
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", "{}"))); },
                ThrowsWith<ConfigError>("modules.layout.modules must name at least one module"));
}

TEST(Config, GroupsDoNotNest) {
    EXPECT_THAT(
        [] {
            return LoadText(WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "inner" })") +
                                      R"(
        inner = { type = "group", icon = "-", modules = { "memory" } },)"));
        },
        ThrowsWith<ConfigError>("modules.layout.modules lists 'inner', which is a group: groups do not nest"));
}

TEST(Config, AMemberShowsOnlyThroughItsOneGroup) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "layout", "cpu" })", kGroup)); },
                ThrowsWith<ConfigError>("modules.cpu is a member of group 'layout', so no _modules list may show it as well"));
    EXPECT_THAT(
        [] {
            return LoadText(
                Replace(WithGroup(kGroup), "layouts = {},", R"(layouts = { { min_length = 1921, modules_left = { "memory" }, modules_center = {}, modules_right = {} } },)"));
        },
        ThrowsWith<ConfigError>("modules.memory is a member of group 'layout', so no _modules list may show it as well"));
    EXPECT_THAT(
        [] {
            return LoadText(WithRightModules(R"({ "layout", "other" })", std::string(kGroup) + R"(
        other = { type = "group", icon = "-", modules = { "cpu" } },)"));
        },
        ThrowsWith<ConfigError>("modules.cpu is a member of both group 'layout' and group 'other'"));
    // One group any number of lists show is still one group.
    EXPECT_NO_THROW(
        LoadText(Replace(WithGroup(kGroup), "layouts = {},", R"(layouts = { { min_length = 1921, modules_left = { "layout" }, modules_center = {}, modules_right = {} } },)")));
}

TEST(Config, TheConfigsWalksReachAGroupsMembers) {
    const Config config = LoadText(WithGroup(kGroup));
    std::vector<std::string> names;
    ForEachModuleInConfig(config, [&names](const ModuleInstance& instance) { names.push_back(instance.name); });
    EXPECT_EQ(names, (std::vector<std::string>{"title", "time", "layout", "cpu", "memory"}));
    EXPECT_TRUE(AnyModuleInConfig(config, [](const ModuleInstance& instance) { return instance.kind == ModuleKind::Memory; }));
    EXPECT_FALSE(AnyModuleInConfig(config, [](const ModuleInstance& instance) { return instance.kind == ModuleKind::Battery; }));
}

// The member's classes style its own block, and the module block's too.
TEST(Config, AGroupsCustomMembersDeclareTheirClasses) {
    const std::string config =
        WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "memory", "weather" })") + "\n" + CustomDefinition("weather", kIntervalRun, R"({ "sunny" })"));
    EXPECT_NO_THROW(LoadStyled(config, std::string(kValidStyle) + "weather {\n  sunny {\n    color: #ff0000\n  }\n}\nmodule {\n  sunny {\n    color: #00ff00\n  }\n}\n"));
}

TEST(Config, TilingModulesNeedATilingWindowManager) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "tiled" })", R"(        tiled = { type = "workspaces" },)")); },
                ThrowsWith<ConfigError>("workspaces needs a tiling window manager, window_manager 'windows' is none"));
    // Through a layout's lists too, not only the top-level ones.
    EXPECT_THAT(
        [] {
            return LoadText(Replace(WithLayouts(R"({ { min_length = 1921, modules_left = { "spaces" }, modules_center = {}, modules_right = {} } })"), "    modules = {\n",
                                    "    modules = {\n        spaces = { type = \"workspaces\" },\n"));
        },
        ThrowsWith<ConfigError>("workspaces needs a tiling window manager"));

    const std::string onGlazeWm = Replace(WithRightModules(R"({ "desktops", "apps", "mode", "tiling" })", R"(        desktops = { type = "workspaces" },
        apps = { type = "windows" },
        mode = { type = "binding_mode" },
        tiling = { type = "tiling_direction", horizontal = "H", vertical = "V" },)"),
                                          R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config config = LoadText(onGlazeWm);
    EXPECT_EQ(config.windowManager, WindowManagerKind::GlazeWm);
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::Workspaces, ModuleKind::Windows, ModuleKind::BindingMode, ModuleKind::TilingDirection}));
    EXPECT_EQ(SettingsOf<TilingDirectionConfig>(config.modules.right, 3).vertical, L"V");
}

// Audio needs no window manager, so it loads with window_manager "windows".
TEST(Config, AudioParsesItsFormatsIconsStepAndOptionalTooltip) {
    const std::string audio =
        R"(        volume = { type = "audio", flow = "output", format = "{volume}%", format_muted = "muted", icon = "v", icon_muted = "m", step = 5, tooltip = "{device}" },)";
    const Config config = LoadText(WithRightModules(R"({ "volume" })", audio));
    const AudioConfig& settings = SettingsOf<AudioConfig>(config.modules.right, 0);
    EXPECT_EQ(settings.formatMuted, "muted");
    EXPECT_EQ(settings.iconMuted, L"m");
    EXPECT_EQ(settings.stepPercent, 5);
    EXPECT_EQ(settings.tooltip, "{device}");
    EXPECT_FALSE(SettingsOf<AudioConfig>(LoadText(WithRightModules(R"({ "volume" })", Replace(audio, R"(, tooltip = "{device}")", ""))).modules.right, 0).tooltip.has_value());
    EXPECT_THAT([&audio] { return LoadText(WithRightModules(R"({ "volume" })", Replace(audio, "step = 5", "step = 101"))); },
                ThrowsWith<ConfigError>("modules.volume.step is a percent of full volume, from 1 to 100, got 101"));
}

TEST(Config, BatteryParsesItsFormatsIconsAndThresholds) {
    const std::string battery = R"(        power = { type = "battery", format_discharging = "{capacity}%", format_charging = "", format_plugged = "", icons = { "e", "f" }, )"
                                R"(icon_charging = "c", icon_plugged = "p", warning_at = 40, critical_at = 15 },)";
    const Config config = LoadText(WithRightModules(R"({ "power" })", battery));
    const BatteryConfig& settings = SettingsOf<BatteryConfig>(config.modules.right, 0);
    EXPECT_EQ(settings.formatDischarging, "{capacity}%");
    EXPECT_EQ(settings.icons, (std::vector<std::wstring>{L"e", L"f"}));
    EXPECT_EQ(settings.iconPlugged, L"p");
    EXPECT_EQ(settings.warningAtPercent, 40);
    EXPECT_EQ(settings.criticalAtPercent, 15);
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, "warning_at = 40", "warning_at = 101"))); },
                ThrowsWith<ConfigError>("modules.power.warning_at is a percent of full charge, from 1 to 100, got 101"));
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, "critical_at = 15", "critical_at = 40"))); },
                ThrowsWith<ConfigError>("modules.power.critical_at (40) must be under modules.power.warning_at (40)"));
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, R"({ "e", "f" })", "{}"))); },
                ThrowsWith<ConfigError>("modules.power.icons must hold at least one icon, emptiest first"));
}

TEST(Config, TrayTakesItsToggleIcon) {
    const Config config = LoadText(WithRightModules(R"({ "icons" })", R"(        icons = { type = "tray", icon = ">" },)"));
    EXPECT_EQ(Kinds(config.modules.right), std::vector<ModuleKind>{ModuleKind::Tray});
    EXPECT_EQ(SettingsOf<TrayConfig>(config.modules.right, 0).icon, L">");
}

TEST(Config, ActionsResolveToAbsoluteExecutables) {
    const Config config = LoadText(WithClockKeys(kValidConfig, R"(on_click = { "cmd", "/c", "exit 0" }, on_middle_click = { "cmd" })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.click.has_value());
    const Command& click = std::get<Command>(*actions.click);
    EXPECT_TRUE(click.program.is_absolute());
    EXPECT_EQ(click.arguments, (std::vector<std::wstring>{L"/c", L"exit 0"}));
    EXPECT_TRUE(actions.middleClick.has_value());
    EXPECT_FALSE(actions.rightClick.has_value());
    EXPECT_FALSE(actions.scrollDown.has_value());
}

TEST(Config, ActionsMustNameAnExistingExecutable) {
    EXPECT_THAT([] { return LoadText(WithClockKeys(kValidConfig, "on_click = {}")); }, ThrowsWith<ConfigError>("modules.time.on_click must name a program"));
    EXPECT_THAT([] { return LoadText(WithClockKeys(kValidConfig, R"(on_click = { "intarsia-no-such-program" })")); },
                ThrowsWith<ConfigError>("not an existing file or a program on PATH"));
}

TEST(Config, ActionsRefuseScriptsThatWouldRunThroughCmd) {
    const TempConfig scratch(kValidConfig, kValidStyle);
    const std::filesystem::path script = scratch.Path().parent_path() / "action.bat";
    std::ofstream(script) << "@echo off\n";
    const std::string config = WithClockKeys(kValidConfig, "on_click = { [[" + script.string() + "]] }");
    EXPECT_THAT([&config] { return LoadText(config); }, ThrowsWith<ConfigError>("which is not an .exe"));
}

TEST(Config, WindowManagerActionsSendTheirTextAsWritten) {
    const std::string glazeWm = Replace(kValidConfig, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config config = LoadText(WithClockKeys(glazeWm, R"(on_scroll_up = { window_manager = "focus --next-workspace" })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.scrollUp.has_value());
    EXPECT_EQ(std::get<WindowManagerCommand>(*actions.scrollUp).text, "focus --next-workspace");

    EXPECT_THAT([&glazeWm] { return LoadText(WithClockKeys(glazeWm, R"(on_click = { window_manager = "focus\n--workspace 2" })")); },
                ThrowsWith<ConfigError>("modules.time.on_click.window_manager must be a command on one line"));
    EXPECT_THAT([&glazeWm] { return LoadText(WithClockKeys(glazeWm, R"(on_click = { window_manager = "" })")); }, ThrowsWith<ConfigError>("must be a command on one line"));
}

TEST(Config, WindowManagerActionsNeedAWindowManagerThatTakesCommands) {
    EXPECT_THAT([] { return LoadText(WithClockKeys(kValidConfig, R"(on_click = { window_manager = "focus --next-workspace" })")); },
                ThrowsWith<ConfigError>("window_manager 'windows' takes none"));
}

TEST(Config, MenuActionsListThroughOneCommandAndSelectThroughAnother) {
    const Config config =
        LoadText(WithClockKeys(kValidConfig, R"(on_click = { menu = { command = { "cmd", "/c", "list" }, timeout = 5, on_select = { "explorer", "/select" } } })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.click.has_value());
    const MenuAction& menu = std::get<MenuAction>(*actions.click);
    EXPECT_TRUE(menu.command.program.is_absolute());
    EXPECT_EQ(menu.command.arguments, (std::vector<std::wstring>{L"/c", L"list"}));
    EXPECT_EQ(menu.timeout, std::chrono::seconds(5));
    EXPECT_EQ(menu.onSelect.arguments, (std::vector<std::wstring>{L"/select"}));
    EXPECT_THAT([] { return LoadText(WithClockKeys(kValidConfig, R"(on_click = { menu = { command = { "cmd" }, timeout = 5, on_select = { "cmd", "/c", "pick" } } })")); },
                ThrowsWith<ConfigError>("modules.time.on_click.menu.on_select may not run cmd.exe"));
}

TEST(Config, CustomModulesParseTheirCommandAndTimeout) {
    const Config config = LoadText(WithRightModules(R"({ "hello" })", CustomDefinition("hello", kIntervalRun, R"({ "sunny", "rain" })")));
    const CustomConfig& custom = SettingsOf<CustomConfig>(config.modules.right, 0);
    EXPECT_EQ(custom.command.program.filename(), L"cmd.exe");
    EXPECT_EQ(custom.command.arguments, (std::vector<std::wstring>{L"/c", L"echo hi"}));
    EXPECT_EQ(std::get<IntervalRun>(custom.run).timeout, std::chrono::seconds(5));
    EXPECT_EQ(custom.output, CustomOutput::Json);
    EXPECT_EQ(custom.classes, (std::vector<std::string>{"sunny", "rain"}));
    EXPECT_EQ(custom.text.interval, std::chrono::seconds(30));
}

namespace {

    std::string WithCustomRun(std::string_view run) {
        return WithRightModules(R"({ "hello" })", CustomDefinition("hello", run, "{}"));
    }

} // namespace

TEST(Config, ContinuousCustomModulesTakeNoTimeout) {
    const Config config = LoadText(WithCustomRun(R"(run = "continuous",)"));
    EXPECT_TRUE(std::holds_alternative<ContinuousRun>(SettingsOf<CustomConfig>(config.modules.right, 0).run));
    EXPECT_THAT([] { return LoadText(WithCustomRun(R"(run = "continuous", timeout = 5,)")); }, ThrowsWith<ConfigError>("applies only to run = \"interval\""));
}

TEST(Config, CustomModulesNeedARun) {
    EXPECT_THAT([] { return LoadText(WithCustomRun("timeout = 5,")); }, ThrowsWith<ConfigError>("modules.hello.run is missing"));
}

TEST(Config, CounterModulesReadTheirPathAndCombine) {
    const Config config = LoadText(WithRightModules(
        R"({ "gpu" })", R"(        gpu = { type = "counter", counter = "\\Processor(*)\\% Processor Time", combine = "max", format = "{value:.0f}%", icon = "", interval = 2 },)"));
    const CounterConfig& counter = SettingsOf<CounterConfig>(config.modules.right, 0);
    EXPECT_EQ(counter.path, L"\\Processor(*)\\% Processor Time");
    EXPECT_EQ(counter.combine, CounterCombine::Max);
    EXPECT_EQ(counter.text.interval, std::chrono::seconds(2));
}

TEST(Config, CounterPathsMustNameALocalCounterThisSystemHas) {
    const auto load = [](std::string_view path) {
        return LoadText(WithRightModules(
            R"({ "c" })", std::format(R"(        c = {{ type = "counter", counter = "{}", combine = "sum", format = "{{value}}", icon = "", interval = 1 }},)", path)));
    };
    EXPECT_THAT([&load] { return load(R"(\\No Such Object\\Nothing)"); }, ThrowsWith<ConfigError>("modules.c.counter names no counter this system has: PdhAddEnglishCounterW"));
    EXPECT_THAT([&load] { return load(R"(Processor(_Total)\\% Processor Time)"); }, ThrowsWith<ConfigError>("modules.c.counter must be a local counter path"));
    EXPECT_THAT([&load] { return load(R"(\\\\otherhost\\Processor(_Total)\\% Processor Time)"); }, ThrowsWith<ConfigError>("modules.c.counter must be a local counter path"));
}

TEST(Config, ModulesAreStyledByTheirNameInTheConfig) {
    const Config config = LoadStyled(kValidConfig, std::string(kValidStyle) + "time {\n  color: #ff0000\n  icon {\n    color: #00ff00\n  }\n}\n");
    const ModuleStyle& time = config.modules.center.at(0).style;
    EXPECT_FLOAT_EQ(time.color.r, 1.0f);
    EXPECT_FLOAT_EQ(time.icon.color.g, 1.0f);
    // The window module takes the bar's color, since no block names it.
    EXPECT_FLOAT_EQ(config.modules.left.at(0).style.color.r, 0xe0 / 255.0f);
}

// The style's own errors, which carry the style file's name and line, reach the user through LoadConfig.
TEST(Config, StyleIdsMustNameADefinedModule) {
    EXPECT_THAT([] { return LoadStyled(kValidConfig, std::string(kValidStyle) + "tmie {\n  color: #ff0000\n}\n"); },
                ThrowsWith<ConfigError>("style.conf:7: tmie: names no module"));
}

namespace {

    std::string WithWeather(std::string_view classes) {
        return WithRightModules(R"({ "weather" })", CustomDefinition("weather", kIntervalRun, classes));
    }

} // namespace

TEST(Config, AClassNoCustomModuleDeclaresIsAnError) {
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "module {\n  snowy {\n    color: #ff0000\n  }\n}\n"); },
                ThrowsWith<ConfigError>("module: snowy is not a block module holds"));
    // Declared by the weather module, but the block names a clock.
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "time {\n  sunny {\n    color: #ff0000\n  }\n}\n"); },
                ThrowsWith<ConfigError>("time: sunny is not a block a module holds"));
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "weather {\n  sunny {\n    item {\n      color: #ff0000\n    }\n  }\n}\n"); },
                ThrowsWith<ConfigError>("weather > sunny: item is not a block weather > sunny holds, which are: icon"));
}

TEST(Config, DeclaredClassesAreClassNamesOfTheirOwn) {
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "two words" })")); }, ThrowsWith<ConfigError>("modules.weather.classes lists 'two words', which is not a class name"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "alert" })")); }, ThrowsWith<ConfigError>("modules.weather.classes lists 'alert', a class intarsia styles itself"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "sunny", "sunny" })")); }, ThrowsWith<ConfigError>("modules.weather.classes lists 'sunny' twice"));
}

// The config cannot read files, load chunks (load takes bytecode, which Lua does not verify), reach outside the
// process, catch its abort (a protected call could swallow it), hide from it (Lua runs a __gc metamethod with hooks
// off), or run a pattern match (a short pattern backtracks for longer than the bar lives, inside C calls the
// instruction count never sees).
TEST(Config, TheConfigHasNoGlobalThatReachesOutOrEscapesItsLimits) {
    struct Removed {
        std::string_view config;
        std::string_view error;
    };
    for (const Removed& removed : std::initializer_list<Removed>{
             {.config = "dofile('C:/Windows/win.ini')", .error = "attempt to call a nil value (global 'dofile')"},
             {.config = "loadfile('C:/Windows/win.ini')", .error = "attempt to call a nil value (global 'loadfile')"},
             {.config = "load('return 1')", .error = "attempt to call a nil value (global 'load')"},
             {.config = "return require('os')", .error = "attempt to call a nil value (global 'require')"},
             {.config = "pcall(print)", .error = "attempt to call a nil value (global 'pcall')"},
             {.config = "xpcall(function() end, print)", .error = "attempt to call a nil value (global 'xpcall')"},
             {.config = "setmetatable({}, {})", .error = "attempt to call a nil value (global 'setmetatable')"},
             {.config = "return ('a'):find('a')", .error = "attempt to call a nil value (method 'find')"},
             {.config = "return string.match('a', 'a')", .error = "attempt to call a nil value (field 'match')"},
             {.config = "return string.gmatch('a', 'a')", .error = "attempt to call a nil value (field 'gmatch')"},
             {.config = "return string.gsub('a', 'a', 'b')", .error = "attempt to call a nil value (field 'gsub')"},
         }) {
        EXPECT_THAT([&removed] { return LoadText(removed.config); }, ThrowsWith<ConfigError>(removed.error));
    }
    for (const std::string_view library : {"os", "io", "package", "debug", "coroutine", "utf8"}) {
        const std::string config = std::string("return ") + std::string(library) + ".x";
        EXPECT_THAT([&config] { return LoadText(config); }, ThrowsWith<ConfigError>(std::string("attempt to index a nil value (global '") + std::string(library) + "')"));
    }
}

// One C call can allocate any amount while running no instructions the count hook sees.
TEST(Config, AConfigThatAllocatesTooMuchIsStoppedWithAnError) {
    EXPECT_THAT([] { return LoadText("return { x = ('x'):rep(2^25) }"); }, ThrowsWith<ConfigError>("the config used more than 16 MiB of memory"));
}

// Each module's table stays on the Lua stack while the config is read, far past the slots Lua guarantees.
TEST(Config, ManyModulesFitOnTheLuaStack) {
    std::string names = "{ ";
    std::string definitions;
    for (std::int32_t index = 0; index < 200; ++index) {
        names += "\"clock" + std::to_string(index) + "\", ";
        definitions += "        clock" + std::to_string(index) + " = { type = \"clock\", format = \"{time:%H:%M}\", icon = \"x\", interval = 60 },\n";
    }
    names += "}";
    EXPECT_EQ(LoadText(WithRightModules(names, definitions)).modules.right.size(), 200u);
}

TEST(Config, AnEndlessConfigIsStoppedWithAnError) {
    EXPECT_THAT([] { return LoadText("while true do end"); }, ThrowsWith<ConfigError>("without finishing, is there an endless loop?"));
}

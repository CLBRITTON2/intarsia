#include "config/Config.hpp"

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
        TempConfig(const TempConfig&)                      = delete;
        TempConfig&           operator=(const TempConfig&) = delete;

        std::filesystem::path Path() const {
            return _directory / "config.lua";
        }

      private:
        std::filesystem::path _directory;
    };

    // The config text with one snippet swapped. Throws when the snippet is not there, so a stale case cannot pass.
    std::string Replace(std::string_view config, std::string_view from, std::string_view to) {
        std::string  text(config);
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

    Config LoadStyled(std::string_view config, std::string_view style) {
        const TempConfig files(config, style);
        return LoadConfig(files.Path());
    }

    Config LoadText(std::string_view config) {
        return LoadStyled(config, kValidStyle);
    }

    auto ConfigErrorWith(std::string_view text) {
        return testing::ThrowsMessage<ConfigError>(testing::HasSubstr(std::string(text)));
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

TEST(Config, MonitorsReadsEachEntryKind) {
    const Config config = LoadText(Replace(kValidConfig, R"(monitors = { "*" })", R"(monitors = { "primary", "internal", "DEL41A8-7XYZ123", "BOE0B4D" })"));
    EXPECT_EQ(config.bar.monitors, (std::vector<MonitorMatch>{PrimaryMonitor{}, InternalMonitor{}, MonitorWithId{.id = "DEL41A8-7XYZ123"}, MonitorWithId{.id = "BOE0B4D"}}));
    EXPECT_EQ(LoadText(kValidConfig).bar.monitors, std::vector<MonitorMatch>{AllMonitors{}});
}

TEST(Config, MonitorsRefusesListsThatCannotMeanWhatTheySay) {
    const auto withMonitors = [](std::string_view list) { return Replace(kValidConfig, R"(monitors = { "*" })", "monitors = " + std::string(list)); };
    EXPECT_THAT([&] { return LoadText(Replace(kValidConfig, R"(, monitors = { "*" })", "")); }, ConfigErrorWith("bar.monitors is missing"));
    EXPECT_THAT([&] { return LoadText(withMonitors("{}")); }, ConfigErrorWith("bar.monitors is empty"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "primary", "primary" })")); }, ConfigErrorWith("lists 'primary' twice"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "primary", "internal", "BOE0B4D", "internal" })")); }, ConfigErrorWith("bar.monitors lists 'internal' twice"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "*", "primary" })")); }, ConfigErrorWith("lists '*' beside other entries"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "Primary" })")); }, ConfigErrorWith("lists 'Primary', which is not"));
    EXPECT_THAT([&] { return LoadText(withMonitors(R"({ "del41a8" })")); }, ConfigErrorWith("intarsia-cli monitors lists each monitor's id"));
}

TEST(Config, ShowsBarOnMatchesAnyEntry) {
    const auto bar = [](std::vector<MonitorMatch> monitors) {
        return BarConfig{
            .edge          = Edge::Top,
            .layer         = Layer::Top,
            .space         = Space::Reserve,
            .thicknessDips = 32.0F,
            .monitors      = std::move(monitors),
        };
    };
    const MonitorDescription laptop{
        .monitor = nullptr,
        .primary = true,
        .targets = {DisplayTarget{.id = "BOE0B4D", .name = std::nullopt, .connection = "internal", .internal = true}},
    };
    const MonitorDescription desk{
        .monitor = nullptr,
        .primary = false,
        .targets = {DisplayTarget{.id = "DEL41A8-7XYZ123", .name = "DELL U2720Q", .connection = "DisplayPort", .internal = false}},
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

TEST(Config, MissingFileIsAnError) {
    EXPECT_THAT([] { return LoadConfig(std::filesystem::temp_directory_path() / "intarsia-test-missing.lua"); }, ConfigErrorWith("cannot open config file"));
}

TEST(Config, MissingStyleIsAnError) {
    const std::filesystem::path directory = std::filesystem::temp_directory_path() / "intarsia-test-no-style";
    std::filesystem::create_directories(directory);
    std::ofstream(directory / "config.lua", std::ios::binary) << kValidConfig;
    EXPECT_THAT([&directory] { return LoadConfig(directory / "config.lua"); }, ConfigErrorWith("cannot open style file"));
    std::filesystem::remove_all(directory);
}

TEST(Config, ModulesAreStyledByTheirNameInTheConfig) {
    const Config       config = LoadStyled(kValidConfig, std::string(kValidStyle) + "time {\n  color: #ff0000\n  icon {\n    color: #00ff00\n  }\n}\n");
    const ModuleStyle& time   = config.modules.center.at(0).style;
    EXPECT_FLOAT_EQ(time.color.r, 1.0f);
    EXPECT_FLOAT_EQ(time.icon.color.g, 1.0f);
    // The window module takes the bar's color, since no block names it.
    EXPECT_FLOAT_EQ(config.modules.left.at(0).style.color.r, 0xe0 / 255.0f);
}

TEST(Config, StyleIdsMustNameADefinedModule) {
    EXPECT_THAT([] { return LoadStyled(kValidConfig, std::string(kValidStyle) + "tmie {\n  color: #ff0000\n}\n"); }, ConfigErrorWith("style.conf:7: tmie: names no module"));
}

TEST(Config, StyleErrorsNameTheStyleFileAndLine) {
    EXPECT_THAT([] { return LoadStyled(kValidConfig, std::string(kValidStyle) + "\nmodule {\n  colour: #ff0000\n}\n"); },
                ConfigErrorWith("style.conf:9: module: colour is not a property"));
}

TEST(Config, AModuleMayNotTakeTheNameOfAStyleBlock) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "menu" })", R"(        menu = { type = "cpu" },)")); }, ConfigErrorWith("names a block of style.conf's own"));
}

TEST(Config, TrayTakesItsToggleIcon) {
    const Config config = LoadText(WithRightModules(R"({ "icons" })", R"(        icons = { type = "tray", icon = ">" },)"));
    EXPECT_EQ(Kinds(config.modules.right), std::vector<ModuleKind>{ModuleKind::Tray});
    EXPECT_EQ(SettingsOf<TrayConfig>(config.modules.right, 0).icon, L">");
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "icons" })", R"(        icons = { type = "tray" },)")); }, ConfigErrorWith("modules.icons.icon is missing"));
}

TEST(Config, LuaSyntaxAndRuntimeErrorsAreReported) {
    EXPECT_THAT([] { return LoadText("return {"); }, ConfigErrorWith("expected"));
    EXPECT_THAT([] { return LoadText("error('boom')"); }, ConfigErrorWith("boom"));
}

TEST(Config, ALuaErrorOfBytesThatAreNotUtf8IsReportedAsUtf8) {
    // U+FFFD in place of the bad byte, its literal split from "b" since a hex escape would take the b too.
    EXPECT_THAT([] { return LoadText(R"(error('a\xffb'))"); },
                ConfigErrorWith("a\xEF\xBF\xBD"
                                "b"));
}

TEST(Config, ChunkMustReturnATable) {
    EXPECT_THAT([] { return LoadText("return 42"); }, ConfigErrorWith("must return a table, got number"));
}

TEST(Config, MissingKeyNamesItsPath) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(edge = "top", )", "")); }, ConfigErrorWith("bar.edge is missing"));
}

TEST(Config, WrongTypeNamesPathAndBothTypes) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", R"(thickness = "54")")); }, ConfigErrorWith("bar.thickness must be a number, got string"));
}

TEST(Config, UnknownKeysAreRejectedAtRootAndInsideTables) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "modules_right = {},", "modules_right = {}, extra = 1,")); }, ConfigErrorWith("extra is not a known key"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 54, padding = 2")); }, ConfigErrorWith("bar.padding is not a known key"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "max_length = 80", "max_length = 80, width = 3")); }, ConfigErrorWith("modules.title.width is not a known key"));
}

TEST(Config, ChoicesNameEveryWordTheyTake) {
    struct Choice {
        std::string      config;
        std::string_view error;
    };
    const std::vector<Choice> choices = {
        {.config = Replace(kValidConfig, R"(edge = "top")", R"(edge = "side")"), .error = "bar.edge must be one of top, bottom, left, right, got 'side'"},
        {.config = Replace(kValidConfig, R"(layer = "top")", R"(layer = "overlay")"), .error = "bar.layer must be one of top, bottom, got 'overlay'"},
        {.config = Replace(kValidConfig, R"(space = "reserve")", R"(space = "float")"), .error = "bar.space must be one of reserve, share, got 'float'"},
        {.config = Replace(kValidConfig, R"(window_manager = "windows")", R"(window_manager = "i3")"), .error = "window_manager must be one of windows, glazewm, got 'i3'"},
        {.config = WithRightModules(
             R"({ "hello" })",
             R"(        hello = { type = "custom", command = { "cmd" }, run = "interval", timeout = 5, output = "xml", classes = {}, format = "{text}", icon = "", interval = 30 },)"),
         .error = "modules.hello.output must be one of text, json, got 'xml'"},
        {.config = WithRightModules(
             R"({ "c" })", R"(        c = { type = "counter", counter = "\\Memory\\Available MBytes", combine = "median", format = "{value}", icon = "", interval = 1 },)"),
         .error = "modules.c.combine must be one of sum, max, average, got 'median'"},
    };
    for (const Choice& choice : choices) {
        EXPECT_THAT([&choice] { return LoadText(choice.config); }, ConfigErrorWith(choice.error));
    }
}

TEST(Config, SizesAndIntervalsMustBePositiveAndBounded) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 0")); }, ConfigErrorWith("bar.thickness must be greater than 0"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 10001")); }, ConfigErrorWith("bar.thickness must be at most 10000"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = 1.5")); }, ConfigErrorWith("modules.time.interval must be a whole number"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = 86401")); }, ConfigErrorWith("modules.time.interval must be at most 86400 seconds"));
    EXPECT_NO_THROW(LoadText(Replace(kValidConfig, "interval = 60", "interval = 86400")));
}

TEST(Config, ModuleTableKeysMustBeStrings) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "    modules = {\n", "    modules = {\n        [1] = { type = \"clock\" },\n")); },
                ConfigErrorWith("modules has a key that is not a string"));
}

TEST(Config, ConfigFilesMustBeUtf8) {
    EXPECT_THAT([] { return LoadText("return {} -- \xFF\xFE"); },
                testing::ThrowsMessage<ConfigError>(testing::AllOf(testing::HasSubstr("config file"), testing::HasSubstr("is not valid UTF-8"))));
    EXPECT_THAT([] { return LoadStyled(kValidConfig, "// \xC0\xAF"); }, ConfigErrorWith("style file"));
}

TEST(Config, FilesPastTheirSizeCapAreAnError) {
    EXPECT_THAT([] { return LoadText(std::string(kValidConfig) + "-- " + std::string(size_t{1} << 20, 'x')); }, ConfigErrorWith("is larger than 1024 KiB"));
    EXPECT_THAT([] { return LoadStyled(kValidConfig, std::string(kValidStyle) + "// " + std::string(size_t{64} << 10, 'x')); }, ConfigErrorWith("is larger than 64 KiB"));
    const std::string atTheCap = std::string(kValidStyle) + "// " + std::string((size_t{64} << 10) - kValidStyle.size() - 3, 'x');
    EXPECT_NO_THROW(LoadStyled(kValidConfig, atTheCap));
}

// Lua escapes in valid UTF-8 source still build any bytes.
TEST(Config, ConfigStringsMustBeUtf8) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(icon = "x")", R"(icon = "\xff")")); }, ConfigErrorWith("modules.time.icon is not valid UTF-8"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "title" })", R"({ "\u{D800}" })")); }, ConfigErrorWith("modules_left[1] is not valid UTF-8"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "    modules = {\n", "    modules = {\n        [\"\\xc0\"] = { type = \"clock\" },\n")); },
                ConfigErrorWith("a key of modules is not valid UTF-8"));
}

// A relative path with a directory part would resolve against the bar's current directory.
TEST(Config, ActionsRefuseRelativePathsWithDirectories) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { ".\\cmd.exe" } })")); },
                ConfigErrorWith("not an existing file or a program on PATH"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { "System32\\cmd.exe" } })")); },
                ConfigErrorWith("not an existing file or a program on PATH"));
}

TEST(Config, ListsMayOnlyShowDefinedModules) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", R"({ "time", "clock" })")); },
                ConfigErrorWith("modules_center shows 'clock', which is not defined in modules"));
}

TEST(Config, EveryDefinedModuleMustBeShown) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", "{}")); }, ConfigErrorWith("modules.time is defined but no _modules list or group shows it"));
}

namespace {

    constexpr std::string_view kGroup = R"(        layout = { type = "group", icon = "+", modules = { "cpu", "memory" } },
        cpu = { type = "cpu", format = "{usage}%", icon = "c", interval = 1 },
        memory = { type = "memory", format = "{used:.1f} GB", icon = "m", interval = 2 },)";

    std::string                WithGroup(std::string_view group) {
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

TEST(Config, AGroupTakesOnlyItsTypeIconAndModules) {
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"("memory" } },)", R"("memory" }, on_click = { "cmd" } },)"))); },
                ConfigErrorWith("modules.layout.on_click is not a known key"));
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"(icon = "+", )", ""))); }, ConfigErrorWith("modules.layout.icon is missing"));
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"(icon = "+")", R"(icon = "")"))); }, ConfigErrorWith("modules.layout.icon must be a glyph to click"));
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", "{}"))); }, ConfigErrorWith("modules.layout.modules must name at least one module"));
}

TEST(Config, AGroupsMembersMustBeDefinedListedOnceAndNotGroups) {
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "memory", "disk" })"))); },
                ConfigErrorWith("modules.layout.modules shows 'disk', which is not defined in modules"));
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "memory", "cpu" })"))); },
                ConfigErrorWith("modules.layout.modules lists 'cpu' twice"));
    EXPECT_THAT(
        [] {
            return LoadText(WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "inner" })") +
                                      R"(
        inner = { type = "group", icon = "-", modules = { "memory" } },)"));
        },
        ConfigErrorWith("modules.layout.modules lists 'inner', which is a group: groups do not nest"));
}

TEST(Config, AMemberShowsOnlyThroughItsOneGroup) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "layout", "cpu" })", kGroup)); },
                ConfigErrorWith("modules.cpu is a member of group 'layout', so no _modules list may show it as well"));
    EXPECT_THAT(
        [] {
            return LoadText(
                Replace(WithGroup(kGroup), "layouts = {},", R"(layouts = { { min_length = 1921, modules_left = { "memory" }, modules_center = {}, modules_right = {} } },)"));
        },
        ConfigErrorWith("modules.memory is a member of group 'layout', so no _modules list may show it as well"));
    EXPECT_THAT(
        [] {
            return LoadText(WithRightModules(R"({ "layout", "other" })", std::string(kGroup) + R"(
        other = { type = "group", icon = "-", modules = { "cpu" } },)"));
        },
        ConfigErrorWith("modules.cpu is a member of both group 'layout' and group 'other'"));
    // One group any number of lists show is still one group.
    EXPECT_NO_THROW(
        LoadText(Replace(WithGroup(kGroup), "layouts = {},", R"(layouts = { { min_length = 1921, modules_left = { "layout" }, modules_center = {}, modules_right = {} } },)")));
}

TEST(Config, TheConfigsWalksReachAGroupsMembers) {
    const Config             config = LoadText(WithGroup(kGroup));
    std::vector<std::string> names;
    ForEachModuleInConfig(config, [&names](const ModuleInstance& instance) { names.push_back(instance.name); });
    EXPECT_EQ(names, (std::vector<std::string>{"title", "time", "layout", "cpu", "memory"}));
    EXPECT_TRUE(AnyModuleInConfig(config, [](const ModuleInstance& instance) { return instance.kind == ModuleKind::Memory; }));
    EXPECT_FALSE(AnyModuleInConfig(config, [](const ModuleInstance& instance) { return instance.kind == ModuleKind::Battery; }));
}

TEST(Config, AGroupsCustomMembersDeclareTheirClasses) {
    const std::string config = WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "memory", "weather" })") + R"(
        weather = { type = "custom", command = { "cmd" }, run = "interval", timeout = 5, output = "json", classes = { "sunny" }, format = "{text}", icon = "", interval = 30 },)");
    EXPECT_NO_THROW(LoadStyled(config, std::string(kValidStyle) + "weather {\n  sunny {\n    color: #ff0000\n  }\n}\n"));
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

TEST(Config, LayoutsAreRequiredAndNamedByTheirPlaceInErrors) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "layouts = {},", "")); }, ConfigErrorWith("layouts is missing"));
    EXPECT_THAT([] { return LoadText(WithLayouts(R"({ { min_length = 1921, modules_left = {}, modules_center = {} } })")); },
                ConfigErrorWith("layouts[1].modules_right is missing"));
    EXPECT_THAT([] { return LoadText(WithLayouts(R"({ { min_length = 1921, modules_left = {}, modules_center = {}, modules_right = {}, edge = "top" } })")); },
                ConfigErrorWith("layouts[1].edge is not a known key"));
    EXPECT_THAT([] { return LoadText(WithLayouts(R"({ { min_length = 1921, modules_left = { "clock" }, modules_center = {}, modules_right = {} } })")); },
                ConfigErrorWith("layouts[1].modules_left shows 'clock', which is not defined in modules"));
}

TEST(Config, EachLayoutMustStartPastTheOneBefore) {
    EXPECT_THAT(
        [] {
            return LoadText(WithLayouts(R"({
            { min_length = 2560, modules_left = {}, modules_center = {}, modules_right = {} },
            { min_length = 1921, modules_left = {}, modules_center = {}, modules_right = {} },
        })"));
        },
        ConfigErrorWith("layouts[2].min_length is 1921, it must be greater than the layout before it (2560)"));
}

TEST(Config, TilingModulesNeedATilingWindowManager) {
    struct Tiled {
        std::string_view type;
        std::string_view definition;
    };
    for (const Tiled& tiled : std::initializer_list<Tiled>{
             {.type = "workspaces", .definition = R"(        tiled = { type = "workspaces" },)"},
             {.type = "windows", .definition = R"(        tiled = { type = "windows" },)"},
             {.type = "binding_mode", .definition = R"(        tiled = { type = "binding_mode" },)"},
             {.type = "tiling_direction", .definition = R"(        tiled = { type = "tiling_direction", horizontal = "H", vertical = "V" },)"},
         }) {
        const std::string config = WithRightModules(R"({ "tiled" })", tiled.definition);
        EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith(std::string(tiled.type) + " needs a tiling window manager, window_manager 'windows' is none"));
    }
    // Through a layout's lists too, not only the top-level ones.
    EXPECT_THAT(
        [] {
            return LoadText(Replace(WithLayouts(R"({ { min_length = 1921, modules_left = { "spaces" }, modules_center = {}, modules_right = {} } })"), "    modules = {\n",
                                    "    modules = {\n        spaces = { type = \"workspaces\" },\n"));
        },
        ConfigErrorWith("workspaces needs a tiling window manager"));

    const std::string onGlazeWm = Replace(WithRightModules(R"({ "desktops", "apps", "mode", "tiling" })", R"(        desktops = { type = "workspaces" },
        apps = { type = "windows" },
        mode = { type = "binding_mode" },
        tiling = { type = "tiling_direction", horizontal = "H", vertical = "V" },)"),
                                          R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config      config    = LoadText(onGlazeWm);
    EXPECT_EQ(config.windowManager, WindowManagerKind::GlazeWm);
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::Workspaces, ModuleKind::Windows, ModuleKind::BindingMode, ModuleKind::TilingDirection}));
    EXPECT_EQ(SettingsOf<TilingDirectionConfig>(config.modules.right, 3).vertical, L"V");
    EXPECT_THAT([&onGlazeWm] { return LoadText(Replace(onGlazeWm, R"(horizontal = "H", )", "")); }, ConfigErrorWith("modules.tiling.horizontal is missing"));
}

TEST(Config, ModuleNamesUseASafeAlphabet) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "two words" })", R"(        ["two words"] = { type = "cpu", format = "{usage}", icon = "", interval = 1 },)")); },
                ConfigErrorWith("is not a valid module name"));
}

TEST(Config, TypeMustBeAKnownModuleType) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(type = "clock")", R"(type = "teapot")")); },
                ConfigErrorWith("modules.time.type must be one of clock, window, workspaces, windows, binding_mode, tiling_direction, cpu, memory, audio, "
                                "network, battery, tray, custom, counter, group, got 'teapot'"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(type = "clock", )", "")); }, ConfigErrorWith("modules.time.type is missing"));
}

TEST(Config, OneTypeCanBeShownUnderSeveralNames) {
    const Config config = LoadText(WithRightModules(R"({ "cpu", "cpu_slow" })", R"(        cpu = { type = "cpu", format = "{usage}%", icon = "c", interval = 1 },
        cpu_slow = { type = "cpu", format = "{usage}", icon = "", interval = 5 },)"));
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::Cpu, ModuleKind::Cpu}));
    EXPECT_EQ(config.modules.right.at(1).name, "cpu_slow");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 0).format, "{usage}%");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 1).interval, std::chrono::seconds(5));
}

// Audio needs no window manager, so it loads with window_manager "windows".
TEST(Config, AudioParsesItsFormatsIconsAndStep) {
    const std::string audio =
        R"(        volume = { type = "audio", flow = "output", format = "{volume}%", format_muted = "muted", icon = "v", icon_muted = "m", step = 5, tooltip = "{device}" },)";
    const Config       config   = LoadText(WithRightModules(R"({ "volume" })", audio));
    const AudioConfig& settings = SettingsOf<AudioConfig>(config.modules.right, 0);
    EXPECT_EQ(settings.formatMuted, "muted");
    EXPECT_EQ(settings.iconMuted, L"m");
    EXPECT_EQ(settings.stepPercent, 5);
    EXPECT_EQ(settings.tooltip, "{device}");
    EXPECT_THAT([&audio] { return LoadText(WithRightModules(R"({ "volume" })", Replace(audio, "step = 5", "step = 101"))); },
                ConfigErrorWith("modules.volume.step is a percent of full volume, from 1 to 100, got 101"));
    EXPECT_THAT([&audio] { return LoadText(WithRightModules(R"({ "volume" })", Replace(audio, R"(icon_muted = "m", )", ""))); },
                ConfigErrorWith("modules.volume.icon_muted is missing"));
}

TEST(Config, BatteryParsesItsFormatsIconsAndThresholds) {
    const std::string    battery  = R"(        power = { type = "battery", format_discharging = "{capacity}%", format_charging = "", format_plugged = "", icons = { "e", "f" }, )"
                                    R"(icon_charging = "c", icon_plugged = "p", warning_at = 40, critical_at = 15 },)";
    const Config         config   = LoadText(WithRightModules(R"({ "power" })", battery));
    const BatteryConfig& settings = SettingsOf<BatteryConfig>(config.modules.right, 0);
    EXPECT_EQ(settings.formatDischarging, "{capacity}%");
    EXPECT_EQ(settings.icons, (std::vector<std::wstring>{L"e", L"f"}));
    EXPECT_EQ(settings.iconPlugged, L"p");
    EXPECT_EQ(settings.warningAtPercent, 40);
    EXPECT_EQ(settings.criticalAtPercent, 15);
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, "warning_at = 40", "warning_at = 101"))); },
                ConfigErrorWith("modules.power.warning_at is a percent of full charge, from 1 to 100, got 101"));
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, "critical_at = 15", "critical_at = 40"))); },
                ConfigErrorWith("modules.power.critical_at (40) must be under modules.power.warning_at (40)"));
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, R"({ "e", "f" })", "{}"))); },
                ConfigErrorWith("modules.power.icons must hold at least one icon, emptiest first"));
}

TEST(Config, ActionsResolveToAbsoluteExecutables) {
    const Config config =
        LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { "cmd", "/c", "exit 0" }, on_scroll_up = { "cmd" }, on_middle_click = { "cmd" } })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.click.has_value());
    const Command& click = std::get<Command>(*actions.click);
    EXPECT_TRUE(click.program.is_absolute());
    EXPECT_EQ(click.arguments, (std::vector<std::wstring>{L"/c", L"exit 0"}));
    EXPECT_TRUE(actions.scrollUp.has_value());
    EXPECT_TRUE(actions.middleClick.has_value());
    EXPECT_FALSE(actions.rightClick.has_value());
    EXPECT_FALSE(actions.scrollDown.has_value());
}

TEST(Config, WindowManagerActionsSendTheirTextAsWritten) {
    const std::string    glazeWm = Replace(kValidConfig, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config         config  = LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_scroll_up = { window_manager = "focus --next-workspace" } })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.scrollUp.has_value());
    EXPECT_EQ(std::get<WindowManagerCommand>(*actions.scrollUp).text, "focus --next-workspace");

    EXPECT_THAT([&glazeWm] { return LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "focus\n--workspace 2" } })")); },
                ConfigErrorWith("modules.time.on_click.window_manager must be a command on one line"));
    EXPECT_THAT([&glazeWm] { return LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "" } })")); },
                ConfigErrorWith("must be a command on one line"));
    EXPECT_THAT([&glazeWm] { return LoadText(Replace(glazeWm, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "x", extra = 1 } })")); },
                ConfigErrorWith("modules.time.on_click.extra is not a known key"));
}

TEST(Config, MenuActionsListThroughOneCommandAndSelectThroughAnother) {
    const Config         config  = LoadText(Replace(kValidConfig, "interval = 60 }",
                                                    R"(interval = 60, on_click = { menu = { command = { "cmd", "/c", "list" }, timeout = 5, on_select = { "explorer", "/select" } } } })"));
    const ModuleActions& actions = SettingsOf<TextModuleConfig>(config.modules.center, 0).actions;
    ASSERT_TRUE(actions.click.has_value());
    const MenuAction& menu = std::get<MenuAction>(*actions.click);
    EXPECT_TRUE(menu.command.program.is_absolute());
    EXPECT_EQ(menu.command.arguments, (std::vector<std::wstring>{L"/c", L"list"}));
    EXPECT_EQ(menu.timeout, std::chrono::seconds(5));
    EXPECT_EQ(menu.onSelect.arguments, (std::vector<std::wstring>{L"/select"}));

    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { menu = { command = { "cmd" }, timeout = 5 } } })")); },
                ConfigErrorWith("on_select"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { menu = { command = { "cmd" }, on_select = { "cmd" } } } })")); },
                ConfigErrorWith("timeout"));
    EXPECT_THAT(
        [] {
            return LoadText(
                Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { menu = { command = { "cmd" }, timeout = 5, on_select = { "cmd" }, items = 1 } } })"));
        },
        ConfigErrorWith("modules.time.on_click.menu.items is not a known key"));
    EXPECT_THAT(
        [] {
            return LoadText(
                Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { menu = { command = { "cmd" }, timeout = 5, on_select = { "cmd" } }, extra = 1 } })"));
        },
        ConfigErrorWith("modules.time.on_click.extra is not a known key"));
    EXPECT_THAT(
        [] {
            return LoadText(
                Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { menu = { command = { "cmd" }, timeout = 5, on_select = { "cmd", "/c", "pick" } } } })"));
        },
        ConfigErrorWith("modules.time.on_click.menu.on_select may not run cmd.exe"));
}

TEST(Config, WindowManagerActionsNeedAWindowManagerThatTakesCommands) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { window_manager = "focus --next-workspace" } })")); },
                ConfigErrorWith("window_manager 'windows' takes none"));
}

TEST(Config, ActionsMustNameAnExistingExecutable) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", "interval = 60, on_click = {} }")); }, ConfigErrorWith("modules.time.on_click must name a program"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { "intarsia-no-such-program" } })")); },
                ConfigErrorWith("not an existing file or a program on PATH"));
}

TEST(Config, ActionsRefuseScriptsThatWouldRunThroughCmd) {
    const std::filesystem::path script = std::filesystem::temp_directory_path() / "intarsia-test-action.bat";
    std::ofstream(script) << "@echo off\n";
    std::string escaped = script.string();
    for (size_t position = escaped.find('\\'); position != std::string::npos; position = escaped.find('\\', position + 2)) {
        escaped.insert(position, "\\");
    }
    const std::string config = Replace(kValidConfig, "interval = 60 }", "interval = 60, on_click = { \"" + escaped + "\" } }");
    EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith("which is not an .exe"));
    std::filesystem::remove(script);
}

TEST(Config, CustomModulesParseTheirCommandAndTimeout) {
    const Config        config = LoadText(WithRightModules(
        R"({ "hello" })",
        R"(        hello = { type = "custom", command = { "cmd", "/c", "echo hi" }, run = "interval", timeout = 5, output = "json", classes = { "sunny", "rain" }, format = "{text}", icon = "", interval = 30 },)"));
    const CustomConfig& custom = SettingsOf<CustomConfig>(config.modules.right, 0);
    EXPECT_EQ(custom.command.program.filename(), L"cmd.exe");
    EXPECT_EQ(custom.command.arguments, (std::vector<std::wstring>{L"/c", L"echo hi"}));
    const auto* run = std::get_if<IntervalRun>(&custom.run);
    if (run == nullptr) {
        FAIL() << "run = \"interval\" did not read as an interval run";
    }
    EXPECT_EQ(run->timeout, std::chrono::seconds(5));
    EXPECT_EQ(custom.output, CustomOutput::Json);
    EXPECT_EQ(custom.classes, (std::vector<std::string>{"sunny", "rain"}));
    EXPECT_EQ(custom.text.interval, std::chrono::seconds(30));
}

namespace {

    std::string WithCustomRun(std::string_view run) {
        return WithRightModules(
            R"({ "hello" })",
            std::format(R"(        hello = {{ type = "custom", command = {{ "cmd" }}, {} output = "text", classes = {{}}, format = "{{text}}", icon = "", interval = 30 }},)",
                        run));
    }

} // namespace

TEST(Config, ContinuousCustomModulesTakeNoTimeout) {
    const Config config = LoadText(WithCustomRun(R"(run = "continuous",)"));
    EXPECT_TRUE(std::holds_alternative<ContinuousRun>(SettingsOf<CustomConfig>(config.modules.right, 0).run));
    EXPECT_THAT([] { return LoadText(WithCustomRun(R"(run = "continuous", timeout = 5,)")); }, ConfigErrorWith("applies only to run = \"interval\""));
}

TEST(Config, CustomModulesNeedARun) {
    EXPECT_THAT([] { return LoadText(WithCustomRun("timeout = 5,")); }, ConfigErrorWith("run"));
    EXPECT_THAT([] { return LoadText(WithCustomRun(R"(run = "forever", timeout = 5,)")); }, ConfigErrorWith("run"));
    EXPECT_THAT([] { return LoadText(WithCustomRun(R"(run = "interval",)")); }, ConfigErrorWith("timeout"));
}

namespace {

    std::string WithWeather(std::string_view classes) {
        return WithRightModules(
            R"({ "weather" })",
            std::format(
                R"(        weather = {{ type = "custom", command = {{ "cmd" }}, run = "interval", timeout = 5, output = "json", classes = {}, format = "{{text}}", icon = "", interval = 30 }},)",
                classes));
    }

} // namespace

TEST(Config, DeclaredClassesColorTheirCustomModule) {
    const Config config = LoadStyled(
        WithWeather(R"({ "sunny", "rain" })"),
        std::string(kValidStyle) + "weather {\n  sunny {\n    color: #ff0000\n    icon {\n      color: #00ff00\n    }\n  }\n}\nmodule {\n  rain {\n    color: #0000ff\n  }\n}\n");
    const ModuleStyle& style = config.modules.right.at(0).style;
    ASSERT_EQ(style.classes.size(), 2u);
    EXPECT_EQ(style.classes[0].name, "sunny");
    EXPECT_FLOAT_EQ(style.classes[0].colors.text.r, 1.0f);
    EXPECT_FLOAT_EQ(style.classes[0].colors.icon.g, 1.0f);
    EXPECT_EQ(style.classes[1].name, "rain");
    // The icon takes the class's text color when no block colors it.
    EXPECT_FLOAT_EQ(style.classes[1].colors.icon.b, 1.0f);
}

TEST(Config, AClassNoCustomModuleDeclaresIsAnError) {
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "weather {\n  snowy {\n    color: #ff0000\n  }\n}\n"); },
                ConfigErrorWith("weather: snowy is not a block a module holds"));
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "module {\n  snowy {\n    color: #ff0000\n  }\n}\n"); },
                ConfigErrorWith("module: snowy is not a block module holds"));
    // Declared by the weather module, but the block names a clock.
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "time {\n  sunny {\n    color: #ff0000\n  }\n}\n"); },
                ConfigErrorWith("time: sunny is not a block a module holds"));
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "weather {\n  sunny {\n    item {\n      color: #ff0000\n    }\n  }\n}\n"); },
                ConfigErrorWith("weather > sunny: item is not a block weather > sunny holds, which are: icon"));
}

TEST(Config, DeclaredClassesAreClassNamesOfTheirOwn) {
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "two words" })")); }, ConfigErrorWith("modules.weather.classes lists 'two words', which is not a class name"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "alert" })")); }, ConfigErrorWith("modules.weather.classes lists 'alert', a class intarsia styles itself"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "sunny", "sunny" })")); }, ConfigErrorWith("modules.weather.classes lists 'sunny' twice"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "rain", "sunny", "snow", "sunny" })")); }, ConfigErrorWith("modules.weather.classes lists 'sunny' twice"));
}

TEST(Config, CounterModulesReadTheirPathAndCombine) {
    const Config         config  = LoadText(WithRightModules(
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
    EXPECT_THAT([&load] { return load(R"(\\No Such Object\\Nothing)"); }, ConfigErrorWith("modules.c.counter names no counter this system has: PdhAddEnglishCounterW"));
    EXPECT_THAT([&load] { return load(R"(\\Processor(_Total)\\No Such Counter)"); }, ConfigErrorWith("modules.c.counter names no counter this system has"));
    EXPECT_THAT([&load] { return load(R"(Processor(_Total)\\% Processor Time)"); }, ConfigErrorWith("modules.c.counter must be a local counter path"));
    EXPECT_THAT([&load] { return load(R"(\\\\otherhost\\Processor(_Total)\\% Processor Time)"); }, ConfigErrorWith("modules.c.counter must be a local counter path"));
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
        EXPECT_THAT([&removed] { return LoadText(removed.config); }, ConfigErrorWith(removed.error));
    }
    for (const std::string_view library : {"os", "io", "package", "debug", "coroutine", "utf8"}) {
        const std::string config = std::string("return ") + std::string(library) + ".x";
        EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith(std::string("attempt to index a nil value (global '") + std::string(library) + "')"));
    }
}

// One C call can allocate any amount while running no instructions the count hook sees.
TEST(Config, AConfigThatAllocatesTooMuchIsStoppedWithAnError) {
    EXPECT_THAT([] { return LoadText("return { x = ('x'):rep(2^25) }"); }, ConfigErrorWith("the config used more than 16 MiB of memory"));
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

TEST(Config, TooltipIsOptional) {
    EXPECT_FALSE(SettingsOf<TextModuleConfig>(LoadText(kValidConfig).modules.center, 0).tooltip.has_value());
    const Config config = LoadText(Replace(kValidConfig, R"(interval = 60 },)", R"(interval = 60, tooltip = "{time:%A %d %B %Y}" },)"));
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.center, 0).tooltip, "{time:%A %d %B %Y}");
}

TEST(Config, AnEndlessConfigIsStoppedWithAnError) {
    EXPECT_THAT([] { return LoadText("while true do end"); }, ConfigErrorWith("without finishing, is there an endless loop?"));
}

#include "config/Config.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

namespace {

    constexpr std::string_view kValidConfig = R"lua(
return {
    bar = { edge = "top", layer = "top", space = "reserve", thickness = 54 },
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

    constexpr std::string_view kValidStyle = R"css(
* { font-family: "Some Font"; font-size: 15px; color: #e0def4; }
)css";

    // A config and a style.css beside it in a directory that deletes itself, so each case runs LoadConfig on real files.
    class TempConfig {
      public:
        TempConfig(std::string_view config, std::string_view style) :
            _directory(std::filesystem::temp_directory_path() / ("intarsia-test-" + std::to_string(std::hash<std::string>{}(std::string(config) + std::string(style))))) {
            std::filesystem::create_directories(_directory);
            std::ofstream(_directory / "config.lua", std::ios::binary) << config;
            std::ofstream(_directory / "style.css", std::ios::binary) << style;
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
    const Config       config = LoadStyled(kValidConfig, std::string(kValidStyle) + "#time { color: #ff0000; } clock .icon { color: #00ff00; }");
    const ModuleStyle& time   = config.modules.center.at(0).style;
    EXPECT_FLOAT_EQ(time.color.r, 1.0f);
    EXPECT_FLOAT_EQ(time.icon.color.g, 1.0f);
    // The window module takes the bar's color, since no rule names it.
    EXPECT_FLOAT_EQ(config.modules.left.at(0).style.color.r, 0xe0 / 255.0f);
}

TEST(Config, StyleIdsMustNameADefinedModule) {
    EXPECT_THAT([] { return LoadStyled(kValidConfig, std::string(kValidStyle) + "#tmie { color: #ff0000; }"); }, ConfigErrorWith("#tmie names no module"));
}

TEST(Config, StyleErrorsNameTheStyleFileAndLine) {
    EXPECT_THAT([] { return LoadStyled(kValidConfig, std::string(kValidStyle) + "\n.module { colour: #ff0000; }"); },
                ConfigErrorWith("style.css:4: .module: colour is not a property"));
}

TEST(Config, DividerTakesOnlyItsType) {
    const Config config = LoadStyled(WithRightModules(R"({ "line" })", R"(        line = { type = "divider" },)"), std::string(kValidStyle) + "#line { width: 2px; height: 50%; }");
    EXPECT_EQ(Kinds(config.modules.right), std::vector<ModuleKind>{ModuleKind::Divider});
    EXPECT_FLOAT_EQ(config.modules.right.at(0).style.lineWidthDips, 2.0f);
    EXPECT_FLOAT_EQ(config.modules.right.at(0).style.lineHeight, 0.5f);
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "line" })", R"(        line = { type = "divider", width = 2 },)")); },
                ConfigErrorWith("modules.line.width is not a known key"));
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

TEST(Config, EdgeMustBeOneOfFour) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(edge = "top")", R"(edge = "side")")); }, ConfigErrorWith("got 'side'"));
}

TEST(Config, SizesMustBePositiveAndIntervalWholeSeconds) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 0")); }, ConfigErrorWith("bar.thickness must be greater than 0"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = 1.5")); }, ConfigErrorWith("modules.time.interval must be a whole number"));
}

// inf passes a floor check and casts to an undefined integer, and a float holds whole numbers exactly only up to 2^24.
TEST(Config, NumbersMustBeFiniteAndWholeNumbersBounded) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = math.huge")); }, ConfigErrorWith("bar.thickness must be greater than 0"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "thickness = 54", "thickness = 0/0")); }, ConfigErrorWith("bar.thickness must be greater than 0"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = math.huge")); }, ConfigErrorWith("modules.time.interval must be greater than 0"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60", "interval = 1e30")); }, ConfigErrorWith("modules.time.interval must be a whole number up to 16777216"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "max_length = 80", "max_length = 2^24 + 2")); },
                ConfigErrorWith("modules.title.max_length must be a whole number up to 16777216"));
}

TEST(Config, ModuleTableKeysMustBeStrings) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "    modules = {\n", "    modules = {\n        [1] = { type = \"clock\" },\n")); },
                ConfigErrorWith("modules has a key that is not a string"));
}

TEST(Config, ConfigFilesMustBeUtf8) {
    EXPECT_THAT([] { return LoadText("return {} -- \xFF\xFE"); }, ConfigErrorWith("config file"));
    EXPECT_THAT([] { return LoadText("return {} -- \xFF\xFE"); }, ConfigErrorWith("is not valid UTF-8"));
    EXPECT_THAT([] { return LoadStyled(kValidConfig, "/* \xC0\xAF */"); }, ConfigErrorWith("style file"));
}

// A relative path with a directory part would resolve against the bar's current directory.
TEST(Config, ActionsRefuseRelativePathsWithDirectories) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { ".\\cmd.exe" } })")); },
                ConfigErrorWith("not an existing file or a program on PATH"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "interval = 60 }", R"(interval = 60, on_click = { "System32\\cmd.exe" } })")); },
                ConfigErrorWith("not an existing file or a program on PATH"));
}

TEST(Config, ModuleListsMustBePlainListsOfStrings) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", R"({ "time", x = 1 })")); }, ConfigErrorWith("modules_center must be a plain list"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"({ "time" })", "{ 7 }")); }, ConfigErrorWith("modules_center[1] must be a string"));
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

TEST(Config, AGroupAndItsMembersAreStyledByTheirOwnNames) {
    const Config          config = LoadStyled(WithGroup(kGroup),
                                              std::string(kValidStyle) +
                                                  "#cpu { color: #ff0000; } #layout { transition: width 200ms; } "
                                                           "#layout .icon { transform: rotate(-90deg); } #layout.expanded .icon { transform: rotate(90deg); }");
    const ModuleInstance& group  = config.modules.right.at(0);
    EXPECT_EQ(group.style.transition.duration, std::chrono::milliseconds(200));
    EXPECT_FLOAT_EQ(group.style.icon.rotationDegrees, -90.0f);
    EXPECT_FLOAT_EQ(group.style.expandedIconRotationDegrees, 90.0f);
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

TEST(Config, AGroupsMembersMustSuitTheWindowManager) {
    EXPECT_THAT(
        [] {
            return LoadText(WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "memory", "spaces" })") + R"(
        spaces = { type = "workspaces" },)"));
        },
        ConfigErrorWith("workspaces needs a tiling window manager"));
    EXPECT_THAT([] { return LoadText(WithGroup(Replace(kGroup, R"(interval = 1 },)", R"(interval = 1, on_click = { window_manager = "focus --next-workspace" } },)"))); },
                ConfigErrorWith("window_manager 'windows' takes none"));
}

TEST(Config, AGroupsCustomMembersDeclareTheirClasses) {
    const std::string config = WithGroup(Replace(kGroup, R"({ "cpu", "memory" })", R"({ "cpu", "memory", "weather" })") + R"(
        weather = { type = "custom", command = { "cmd" }, timeout = 5, output = "json", classes = { "sunny" }, format = "{text}", icon = "", interval = 30 },)");
    EXPECT_NO_THROW(LoadStyled(config, std::string(kValidStyle) + "#weather.sunny { color: #ff0000; }"));
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
    EXPECT_THAT([] { return LoadText(WithLayouts("{ 7 }")); }, ConfigErrorWith("layouts[1] must be a table, got number"));
    EXPECT_THAT([] { return LoadText(WithLayouts("{ x = {} }")); }, ConfigErrorWith("layouts must be a plain list"));
    EXPECT_THAT([] { return LoadText(WithLayouts(R"({ { min_length = 1921, modules_left = {}, modules_center = {} } })")); },
                ConfigErrorWith("layouts[1].modules_right is missing"));
    EXPECT_THAT([] { return LoadText(WithLayouts(R"({ { min_length = 1921, modules_left = {}, modules_center = {}, modules_right = {}, edge = "top" } })")); },
                ConfigErrorWith("layouts[1].edge is not a known key"));
    EXPECT_THAT([] { return LoadText(WithLayouts(R"({ { min_length = 0, modules_left = {}, modules_center = {}, modules_right = {} } })")); },
                ConfigErrorWith("layouts[1].min_length must be greater than 0"));
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

TEST(Config, ALayoutsModulesMustSuitTheWindowManager) {
    EXPECT_THAT(
        [] {
            return LoadText(Replace(WithLayouts(R"({ { min_length = 1921, modules_left = { "spaces" }, modules_center = {}, modules_right = {} } })"), "    modules = {\n",
                                    "    modules = {\n        spaces = { type = \"workspaces\" },\n"));
        },
        ConfigErrorWith("workspaces needs a tiling window manager"));
}

TEST(Config, ModuleNamesUseASafeAlphabet) {
    EXPECT_THAT([] { return LoadText(WithRightModules(R"({ "two words" })", R"(        ["two words"] = { type = "cpu", format = "{usage}", icon = "", interval = 1 },)")); },
                ConfigErrorWith("is not a valid module name"));
}

TEST(Config, TypeMustBeAKnownModuleType) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(type = "clock")", R"(type = "teapot")")); },
                ConfigErrorWith("modules.time.type is 'teapot', known types are: clock, window, workspaces, windows, binding_mode, tiling_direction, divider, cpu, memory, audio, "
                                "network, battery, tray, custom, counter, group"));
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

TEST(Config, WindowManagerMustBeAKnownBackend) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(window_manager = "windows")", R"(window_manager = "i3")")); },
                ConfigErrorWith("window_manager must be one of windows, glazewm, got 'i3'"));
}

TEST(Config, WorkspacesNeedAWindowManagerWithWorkspaces) {
    const std::string onWindows = WithRightModules(R"({ "desktops" })", R"(        desktops = { type = "workspaces" },)");
    EXPECT_THAT([&onWindows] { return LoadText(onWindows); }, ConfigErrorWith("window_manager 'windows' is none"));

    const std::string onGlazeWm = Replace(onWindows, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config      config    = LoadText(onGlazeWm);
    EXPECT_EQ(config.windowManager, WindowManagerKind::GlazeWm);
    EXPECT_EQ(Kinds(config.modules.right), std::vector<ModuleKind>{ModuleKind::Workspaces});
    EXPECT_THAT([&onGlazeWm] { return LoadText(Replace(onGlazeWm, R"(type = "workspaces")", R"(type = "workspaces", size = 2)")); },
                ConfigErrorWith("modules.desktops.size is not a known key"));
}

TEST(Config, WindowsNeedAWindowManagerWithWorkspaces) {
    const std::string onWindows = WithRightModules(R"({ "apps" })", R"(        apps = { type = "windows" },)");
    EXPECT_THAT([&onWindows] { return LoadText(onWindows); }, ConfigErrorWith("windows needs a tiling window manager"));

    const std::string onGlazeWm = Replace(onWindows, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    EXPECT_EQ(Kinds(LoadText(onGlazeWm).modules.right), std::vector<ModuleKind>{ModuleKind::Windows});
    EXPECT_THAT([&onGlazeWm] { return LoadText(Replace(onGlazeWm, R"(type = "windows")", R"(type = "windows", icon_size = 2)")); },
                ConfigErrorWith("modules.apps.icon_size is not a known key"));
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
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, ", critical_at = 15", ""))); }, ConfigErrorWith("critical_at"));
    EXPECT_THAT([&battery] { return LoadText(WithRightModules(R"({ "power" })", Replace(battery, R"({ "e", "f" })", "{}"))); },
                ConfigErrorWith("modules.power.icons must hold at least one icon, emptiest first"));
}

TEST(Config, BindingModeAndTilingDirectionParseOnATilingWindowManager) {
    const std::string onWindows = WithRightModules(R"({ "mode", "tiling" })", R"(        mode = { type = "binding_mode" },
        tiling = { type = "tiling_direction", horizontal = "H", vertical = "V" },)");
    EXPECT_THAT([&onWindows] { return LoadText(onWindows); }, ConfigErrorWith("binding_mode needs a tiling window manager"));

    const std::string onGlazeWm = Replace(onWindows, R"(window_manager = "windows")", R"(window_manager = "glazewm")");
    const Config      config    = LoadText(onGlazeWm);
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::BindingMode, ModuleKind::TilingDirection}));
    EXPECT_EQ(SettingsOf<TilingDirectionConfig>(config.modules.right, 1).vertical, L"V");
    EXPECT_THAT([&onGlazeWm] { return LoadText(Replace(onGlazeWm, R"(horizontal = "H", )", "")); }, ConfigErrorWith("modules.tiling.horizontal is missing"));
}

TEST(Config, WindowModuleNeedsAWholeMaxLength) {
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, "max_length = 80", "max_length = 2.5")); }, ConfigErrorWith("modules.title.max_length must be a whole number"));
}

TEST(Config, CpuAndMemoryShareTheTextModuleShape) {
    const Config config = LoadText(WithRightModules(R"({ "cpu", "memory" })", R"(        cpu = { type = "cpu", format = "{usage}%", icon = "c", interval = 1 },
        memory = { type = "memory", format = "{used:.1f} GB", icon = "m", interval = 2 },)"));
    EXPECT_EQ(Kinds(config.modules.right), (std::vector<ModuleKind>{ModuleKind::Cpu, ModuleKind::Memory}));
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 0).format, "{usage}%");
    EXPECT_EQ(SettingsOf<TextModuleConfig>(config.modules.right, 1).interval, std::chrono::seconds(2));
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
        R"(        hello = { type = "custom", command = { "cmd", "/c", "echo hi" }, timeout = 5, output = "json", classes = { "sunny", "rain" }, format = "{text}", icon = "", interval = 30 },)"));
    const CustomConfig& custom = SettingsOf<CustomConfig>(config.modules.right, 0);
    EXPECT_EQ(custom.command.program.filename(), L"cmd.exe");
    EXPECT_EQ(custom.command.arguments, (std::vector<std::wstring>{L"/c", L"echo hi"}));
    EXPECT_EQ(custom.timeout, std::chrono::seconds(5));
    EXPECT_EQ(custom.output, CustomOutput::Json);
    EXPECT_EQ(custom.classes, (std::vector<std::string>{"sunny", "rain"}));
    EXPECT_EQ(custom.text.interval, std::chrono::seconds(30));
}

namespace {

    std::string WithWeather(std::string_view classes) {
        return WithRightModules(
            R"({ "weather" })",
            std::format(
                R"(        weather = {{ type = "custom", command = {{ "cmd" }}, timeout = 5, output = "json", classes = {}, format = "{{text}}", icon = "", interval = 30 }},)",
                classes));
    }

} // namespace

TEST(Config, DeclaredClassesColorTheirCustomModule) {
    const Config       config = LoadStyled(WithWeather(R"({ "sunny", "rain" })"),
                                           std::string(kValidStyle) + "#weather.sunny { color: #ff0000; } #weather.sunny .icon { color: #00ff00; } custom.rain { color: #0000ff; }");
    const ModuleStyle& style  = config.modules.right.at(0).style;
    ASSERT_EQ(style.classes.size(), 2u);
    EXPECT_EQ(style.classes[0].name, "sunny");
    EXPECT_FLOAT_EQ(style.classes[0].colors.text.r, 1.0f);
    EXPECT_FLOAT_EQ(style.classes[0].colors.icon.g, 1.0f);
    EXPECT_EQ(style.classes[1].name, "rain");
    // The icon takes the class's text color when no rule colors it.
    EXPECT_FLOAT_EQ(style.classes[1].colors.icon.b, 1.0f);
}

TEST(Config, AClassNoCustomModuleDeclaresIsAnError) {
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "#weather.snowy { color: #ff0000; }"); },
                ConfigErrorWith("'.snowy' is not a class intarsia styles"));
    // Declared by the weather module, but the rule names a clock.
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "#time.sunny { color: #ff0000; }"); },
                ConfigErrorWith("'.sunny' is not a class intarsia styles"));
    EXPECT_THAT([] { return LoadStyled(WithWeather(R"({ "sunny" })"), std::string(kValidStyle) + "#weather.sunny .item { color: #ff0000; }"); },
                ConfigErrorWith("style a module's text and .icon only"));
}

TEST(Config, DeclaredClassesAreClassNamesOfTheirOwn) {
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "two words" })")); }, ConfigErrorWith("modules.weather.classes lists 'two words', which is not a class name"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "9lives" })")); }, ConfigErrorWith("which is not a class name"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "alert" })")); }, ConfigErrorWith("modules.weather.classes lists 'alert', a class intarsia styles itself"));
    EXPECT_THAT([] { return LoadText(WithWeather(R"({ "sunny", "sunny" })")); }, ConfigErrorWith("modules.weather.classes lists 'sunny' twice"));
}

TEST(Config, CustomModulesNeedACommand) {
    EXPECT_THAT(
        [] {
            return LoadText(WithRightModules(R"({ "hello" })",
                                             R"(        hello = { type = "custom", timeout = 5, output = "text", classes = {}, format = "{text}", icon = "", interval = 30 },)"));
        },
        ConfigErrorWith("modules.hello.command is missing"));
}

TEST(Config, CustomOutputIsTextOrJson) {
    EXPECT_THAT(
        [] {
            return LoadText(WithRightModules(
                R"({ "hello" })",
                R"(        hello = { type = "custom", command = { "cmd" }, timeout = 5, output = "xml", classes = {}, format = "{text}", icon = "", interval = 30 },)"));
        },
        ConfigErrorWith("modules.hello.output must be one of text, json, got 'xml'"));
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

TEST(Config, CounterCombineIsSumMaxOrAverage) {
    EXPECT_THAT(
        [] {
            return LoadText(WithRightModules(
                R"({ "c" })", R"(        c = { type = "counter", counter = "\\Memory\\Available MBytes", combine = "median", format = "{value}", icon = "", interval = 1 },)"));
        },
        ConfigErrorWith("modules.c.combine must be one of sum, max, average, got 'median'"));
}

TEST(Config, TheConfigCannotReadFiles) {
    EXPECT_THAT([] { return LoadText("dofile('C:/Windows/win.ini')"); }, ConfigErrorWith("attempt to call a nil value (global 'dofile')"));
    EXPECT_THAT([] { return LoadText("loadfile('C:/Windows/win.ini')"); }, ConfigErrorWith("attempt to call a nil value (global 'loadfile')"));
}

// load takes precompiled bytecode, which Lua does not verify.
TEST(Config, TheConfigCannotLoadChunks) {
    EXPECT_THAT([] { return LoadText("load('return 1')"); }, ConfigErrorWith("attempt to call a nil value (global 'load')"));
}

TEST(Config, TheConfigHasNoLibraryThatReachesOutsideTheProcess) {
    for (const std::string_view library : {"os", "io", "package", "debug", "coroutine", "utf8"}) {
        const std::string config = std::string("return ") + std::string(library) + ".x";
        EXPECT_THAT([&config] { return LoadText(config); }, ConfigErrorWith(std::string("attempt to index a nil value (global '") + std::string(library) + "')"));
    }
    EXPECT_THAT([] { return LoadText("return require('os')"); }, ConfigErrorWith("attempt to call a nil value (global 'require')"));
}

// A protected call could swallow the abort, and Lua runs a __gc metamethod with hooks off, so an endless loop there
// could never be stopped: neither can be written.
TEST(Config, AnEndlessConfigCannotCatchOrHideFromItsAbort) {
    EXPECT_THAT([] { return LoadText("while true do pcall(function() while true do end end) end"); }, ConfigErrorWith("attempt to call a nil value (global 'pcall')"));
    EXPECT_THAT([] { return LoadText("xpcall(function() end, print)"); }, ConfigErrorWith("attempt to call a nil value (global 'xpcall')"));
    EXPECT_THAT([] { return LoadText("setmetatable({}, { __gc = function() while true do end end })"); }, ConfigErrorWith("attempt to call a nil value (global 'setmetatable')"));
}

// A short pattern backtracks for longer than the bar lives, inside C calls the instruction count never sees.
TEST(Config, AConfigCannotRunAPatternMatch) {
    EXPECT_THAT([] { return LoadText("return (('a'):rep(1000)):find(('.-'):rep(10) .. 'b')"); }, ConfigErrorWith("attempt to call a nil value (method 'find')"));
    EXPECT_THAT([] { return LoadText("return string.match('a', 'a')"); }, ConfigErrorWith("attempt to call a nil value (field 'match')"));
    EXPECT_THAT([] { return LoadText("return string.gmatch('a', 'a')"); }, ConfigErrorWith("attempt to call a nil value (field 'gmatch')"));
    EXPECT_THAT([] { return LoadText("return string.gsub('a', 'a', 'b')"); }, ConfigErrorWith("attempt to call a nil value (field 'gsub')"));
}

// One C call can allocate any amount while running no instructions the count hook sees.
TEST(Config, AConfigThatAllocatesTooMuchIsStoppedWithAnError) {
    EXPECT_THAT([] { return LoadText("return { x = ('x'):rep(2^25) }"); }, ConfigErrorWith("the config used more than 16 MiB of memory"));
    EXPECT_THAT([] { return LoadText("local t = {} for i = 1, 1e7 do t[i] = i end return t"); }, ConfigErrorWith("the config used more than 16 MiB of memory"));
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
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(max_length = 80 })", R"(max_length = 80, tooltip = 7 })")); },
                ConfigErrorWith("modules.title.tooltip must be a string"));
}

TEST(Config, LayerIsTopOrBottom) {
    EXPECT_EQ(LoadText(kValidConfig).bar.layer, Layer::Top);
    EXPECT_EQ(LoadText(Replace(kValidConfig, R"(layer = "top")", R"(layer = "bottom")")).bar.layer, Layer::Bottom);
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(layer = "top")", R"(layer = "overlay")")); }, ConfigErrorWith("bar.layer must be one of top, bottom, got 'overlay'"));
}

TEST(Config, SpaceIsReserveOrShare) {
    EXPECT_EQ(LoadText(kValidConfig).bar.space, Space::Reserve);
    EXPECT_EQ(LoadText(Replace(kValidConfig, R"(space = "reserve")", R"(space = "share")")).bar.space, Space::Share);
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(space = "reserve")", R"(space = "float")")); },
                ConfigErrorWith("bar.space must be one of reserve, share, got 'float'"));
    EXPECT_THAT([] { return LoadText(Replace(kValidConfig, R"(space = "reserve", )", "")); }, ConfigErrorWith("bar.space"));
}

TEST(Config, AnEndlessConfigIsStoppedWithAnError) {
    EXPECT_THAT([] { return LoadText("while true do end"); }, ConfigErrorWith("without finishing, is there an endless loop?"));
}
